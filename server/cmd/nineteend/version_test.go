package main

import (
	"os"
	"path/filepath"
	"regexp"
	"testing"
)

var motifVersionCMake = regexp.MustCompile(`(?m)^\s*VERSION\s+(\d+\.\d+\.\d+)\s*$`)

// TestVersionSuitCMake — la constante `version` contre `CMakeLists.txt`.
//
// Elle est restée à 15.0.0 alors que le projet était en 17.0.0. Le site lit
// `/api/v1/version` pour deux choses : afficher le numéro sur sa page de
// téléchargement, et CONSTRUIRE les liens vers les paquets de release GitHub
// (`Nineteen-15.0.0-macos-universal.dmg`). Le premier était faux, le second
// pointait vers des fichiers inexistants — et un lien mort ne se voit qu'en
// cliquant dessus.
//
// Le serveur ne peut pas lire `CMakeLists.txt` au démarrage : l'image Docker
// n'embarque que `server/`. La copie manuelle reste donc nécessaire ; ce test
// est ce qui la rend sûre.
func TestVersionSuitCMake(t *testing.T) {
	racine := racineDépôt(t)
	if racine == "" {
		t.Skip("dépôt complet absent (contexte server/ seul) : CMakeLists.txt n'est pas lisible d'ici")
	}

	brut, err := os.ReadFile(filepath.Join(racine, "CMakeLists.txt"))
	if err != nil {
		t.Fatalf("CMakeLists.txt illisible : %v", err)
	}
	m := motifVersionCMake.FindSubmatch(brut)
	if m == nil {
		t.Fatal("aucune ligne VERSION dans CMakeLists.txt : ce test ne contrôle plus rien")
	}

	if want := string(m[1]); version != want {
		t.Errorf("le serveur annonce la version %q, CMakeLists.txt dit %q — "+
			"mettre à jour la constante `version` dans main.go", version, want)
	}
}

func racineDépôt(t *testing.T) string {
	t.Helper()
	dir, err := os.Getwd()
	if err != nil {
		return ""
	}
	for i := 0; i < 8; i++ {
		if _, err := os.Stat(filepath.Join(dir, "CMakeLists.txt")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return ""
		}
		dir = parent
	}
	return ""
}
