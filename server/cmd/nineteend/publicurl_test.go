package main

import (
	"io"
	"log/slog"
	"testing"
)

// TestURLPubliqueValide — ce que le serveur accepte d'ANNONCER au joueur.
//
// Ce que ce test protège : la page de téléchargement écrit la valeur sous le
// bouton, telle qu'on la tape dans `--server=`. Une adresse fausse y est PIRE
// que pas d'adresse — elle envoie tous les visiteurs de la page vers un serveur
// qui n'existe pas, et le seul symptôme côté jeu est un classement qui reste
// local, sans erreur. Le contrôle est donc au démarrage, une fois, plutôt que
// dans le navigateur de chacun.
//
// Le cas `https` mérite d'être nommé : ce n'est pas une préférence de style.
// Le client refuse explicitement `https://` — `ns_http_parse_url` échoue, et
// `tests/test_online.c` le cloue — parce que le traiter comme du HTTP en clair
// enverrait le jeton de session sur un port qui ne le comprend pas. Annoncer
// une adresse `https` donnerait donc à coup sûr au joueur une URL que son jeu
// ne sait pas ouvrir, et précisément sur le déploiement où l'aide compte le
// plus : celui qui est derrière un vrai proxy TLS.
func TestURLPubliqueValide(t *testing.T) {
	muet := slog.New(slog.NewTextHandler(io.Discard, nil))

	cas := []struct {
		nom    string
		entrée string
		veut   string
	}{
		{"vide : rien n'est annoncé", "", ""},
		{"des blancs seuls valent vide", "   ", ""},
		{"une adresse http simple passe", "http://localhost:8080", "http://localhost:8080"},
		{"un nom d'hôte public passe", "http://arcade.example:8080", "http://arcade.example:8080"},
		{"les blancs autour sont rognés", "  http://localhost:8080  ", "http://localhost:8080"},

		// La barre finale ne casse rien — le client la retire de son côté — mais
		// la page afficherait « …:8080/ » là où le journal du jeu répondra
		// « …:8080 ». Deux textes qui diffèrent sur la même chose font douter du
		// bon, alors on l'aligne ici.
		{"la barre oblique finale est retirée", "http://localhost:8080/", "http://localhost:8080"},
		{"un chemin est retiré : le jeu ajoute /api/v1 lui-même",
			"http://localhost:8080/api/v1", "http://localhost:8080"},

		// Les refus. Chacun rend "" : on n'annonce rien plutôt que d'annoncer
		// faux, et le journal dit pourquoi.
		{"https est refusé : le jeu ne sait pas l'ouvrir", "https://arcade.example", ""},
		{"un schéma inconnu est refusé", "ftp://arcade.example", ""},
		{"sans schéma, il n'y a pas d'hôte à joindre", "arcade.example:8080", ""},
		{"un chemin nu n'est pas une adresse", "/api/v1", ""},
		{"un schéma sans hôte est refusé", "http://", ""},
	}

	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			if got := urlPubliqueValide(c.entrée, muet); got != c.veut {
				t.Errorf("urlPubliqueValide(%q) = %q, attendu %q", c.entrée, got, c.veut)
			}
		})
	}
}

// TestURLPubliqueNAnnonceJamaisCeQuElleARefuse — la propriété, pas les cas.
//
// Tout ce qui ressort d'ici doit être une adresse que le JEU sait ouvrir :
// schéma `http`, un hôte, et rien derrière. Écrit comme une propriété parce que
// la liste de cas ci-dessus vieillira, et qu'un refus oublié se rattrape ici.
func TestURLPubliqueNAnnonceJamaisCeQuElleARefuse(t *testing.T) {
	muet := slog.New(slog.NewTextHandler(io.Discard, nil))

	entrées := []string{
		"", "   ", "http://localhost:8080", "http://localhost:8080/",
		"https://arcade.example", "ftp://x", "arcade.example", "/api",
		"http://", "http://x:8080/a/b?c=1#d", "://", "ht tp://x",
	}
	for _, e := range entrées {
		got := urlPubliqueValide(e, muet)
		if got == "" {
			continue // rien annoncé : toujours sûr
		}
		if len(got) < len("http://x") || got[:7] != "http://" {
			t.Errorf("urlPubliqueValide(%q) annonce %q, qui n'est pas en http://", e, got)
		}
		// Pas de suffixe : le jeu colle « /api/v1/… » derrière ce qu'on lui
		// donne, donc tout reliquat produirait un chemin doublé.
		for i := len("http://"); i < len(got); i++ {
			if got[i] == '/' || got[i] == '?' || got[i] == '#' {
				t.Errorf("urlPubliqueValide(%q) annonce %q, qui porte un suffixe", e, got)
				break
			}
		}
	}
}
