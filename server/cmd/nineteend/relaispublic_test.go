package main

import (
	"bytes"
	"io"
	"log/slog"
	"strings"
	"testing"
)

// TestAdresseRelaisValide — ce que le serveur accepte d'ANNONCER aux joueurs
// d'un salon.
//
// C'est le pendant de `TestURLPubliqueValide` pour l'autre port, et l'enjeu y
// est plus grand. Une URL publique fausse laisse le jeu tourner en local ; une
// adresse de relais fausse arrive APRÈS qu'un joueur a obtenu un code, réuni
// sept autres personnes et distribué les places. Le mode échouerait donc au
// dernier moment, et pour tout le monde à la fois.
//
// Les deux refus qui méritent d'être nommés :
//
//   - UN SCHÉMA. Le relais n'est pas du HTTP : c'est un protocole binaire sur
//     TCP (voir l'en-tête de internal/duel/relay.go). « http://x:8081 » n'a
//     aucun sens ici, et le recopier tel quel donnerait un nom d'hôte absurde.
//
//   - UNE ADRESSE D'ÉCOUTE. « :8081 » et « 0.0.0.0:8081 » sont ce qu'on écrit
//     dans `-duel-addr` ; elles disent « toutes les interfaces », ce qui ne
//     désigne aucune machine vue du joueur. Les annoncer enverrait chaque
//     client se connecter à lui-même.
func TestAdresseRelaisValide(t *testing.T) {
	muet := slog.New(slog.NewTextHandler(io.Discard, nil))

	cas := []struct {
		nom      string
		entrée   string
		hôte     string
		port     int
		annoncée bool
	}{
		{"vide : rien n'est annoncé", "", "", 0, false},
		{"des blancs seuls valent vide", "   ", "", 0, false},

		{"un nom d'hôte et un port", "arcade.example:8081", "arcade.example", 8081, true},
		{"les blancs autour sont rognés", "  arcade.example:8081  ", "arcade.example", 8081, true},
		{"une adresse IPv4", "203.0.113.7:8081", "203.0.113.7", 8081, true},
		{"une adresse IPv6 entre crochets", "[2001:db8::1]:8081", "2001:db8::1", 8081, true},
		{"la boucle locale, pour le développement", "127.0.0.1:8081", "127.0.0.1", 8081, true},

		// Les refus. Chacun rend le zéro : on n'annonce rien plutôt que faux, et
		// le journal dit pourquoi.
		{"un schéma http", "http://arcade.example:8081", "", 0, false},
		{"un schéma tcp", "tcp://arcade.example:8081", "", 0, false},
		{"sans port", "arcade.example", "", 0, false},
		{"un port vide", "arcade.example:", "", 0, false},
		{"un port qui n'est pas un nombre", "arcade.example:huit", "", 0, false},
		{"le port zéro", "arcade.example:0", "", 0, false},
		{"un port hors bornes", "arcade.example:70000", "", 0, false},
		{"toutes les interfaces, sans hôte", ":8081", "", 0, false},
		{"toutes les interfaces, en IPv4", "0.0.0.0:8081", "", 0, false},
		{"toutes les interfaces, en IPv6", "[::]:8081", "", 0, false},
	}

	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			got := adresseRelaisValide(c.entrée, muet)
			if got.Annonce() != c.annoncée {
				t.Fatalf("adresseRelaisValide(%q).Annonce() = %v, attendu %v",
					c.entrée, got.Annonce(), c.annoncée)
			}
			if got.Hote != c.hôte || got.Port != c.port {
				t.Errorf("adresseRelaisValide(%q) = %s:%d, attendu %s:%d",
					c.entrée, got.Hote, got.Port, c.hôte, c.port)
			}
		})
	}
}

// TestLeRelaisAnnonceNEstJamaisUneAdresseDEcoute — la propriété, pas les cas.
//
// Écrite comme une propriété parce que la liste ci-dessus vieillira : ce qui
// doit rester vrai, c'est que rien de ce qui sort d'ici ne puisse désigner
// « toutes les interfaces », ni porter un schéma, ni un port impossible.
func TestLeRelaisAnnonceNEstJamaisUneAdresseDEcoute(t *testing.T) {
	muet := slog.New(slog.NewTextHandler(io.Discard, nil))

	entrées := []string{
		"", "   ", ":8081", "0.0.0.0:8081", "[::]:8081", "*:8081",
		"http://x:8081", "arcade.example", "arcade.example:0",
		"arcade.example:65536", "arcade.example:-1", "arcade.example:8081",
		"[2001:db8::1]:8081", "x:y:z", "::8081",
	}
	for _, e := range entrées {
		got := adresseRelaisValide(e, muet)
		if !got.Annonce() {
			continue // rien annoncé : toujours sûr
		}
		switch got.Hote {
		case "", "0.0.0.0", "::", "[::]", "*":
			t.Errorf("adresseRelaisValide(%q) annonce l'hôte %q, qui ne désigne "+
				"aucune machine", e, got.Hote)
		}
		if strings.Contains(got.Hote, "/") {
			t.Errorf("adresseRelaisValide(%q) annonce l'hôte %q, qui porte un schéma", e, got.Hote)
		}
		if got.Port < 1 || got.Port > 65535 {
			t.Errorf("adresseRelaisValide(%q) annonce le port %d", e, got.Port)
		}
	}
}

// TestLeJournalDuRelaisDitLaquelleDesRaisons — le message est le livrable.
//
// Trois branches de refus rendent la MÊME valeur — le zéro — donc un test qui
// ne regarderait que le retour survivrait à la suppression de deux d'entre
// elles. Ce que chacune apporte en propre est son message, et c'est lui qui
// dira à l'exploitant qu'il a recopié `-duel-addr` au lieu de l'adresse
// publique — la faute la plus facile à faire des trois.
func TestLeJournalDuRelaisDitLaquelleDesRaisons(t *testing.T) {
	journalDe := func(entrée string) string {
		var tampon bytes.Buffer
		adresseRelaisValide(entrée, slog.New(slog.NewTextHandler(&tampon, nil)))
		return tampon.String()
	}

	if j := journalDe("http://arcade.example:8081"); !strings.Contains(j, "schema") {
		t.Errorf("un schéma n'est pas signalé comme tel :\n%s", j)
	}
	if j := journalDe("0.0.0.0:8081"); !strings.Contains(j, "adresse d'ecoute") {
		t.Errorf("une adresse d'écoute n'est pas reconnue pour ce qu'elle est — "+
			"c'est pourtant la faute la plus facile à faire :\n%s", j)
	}
	if j := journalDe("arcade.example:huit"); !strings.Contains(j, "port") {
		t.Errorf("un port invalide n'est pas nommé :\n%s", j)
	}

	// Et le cas qui marche le DIT : sans cette ligne, on ne sait pas si la
	// variable a été prise en compte.
	if j := journalDe("arcade.example:8081"); !strings.Contains(j, "relais annonce") {
		t.Errorf("une adresse acceptée ne se voit pas dans le journal :\n%s", j)
	}
}
