package runs

import (
	"strings"
	"testing"
	"time"
)

// Contexte de référence : une partie d'Aplomb ouverte il y a une minute.
func testContext() Context {
	return Context{
		Secret:            []byte("secret-de-partie-32-octets-exact"),
		Seed:              1234567890,
		StartedAt:         time.Now().Add(-time.Minute),
		Now:               time.Now(),
		GameSlug:          "aplomb-hard",
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
			// « drop » est un geste : il ne rapporte rien, il est compté pour
			// la fréquence. « lines » porte les POINTS de la fournée, parce que
			// le barème de 2020 ne se déduit pas de leur nombre — voir la table.
			{At: 2000, Kind: "drop", Value: 0},
			{At: 4000, Kind: "lines", Value: 100},
			{At: 9000, Kind: "lines", Value: 300},
			{At: 15000, Kind: "lines", Value: 1500},
		},
		ClaimedScore: 1900,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if !v.Accepted {
		t.Fatalf("partie honnête rejetée : %s", v.Reason)
	}
	if v.Score != 1900 {
		t.Fatalf("score recalculé %d, attendu 1900", v.Score)
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
		DurationMs:   50, // 50 ms pour un Aplomb
		Events:       []Event{{At: 10, Kind: "lines"}},
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
			{At: 1000, Kind: "lines", Value: 400},
			{At: 2000, Kind: "lines", Value: 400},
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
	for _, slug := range []string{"aplomb", "aplomb-easy", "aplomb-hard", "flappy-easy", "dedale"} {
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

// La partie que le client produit RÉELLEMENT, avec le vocabulaire qu'il émet.
//
// C'est le test qui manquait des deux côtés. Le client émettait « flap » pour
// chaque battement d'aile et « death » à la fin ; la table ne connaissait ni
// l'un ni l'autre dans `points`, et un événement absent de `points` et de
// `scaled` est refusé SÈCHEMENT. Toute partie de Flappy soumise était donc
// rejetée en bloc — sans que personne le voie, l'envoi côté client étant « au
// mieux, jamais bloquant ».
//
// `tests/test_scores.c` tient le même invariant depuis le C. Les deux tables
// sont écrites dans deux langages ; ce sont ces deux tests qui les tiennent
// d'accord.
func TestVocabulaireDuClientAccepte(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "flappy"

	// Une minute de jeu, trois tuyaux franchis, des battements entre les deux.
	sub := Submission{
		DurationMs: 12_000,
		Events: []Event{
			{At: 500, Kind: "flap", Value: 0},
			{At: 1200, Kind: "flap", Value: 0},
			{At: 2000, Kind: "pipe", Value: 1},
			{At: 2600, Kind: "flap", Value: 0},
			{At: 4000, Kind: "pipe", Value: 1},
			{At: 5100, Kind: "flap", Value: 0},
			{At: 6000, Kind: "pipe", Value: 1},
			{At: 6400, Kind: "death", Value: 0},
		},
		ClaimedScore: 3,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if !v.Accepted {
		t.Fatalf("la partie que le client produit est rejetée : %s", v.Reason)
	}
	if v.Score != 3 {
		t.Fatalf("score recalculé %d, attendu 3", v.Score)
	}
}

// Les événements muets ne doivent RIEN rapporter : les accepter ne doit pas
// ouvrir une voie pour gonfler un score en battant des ailes.
func TestEvenementsMuetsNeRapportentRien(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "flappy"

	sub := Submission{
		DurationMs: 10_000,
		Events: []Event{
			{At: 100, Kind: "flap", Value: 9999},
			{At: 200, Kind: "flap", Value: 9999},
			{At: 300, Kind: "death", Value: 9999},
		},
		ClaimedScore: 0,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if !v.Accepted {
		t.Fatalf("une partie sans point devrait rester valide : %s", v.Reason)
	}
	if v.Score != 0 {
		t.Fatalf("les événements muets ont rapporté %d points", v.Score)
	}
}

// Et la limite de fréquence continue de s'appliquer à un événement muet : c'est
// tout ce à quoi il sert.
func TestEvenementMuetResteLimiteEnFrequence(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "flappy"

	// 12 battements par seconde au maximum : on en met 200 en 10 s.
	sub := Submission{DurationMs: 10_000, ClaimedScore: 0}
	for i := 0; i < 200; i++ {
		sub.Events = append(sub.Events, Event{At: int64(i * 50), Kind: "flap"})
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if v.Accepted {
		t.Fatal("200 battements en 10 s auraient dû être refusés")
	}
	if !strings.Contains(v.Reason, "flap") {
		t.Fatalf("la raison devrait nommer l'événement : %s", v.Reason)
	}
}

// Un fruit géant de Snake vaut dix fois sa valeur : jusqu'à 100 000 pour le
// muffin rose. La borne écrite en dur était à 10 000, donc la partie était
// refusée pour « valeur hors bornes » — sur un fruit parfaitement légitime.
func TestFruitGeantAccepte(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "snake"
	ctx.MaxPlausibleScore = 999_999

	sub := Submission{
		DurationMs: 20_000,
		Events: []Event{
			{At: 3000, Kind: "fruit", Value: 20},      // une fraise
			{At: 9000, Kind: "fruit", Value: 100_000}, // un muffin rose géant
			{At: 15000, Kind: "death"},
		},
		ClaimedScore: 100_020,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if !v.Accepted {
		t.Fatalf("un fruit géant est rejeté : %s", v.Reason)
	}
	if v.Score != 100_020 {
		t.Fatalf("score recalculé %d, attendu 100020", v.Score)
	}
}

// En hardcore, manger un fruit COÛTE cinq fois sa valeur et le score vient des
// fruits qu'on laisse expirer. Le total passe donc par des valeurs négatives,
// ce que le garde-fou refusait EN COURS DE ROUTE — rendant le mode
// insoumettable avant même d'exister.
func TestHardcoreNegatifTransitoireAccepte(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "snake-hard"

	sub := Submission{
		DurationMs: 20_000,
		Events: []Event{
			{At: 2000, Kind: "fruit", Value: -100}, // mangé : ça coûte
			{At: 5000, Kind: "fruit", Value: -250},
			{At: 9000, Kind: "fruit", Value: 900}, // expiré : ça rapporte
			{At: 14000, Kind: "fruit", Value: 700},
			{At: 18000, Kind: "death"},
		},
		ClaimedScore: 1250,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if !v.Accepted {
		t.Fatalf("une partie hardcore honnête est rejetée : %s", v.Reason)
	}
	if v.Score != 1250 {
		t.Fatalf("score recalculé %d, attendu 1250", v.Score)
	}
}

// Et une partie qui finit dans le rouge vaut zéro, pas un score négatif : on ne
// doit rien à la salle en sortant.
func TestScoreFinalNegatifRameneAZero(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "snake-hard"

	sub := Submission{
		DurationMs: 10_000,
		Events: []Event{
			{At: 2000, Kind: "fruit", Value: -500},
			{At: 5000, Kind: "death"},
		},
		ClaimedScore: 0,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if !v.Accepted {
		t.Fatalf("partie rejetée : %s", v.Reason)
	}
	if v.Score != 0 {
		t.Fatalf("score recalculé %d, attendu 0", v.Score)
	}
}

// Les bornes restent des bornes : au-delà, c'est toujours refusé, et les jeux
// qui n'en déclarent pas gardent celles d'avant.
func TestValeurAuDelaDesBornesToujoursRefusee(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "snake"
	sub := Submission{
		DurationMs:   10_000,
		Events:       []Event{{At: 1000, Kind: "fruit", Value: 100_001}},
		ClaimedScore: 100_001,
	}
	sign(ctx, &sub)
	if v := Verify(ctx, sub); v.Accepted {
		t.Fatal("une valeur au-delà de la borne de Snake aurait dû être refusée")
	}

	// Aplomb n'en déclare pas : il garde 0 à 10 000.
	ctx2 := testContext()
	sub2 := Submission{
		DurationMs:   10_000,
		Events:       []Event{{At: 1000, Kind: "lines", Value: 10_001}},
		ClaimedScore: 1_000_100,
	}
	sign(ctx2, &sub2)
	if v := Verify(ctx2, sub2); v.Accepted {
		t.Fatal("les bornes par défaut ont changé pour un jeu qui n'en déclare pas")
	}
}

// Le Démineur ouvre une CASCADE : un seul appui dévoile jusqu'à trois cents
// cases. Le barème est donc proportionnel, et c'est le seul jeu où un
// événement porte une quantité que le joueur ne choisit pas.
//
// Compter les événements plutôt que les cases aurait classé à 5 points une
// partie affichée à 1 500 — sans que rien, côté client, ne le laisse voir.
func TestCascadeDuDemineurCompteSesCases(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "demineur"

	sub := Submission{
		DurationMs: 20_000,
		Events: []Event{
			{At: 500, Kind: "move"},
			{At: 900, Kind: "cell", Value: 42}, // une grande plage : 210 points
			{At: 3000, Kind: "move"},
			{At: 3400, Kind: "cell", Value: 1}, // une case seule : 5 points
			{At: 6000, Kind: "flag"},           // 2 points
			{At: 9000, Kind: "death"},
		},
		ClaimedScore: 217,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if !v.Accepted {
		t.Fatalf("une cascade légitime est rejetée : %s", v.Reason)
	}
	if v.Score != 42*5+1*5+2 {
		t.Fatalf("score recalculé %d, attendu %d", v.Score, 42*5+1*5+2)
	}
}

// Gagner vaut 500 points, une fois — et la limite de fréquence de « win »
// (0,2/s) interdit d'en empiler.
func TestVictoireDuDemineur(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "demineur"

	sub := Submission{
		DurationMs: 60_000,
		Events: []Event{
			{At: 1000, Kind: "cell", Value: 300},
			{At: 2000, Kind: "win"},
			{At: 2100, Kind: "death"},
		},
		ClaimedScore: 2000,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if !v.Accepted {
		t.Fatalf("une partie gagnée est rejetée : %s", v.Reason)
	}
	if v.Score != 300*5+500 {
		t.Fatalf("score recalculé %d, attendu %d", v.Score, 300*5+500)
	}
}

// Et la cascade reste bornée : la grille ne fait que 400 cases, donc une valeur
// au-delà de la borne par défaut (10 000) est une invention.
func TestCascadeImpossibleRefusee(t *testing.T) {
	ctx := testContext()
	ctx.GameSlug = "demineur"
	ctx.MaxPlausibleScore = 9_999_999

	sub := Submission{
		DurationMs: 20_000,
		Events: []Event{
			{At: 900, Kind: "cell", Value: 500_000},
			{At: 1000, Kind: "death"},
		},
		ClaimedScore: 2_500_000,
	}
	sign(ctx, &sub)

	v := Verify(ctx, sub)
	if v.Accepted {
		t.Fatal("une cascade de 500 000 cases sur une grille de 400 aurait dû être refusée")
	}
	if !strings.Contains(v.Reason, "cell") {
		t.Fatalf("la raison devrait nommer l'événement : %s", v.Reason)
	}
}
