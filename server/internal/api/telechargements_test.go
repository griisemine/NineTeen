// telechargements_test.go — la route qui donne le jeu, verifiee sans base.
//
// Rien de ce qui suit ne touche PostgreSQL : le depot est un repertoire, la
// version une constante, et la decision « locale, github ou aucune » se prend
// entre les deux. C'est justement pourquoi elle a ete deplacee du navigateur
// vers le serveur, ou elle se teste.
package api

import (
	"encoding/json"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"nineteen/internal/telechargements"
)

func serveurAvecDepot(racine string, publiee bool) *Server {
	return New(Config{
		Logger:  slog.New(slog.NewTextHandler(io.Discard, nil)),
		Version: "17.0.0",
		Publiee: publiee,
		Depot:   telechargements.Ouvrir(racine),
	})
}

func lireJSON(t *testing.T, rec *httptest.ResponseRecorder) map[string]any {
	t.Helper()
	var out map[string]any
	if err := json.Unmarshal(rec.Body.Bytes(), &out); err != nil {
		t.Fatalf("reponse illisible : %v (%s)", err, rec.Body.String())
	}
	return out
}

// LES TROIS ETATS, et le fait qu'aucun d'eux ne soit decide par la page.
func TestLaSourceDesPaquetsEstDecideeParLeServeur(t *testing.T) {
	racine := t.TempDir()
	if err := os.WriteFile(filepath.Join(racine, "nineteen_17.0.0_arm64.deb"),
		[]byte("paquet"), 0o644); err != nil {
		t.Fatal(err)
	}

	// 1. Un repertoire garni : la source est locale, et les URL sont les
	//    notres.
	rec := appel(t, serveurAvecDepot(racine, false), "GET", "/api/v1/telechargements")
	if rec.Code != http.StatusOK {
		t.Fatalf("code %d", rec.Code)
	}
	corps := lireJSON(t, rec)
	if corps["source"] != "locale" {
		t.Errorf("source %q, attendu « locale »", corps["source"])
	}
	fichiers, _ := corps["fichiers"].([]any)
	if len(fichiers) != 1 {
		t.Fatalf("%d fichiers, 1 attendu", len(fichiers))
	}
	premier, _ := fichiers[0].(map[string]any)
	if url, _ := premier["url"].(string); !strings.HasPrefix(url, "/telechargements/") {
		t.Errorf("URL %q : un paquet local doit etre servi par ce serveur", url)
	}

	// 2. Rien en local, mais une release declaree publiee : on donne GitHub.
	rec = appel(t, serveurAvecDepot(t.TempDir(), true), "GET", "/api/v1/telechargements")
	corps = lireJSON(t, rec)
	if corps["source"] != "github" {
		t.Errorf("source %q, attendu « github »", corps["source"])
	}
	fichiers, _ = corps["fichiers"].([]any)
	if len(fichiers) != 3 {
		t.Errorf("%d fichiers annonces, 3 attendus pour une release", len(fichiers))
	}

	// 3. Ni l'un ni l'autre : on ne montre AUCUN bouton. Un bouton mort est
	//    pire qu'un bouton absent, il fait douter du reste de la page.
	rec = appel(t, serveurAvecDepot("", false), "GET", "/api/v1/telechargements")
	corps = lireJSON(t, rec)
	if corps["source"] != "aucune" {
		t.Errorf("source %q, attendu « aucune »", corps["source"])
	}
	fichiers, _ = corps["fichiers"].([]any)
	if len(fichiers) != 0 {
		t.Errorf("%d fichiers annonces alors qu'il n'y en a aucun", len(fichiers))
	}
	// Jamais `null` : une page qui doit distinguer `null` de `[]` avant de
	// compter est une page ou l'on finit par oublier le cas.
	if _, ok := corps["fichiers"].([]any); !ok {
		t.Error("« fichiers » n'est pas un tableau quand il est vide")
	}
	if _, ok := corps["manifestes"].([]any); !ok {
		t.Error("« manifestes » n'est pas un tableau quand il est vide")
	}
}

func TestUnPaquetSeTelechargeEtLeReste404(t *testing.T) {
	racine := t.TempDir()
	contenu := "le contenu du paquet"
	if err := os.WriteFile(filepath.Join(racine, "nineteen_17.0.0_arm64.deb"),
		[]byte(contenu), 0o644); err != nil {
		t.Fatal(err)
	}
	// Un fichier present mais qui n'est pas un paquet reste servable : le
	// manifeste de signature en est un, et la page y renvoie.
	if err := os.WriteFile(filepath.Join(racine, "SIGNATURE-linux.txt"),
		[]byte("sommes"), 0o644); err != nil {
		t.Fatal(err)
	}

	s := serveurAvecDepot(racine, false)

	rec := appel(t, s, "GET", "/telechargements/nineteen_17.0.0_arm64.deb")
	if rec.Code != http.StatusOK {
		t.Fatalf("code %d, 200 attendu", rec.Code)
	}
	if rec.Body.String() != contenu {
		t.Errorf("contenu servi %q", rec.Body.String())
	}
	// `attachment` et pas `inline` : un .deb n'a rien a faire dans un onglet.
	if cd := rec.Header().Get("Content-Disposition"); !strings.HasPrefix(cd, "attachment") {
		t.Errorf("Content-Disposition %q", cd)
	}

	rec = appel(t, s, "GET", "/telechargements/SIGNATURE-linux.txt")
	if rec.Code != http.StatusOK {
		t.Errorf("le manifeste de signature rend %d", rec.Code)
	}

	// Ce qui n'est pas la, et ce qui essaie de sortir du repertoire.
	for _, chemin := range []string{
		"/telechargements/absent.deb",
		"/telechargements/..",
		"/telechargements/.cache",
	} {
		rec = appel(t, s, "GET", chemin)
		if rec.Code == http.StatusOK {
			t.Errorf("%s a ete servi", chemin)
		}
	}
}

// La route de liste et la route de fichier sont PUBLIQUES : telecharger le jeu
// ne demande pas de compte, et en demander un serait prendre le probleme a
// l'envers. On s'inscrit pour jouer en ligne, pas pour obtenir le binaire.
func TestLeTelechargementNeDemandeAucunCompte(t *testing.T) {
	racine := t.TempDir()
	if err := os.WriteFile(filepath.Join(racine, "Nineteen-17.0.0-macOS-universal.dmg"),
		[]byte("image"), 0o644); err != nil {
		t.Fatal(err)
	}
	s := serveurAvecDepot(racine, false)

	// Aucun cookie de session n'est pose sur ces requetes.
	if rec := appel(t, s, "GET", "/api/v1/telechargements"); rec.Code != http.StatusOK {
		t.Errorf("la liste demande une session : code %d", rec.Code)
	}
	if rec := appel(t, s, "GET", "/telechargements/Nineteen-17.0.0-macOS-universal.dmg"); rec.Code != http.StatusOK {
		t.Errorf("le fichier demande une session : code %d", rec.Code)
	}
}

// Un serveur qui n'heberge aucun paquet ne doit pas rendre 500 sur la route de
// fichier : elle doit simplement ne rien trouver.
func TestSansDepotLaRouteDeFichierRend404(t *testing.T) {
	s := serveurAvecDepot("", false)
	if rec := appel(t, s, "GET", "/telechargements/quoi.deb"); rec.Code != http.StatusNotFound {
		t.Errorf("code %d, 404 attendu", rec.Code)
	}
}
