package runs

import (
	"strings"
	"testing"
	"time"
)

// Contexte de référence : une partie de Tetris ouverte il y a une minute.
func testContext() Context {
	return Context{
		Secret:            []byte("secret-de-partie-32-octets-exact"),
		Seed:              1234567890,
		StartedAt:         time.Now().Add(-time.Minute),
		Now:               time.Now(),
		GameSlug:          "tetris-hard",
		MaxPlausibleScore: 999999,
	}
}

// sign scelle une soumission comme le ferait un client honnête.
func sign(ctx Context, sub *Submission) {
	sub.Seal = Seal(ctx.Secret, CanonicalPayload(ctx.Seed, sub.DurationMs, sub.ClaimedScore, sub.Events))
}

func TestPartieHonnete(t *testing.T) {
	ctx := testContext()
	sub := Submission{
		DurationMs: 30_000,
		Events: []Event{
			{At: 2000, Kind: "drop", Value: 0},
			{At: 4000, Kind: "lines", Value: 1},
			{At: 9000, Kind: "lines", Value: 2},
			{At: 15000, Kind: "tetris", Value: 0},
		},
		// 1 (drop) + 100 (1 ligne) + 200 (2 lignes) + 400 (tetris) = 701
		ClaimedScore: 701,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if !v.Accepted {
		t.Fatalf("partie honnête rejetée : %s", v.Reason)
	}
	if v.Score != 701 {
		t.Fatalf("score recalculé %d, attendu 701", v.Score)
	}
}

// Le cas central : le client annonce un score que ses événements ne produisent
// pas. C'est exactement ce que permettait la V1, où le score envoyé était accepté.
func TestScoreGonfleRejete(t *testing.T) {
	ctx := testContext()
	sub := Submission{
		DurationMs:   30_000,
		Events:       []Event{{At: 1000, Kind: "lines", Value: 1}},
		ClaimedScore: 999_000, // le joueur s'attribue un record
	}
	sign(ctx, &sub) // il scelle correctement : le sceau ne suffit donc pas

	v := Verify(ctx, sub)
	if v.Accepted {
		t.Fatal("un score sans rapport avec les événements a été accepté")
	}
	if !strings.Contains(v.Reason, "recalculé") {
		t.Fatalf("motif inattendu : %s", v.Reason)
	}
}

func TestSceauModifieRejete(t *testing.T) {
	ctx := testContext()
	sub := Submission{
		DurationMs:   10_000,
		Events:       []Event{{At: 1000, Kind: "lines", Value: 1}},
		ClaimedScore: 100,
	}
	sign(ctx, &sub)

	// Le joueur double son score après avoir scellé.
	sub.ClaimedScore = 200

	if Verify(ctx, sub).Accepted {
		t.Fatal("une soumission modifiée après scellement a été acceptée")
	}
}

func TestSecretDUneAutrePartieRejete(t *testing.T) {
	ctx := testContext()
	sub := Submission{
		DurationMs:   10_000,
		Events:       []Event{{At: 1000, Kind: "lines", Value: 1}},
		ClaimedScore: 100,
	}
	// Scellé avec le secret d'une autre partie.
	other := ctx
	other.Secret = []byte("un-autre-secret-de-32-octets-ok!")
	sign(other, &sub)

	if Verify(ctx, sub).Accepted {
		t.Fatal("un sceau produit avec un autre secret a été accepté")
	}
}

func TestGraineDifferenteRejetee(t *testing.T) {
	ctx := testContext()
	sub := Submission{DurationMs: 10_000, ClaimedScore: 0}
	sign(ctx, &sub)

	// Le serveur vérifie avec SA graine : un client qui rejoue une partie
	// enregistrée sous une autre graine est démasqué.
	ctx.Seed = 999
	if Verify(ctx, sub).Accepted {
		t.Fatal("une soumission scellée sous une autre graine a été acceptée")
	}
}

func TestDureeSuperieureAuTempsEcouleRejetee(t *testing.T) {
	ctx := testContext()
	ctx.StartedAt = time.Now().Add(-5 * time.Second) // partie ouverte il y a 5 s

	sub := Submission{
		DurationMs:   3_600_000, // le client prétend avoir joué une heure
		Events:       []Event{{At: 1000, Kind: "lines", Value: 1}},
		ClaimedScore: 100,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if v.Accepted {
		t.Fatal("une durée supérieure au temps réellement écoulé a été acceptée")
	}
	if !strings.Contains(v.Reason, "durée") {
		t.Fatalf("motif inattendu : %s", v.Reason)
	}
}

func TestDebitIrrealisteRejete(t *testing.T) {
	ctx := testContext()

	// 5000 événements en une seconde : un script, pas un joueur.
	events := make([]Event, 5000)
	for i := range events {
		events[i] = Event{At: int64(i % 1000), Kind: "drop"}
	}
	sub := Submission{DurationMs: 1000, Events: events, ClaimedScore: 5000}
	sign(ctx, &sub)

	if Verify(ctx, sub).Accepted {
		t.Fatal("un débit d'événements irréaliste a été accepté")
	}
}

func TestEvenementsDesordonnesRejetes(t *testing.T) {
	ctx := testContext()
	sub := Submission{
		DurationMs: 10_000,
		Events: []Event{
			{At: 5000, Kind: "lines", Value: 1},
			{At: 1000, Kind: "lines", Value: 1}, // remonte le temps
		},
		ClaimedScore: 200,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if v.Accepted {
		t.Fatal("des événements désordonnés ont été acceptés")
	}
	if !strings.Contains(v.Reason, "désordonnés") {
		t.Fatalf("motif inattendu : %s", v.Reason)
	}
}

func TestEvenementInventeRejete(t *testing.T) {
	ctx := testContext()
	sub := Submission{
		DurationMs:   10_000,
		Events:       []Event{{At: 1000, Kind: "jackpot", Value: 1}},
		ClaimedScore: 0,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if v.Accepted {
		t.Fatal("un type d'événement inconnu a été accepté")
	}
	if !strings.Contains(v.Reason, "inconnu") {
		t.Fatalf("motif inattendu : %s", v.Reason)
	}
}

// Une valeur énorme sur un événement proportionnel ne doit pas faire déborder
// l'accumulateur et produire un score négatif — donc « meilleur » qu'un autre
// selon un tri naïf.
func TestValeurEnormeRejetee(t *testing.T) {
	ctx := testContext()
	sub := Submission{
		DurationMs:   10_000,
		Events:       []Event{{At: 1000, Kind: "lines", Value: 1 << 40}},
		ClaimedScore: 0,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if v.Accepted {
		t.Fatal("une valeur d'événement hors bornes a été acceptée")
	}
	if v.Score < 0 {
		t.Fatalf("score négatif produit : %d", v.Score)
	}
}

func TestPartieTropCourteRejetee(t *testing.T) {
	ctx := testContext()
	sub := Submission{
		DurationMs:   50, // 50 ms pour un Tetris
		Events:       []Event{{At: 10, Kind: "tetris"}},
		ClaimedScore: 400,
	}
	sign(ctx, &sub)

	if Verify(ctx, sub).Accepted {
		t.Fatal("une partie de 50 ms a été acceptée")
	}
}

func TestPlafondDePlausibilite(t *testing.T) {
	ctx := testContext()
	ctx.MaxPlausibleScore = 500

	sub := Submission{
		DurationMs: 60_000,
		Events: []Event{
			{At: 1000, Kind: "tetris"},
			{At: 2000, Kind: "tetris"},
		},
		ClaimedScore: 800,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if v.Accepted {
		t.Fatal("un score au-delà du plafond du jeu a été accepté")
	}
	if v.Score != 800 {
		t.Fatalf("le score doit rester calculé (%d) même s'il est refusé", v.Score)
	}
}

// Le message d'erreur ne doit pas renvoyer au navigateur une chaîne choisie par
// le client : ce serait un XSS réfléchi si la page l'affichait sans échapper.
func TestMessageDErreurAssaini(t *testing.T) {
	ctx := testContext()
	sub := Submission{
		DurationMs:   10_000,
		Events:       []Event{{At: 1000, Kind: "<script>alert(1)</script>", Value: 1}},
		ClaimedScore: 0,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if v.Accepted {
		t.Fatal("événement inconnu accepté")
	}
	if strings.ContainsAny(v.Reason, "<>\"'") {
		t.Fatalf("le motif contient des caractères non assainis : %q", v.Reason)
	}
}

func TestJournalTropVolumineuxRejete(t *testing.T) {
	ctx := testContext()
	events := make([]Event, 300_000)
	sub := Submission{DurationMs: 600_000, Events: events}
	sign(ctx, &sub)

	if Verify(ctx, sub).Accepted {
		t.Fatal("un journal de 300 000 événements a été accepté")
	}
}

// Les règles doivent se résoudre pour les deux difficultés d'un même jeu.
func TestReglesParDifficulte(t *testing.T) {
	for _, slug := range []string{"tetris", "tetris-easy", "tetris-hard", "flappy-easy", "pacman"} {
		if _, ok := rulesFor(slug); !ok {
			t.Errorf("règles introuvables pour %q", slug)
		}
	}
	if _, ok := rulesFor("jeu-inexistant"); ok {
		t.Error("des règles ont été trouvées pour un jeu inexistant")
	}
}

// La sérialisation canonique doit être stable : c'est elle que le client C et le
// serveur Go doivent produire à l'identique, sinon aucune partie ne passe.
func TestPayloadCanoniqueStable(t *testing.T) {
	events := []Event{{At: 100, Kind: "lines", Value: 2}, {At: 250, Kind: "drop", Value: 0}}
	got := CanonicalPayload(42, 1000, 201, events)
	want := "v1|42|1000|201|2|100:lines:2|250:drop:0"
	if got != want {
		t.Fatalf("payload canonique modifié :\n  obtenu : %s\n  attendu : %s", got, want)
	}
}
