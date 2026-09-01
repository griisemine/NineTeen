package telechargements

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestClasserReconnaitLesPaquetsDuDepot(t *testing.T) {
	// Ces noms sont ceux que `packaging/` produit reellement, releves sur les
	// fichiers fabriques par la pile et par cpack. Ils ne sont pas inventes pour
	// le test.
	cas := []struct {
		nom        string
		plateforme string
		arch       string
	}{
		{"Nineteen-17.0.0-windows-x64.exe", "windows", "x86_64"},
		{"Nineteen-17.0.0-macOS-universal.dmg", "macos", "universel"},
		{"Nineteen-17.0.0-x86_64.AppImage", "linux", "x86_64"},
		{"Nineteen-17.0.0-aarch64.AppImage", "linux", "arm64"},
		{"nineteen_17.0.0_arm64.deb", "linux", "arm64"},
		{"nineteen_17.0.0_amd64.deb", "linux", "x86_64"},
		{"nineteen-17.0.0-linux-aarch64.tar.gz", "linux", "arm64"},

		// Ce qui n'est pas un paquet.
		{"LISEZ-MOI.txt", "", ""},
		{"SIGNATURE-linux.txt", "", ""},
		{"notes.md", "", ""},
	}

	for _, c := range cas {
		plateforme, arch, format := Classer(c.nom)
		if plateforme != c.plateforme {
			t.Errorf("%s : plateforme %q, attendu %q", c.nom, plateforme, c.plateforme)
		}
		if arch != c.arch {
			t.Errorf("%s : architecture %q, attendu %q", c.nom, arch, c.arch)
		}
		if c.plateforme != "" && format == "" {
			t.Errorf("%s : aucun format annonce", c.nom)
		}
	}
}

// LE NOM VIENT DU RESEAU, et il finit dans un chemin de fichier.
//
// Un `filepath.Join` seul ne protege pas : « ../../etc/passwd » se joint tres
// bien. Chacun de ces noms doit etre refuse AVANT toute jonction.
func TestUnNomDeFichierMalveillantEstRefuse(t *testing.T) {
	racine := t.TempDir()
	depot := Ouvrir(racine)

	// Un fichier reel, pour que le refus vienne du nom et non de l'absence.
	if err := os.WriteFile(filepath.Join(racine, "paquet.deb"), []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}

	mauvais := []string{
		"../../etc/passwd",
		"..",
		".",
		"/etc/passwd",
		"paquet.deb/../../etc/passwd",
		`..\..\windows\system32`,
		".cache",
		"paquet deb",
		"paquet;rm.deb",
		"",
		strings.Repeat("a", 200) + ".deb",
	}
	for _, nom := range mauvais {
		if _, ok := depot.Chemin(nom); ok {
			t.Errorf("le nom %q a ete accepte", nom)
		}
	}

	if _, ok := depot.Chemin("paquet.deb"); !ok {
		t.Error("un nom legitime a ete refuse : le controle est trop strict")
	}
}

func TestListeNAnnonceQueCeQuiEstLa(t *testing.T) {
	racine := t.TempDir()
	ecrire := func(nom, contenu string) {
		t.Helper()
		if err := os.WriteFile(filepath.Join(racine, nom), []byte(contenu), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	ecrire("Nineteen-17.0.0-windows-x64.exe", "w")
	ecrire("Nineteen-17.0.0-macOS-universal.dmg", "m")
	ecrire("Nineteen-17.0.0-aarch64.AppImage", "l")
	ecrire("nineteen_17.0.0_arm64.deb", "l")
	ecrire("SIGNATURE-linux.txt", "somme")
	ecrire("LISEZ-MOI.txt", "explication")
	if err := os.Mkdir(filepath.Join(racine, "_CPack_Packages"), 0o755); err != nil {
		t.Fatal(err)
	}

	depot := Ouvrir(racine)
	paquets, annexes := depot.Liste()

	if len(paquets) != 4 {
		t.Fatalf("%d paquets listes, 4 attendus : %v", len(paquets), paquets)
	}
	// L'ordre est Windows, macOS, Linux. Une page dont les boutons changent de
	// place d'un chargement a l'autre est une page qu'on ne peut pas apprendre.
	ordre := []string{"windows", "macos", "linux", "linux"}
	for i, veut := range ordre {
		if paquets[i].Plateforme != veut {
			t.Errorf("place %d : %s, attendu %s", i, paquets[i].Plateforme, veut)
		}
	}
	for _, p := range paquets {
		if !strings.HasPrefix(p.URL, Prefixe) {
			t.Errorf("%s : URL %q hors du prefixe de service", p.Nom, p.URL)
		}
		if p.Octets != 1 {
			t.Errorf("%s : %d octets annonces, 1 ecrit", p.Nom, p.Octets)
		}
	}

	// Le manifeste est une annexe, le LISEZ-MOI n'est rien du tout, et le
	// repertoire de travail de CPack n'apparait pas.
	if len(annexes) != 1 || annexes[0].Nom != "SIGNATURE-linux.txt" {
		t.Errorf("annexes attendues : SIGNATURE-linux.txt seule, obtenu %v", annexes)
	}
}

func TestUnDepotSansRepertoireNAnnonceRien(t *testing.T) {
	vide := Ouvrir("")
	if vide.Actif() {
		t.Error("un depot sans racine se declare actif")
	}
	if p, a := vide.Liste(); len(p) != 0 || len(a) != 0 {
		t.Errorf("un depot sans racine annonce %d paquets et %d annexes", len(p), len(a))
	}

	// Un repertoire configure mais absent est un depot VIDE, pas une panne : la
	// composition Docker fabrique les paquets en parallele du serveur, et le
	// site doit rester servi entre les deux.
	absent := Ouvrir(filepath.Join(t.TempDir(), "pas-encore"))
	if p, _ := absent.Liste(); len(p) != 0 {
		t.Errorf("un repertoire absent annonce %d paquets", len(p))
	}
}

// La somme n'est pas calculee dans le gestionnaire HTTP : hacher un .deb de
// 167 Mio a chaque affichage de la page ferait payer une demi-seconde par
// plateforme et par visiteur, pour une valeur qui ne change qu'a la
// fabrication.
func TestLaSommeArriveApresLeFichier(t *testing.T) {
	racine := t.TempDir()
	contenu := []byte("un paquet, pour de faux")
	if err := os.WriteFile(filepath.Join(racine, "n_1.0_amd64.deb"), contenu, 0o644); err != nil {
		t.Fatal(err)
	}

	depot := Ouvrir(racine)

	paquets, _ := depot.Liste()
	if len(paquets) != 1 {
		t.Fatalf("%d paquets, 1 attendu", len(paquets))
	}
	if paquets[0].SHA256 != "" {
		t.Error("la somme est annoncee avant d'avoir ete calculee")
	}

	depot.chauffeUnTour(nil)

	paquets, _ = depot.Liste()
	somme := sha256.Sum256(contenu)
	if got, want := paquets[0].SHA256, hex.EncodeToString(somme[:]); got != want {
		t.Errorf("somme %q, attendu %q", got, want)
	}

	// LE CONTENU CHANGE, LA SOMME AUSSI. Un paquet refabrique garde son nom :
	// servir l'ancienne somme serait pire que n'en servir aucune.
	nouveau := []byte("le meme paquet, refabrique")
	if err := os.WriteFile(filepath.Join(racine, "n_1.0_amd64.deb"), nouveau, 0o644); err != nil {
		t.Fatal(err)
	}
	paquets, _ = depot.Liste()
	if paquets[0].SHA256 != "" {
		t.Error("la somme de l'ancien contenu est encore annoncee")
	}
	depot.chauffeUnTour(nil)
	paquets, _ = depot.Liste()
	somme = sha256.Sum256(nouveau)
	if got, want := paquets[0].SHA256, hex.EncodeToString(somme[:]); got != want {
		t.Errorf("somme apres refabrication %q, attendu %q", got, want)
	}
}

func TestChaufferSArreteAvecLeContexte(t *testing.T) {
	depot := Ouvrir(t.TempDir())
	ctx, annuler := context.WithCancel(context.Background())
	fini := make(chan struct{})
	go func() {
		depot.Chauffer(ctx, nil, time.Hour)
		close(fini)
	}()
	annuler()
	select {
	case <-fini:
	case <-time.After(2 * time.Second):
		t.Fatal("Chauffer ne rend pas la main quand le contexte est annule")
	}
}

// LES NOMS DE LA RELEASE SUIVENT `packaging/`, ET C'EST UNE ERREUR PAYEE.
//
// Les trois noms batis par la page etaient faux tous les trois, et la garde
// « la release est-elle publiee » ne pouvait pas le voir : elle verifie que la
// release existe, pas que le fichier demande s'y trouve. Ce test lit
// `packaging/CPackNineteen.cmake` et compare.
func TestLesNomsDeReleaseSuiventPackaging(t *testing.T) {
	racine := racineDepot()
	if racine == "" {
		t.Skip("depot complet absent : packaging/ n'est pas lisible d'ici")
	}
	brut, err := os.ReadFile(filepath.Join(racine, "packaging", "CPackNineteen.cmake"))
	if err != nil {
		t.Fatalf("CPackNineteen.cmake illisible : %v", err)
	}
	cpack := string(brut)

	fichiers := SurGitHub("17.0.0")
	parPlateforme := map[string]string{}
	for _, f := range fichiers {
		parPlateforme[f.Plateforme] = f.Nom
	}

	// CPack ecrit le nom SANS extension. On la retire pour comparer.
	verifier := func(plateforme, modele string) {
		t.Helper()
		nom := parPlateforme[plateforme]
		if nom == "" {
			t.Errorf("aucun fichier %s dans la release", plateforme)
			return
		}
		attendu := strings.ReplaceAll(modele, "${CPACK_PACKAGE_VERSION}", "17.0.0")
		if !strings.HasPrefix(nom, attendu) {
			t.Errorf("la release annonce %q, CPackNineteen.cmake produit %q",
				nom, attendu)
		}
		if !strings.Contains(cpack, modele) {
			t.Errorf("le modele %q n'est plus dans CPackNineteen.cmake", modele)
		}
	}

	verifier("windows", "Nineteen-${CPACK_PACKAGE_VERSION}-windows-x64")
	verifier("macos", "Nineteen-${CPACK_PACKAGE_VERSION}-macOS-universal")

	// L'AppImage ne sort pas de CPack : `paquets.sh` la nomme d'apres `uname -m`,
	// comme le veut la convention AppImage. Sur le runner de release, c'est
	// x86_64.
	paquets, err := os.ReadFile(filepath.Join(racine, "packaging", "linux", "paquets.sh"))
	if err != nil {
		t.Fatalf("paquets.sh illisible : %v", err)
	}
	if !strings.Contains(string(paquets), `Nineteen-${VERSION}-${ARCH}.AppImage`) {
		t.Error("paquets.sh ne nomme plus l'AppImage comme la release l'annonce")
	}
	if nom := parPlateforme["linux"]; !strings.HasSuffix(nom, "-x86_64.AppImage") {
		t.Errorf("la release annonce %q pour Linux, attendu une AppImage x86_64", nom)
	}
}

// racineDepot remonte jusqu'au dossier qui porte CMakeLists.txt.
//
// Rend "" quand il n'y est pas : c'est le cas dans l'image Docker, dont le
// contexte de construction est `server/` seul. Le test concerne le dit et se
// saute, plutot que d'echouer sur une absence qui n'est pas un defaut.
func racineDepot() string {
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

// LA VERSION ANNONCEE DOIT ETRE CELLE DES PAQUETS, et le piege est le tri.
//
// « 17.9.0 » vient AVANT « 17.10.0 », ce qu'une comparaison de chaines rend
// faux. Le jour ou la dixieme version mineure sort, un depot qui se trompe
// annonce l'ancienne, et le jeu cesse silencieusement de se mettre a jour.
func TestLaVersionDuDepotEstCelleDuPaquetLePlusRecent(t *testing.T) {
	racine := t.TempDir()
	for _, nom := range []string{
		"Nineteen-17.9.0-aarch64.AppImage",
		"nineteen_17.10.0_arm64.deb",
		"Nineteen-17.2.0-macOS-universal.dmg",
		"SIGNATURE-linux.txt", // une annexe ne porte aucune version
	} {
		if err := os.WriteFile(filepath.Join(racine, nom), []byte("x"), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	d := Ouvrir(racine)
	if got := d.Version(); got != "17.10.0" {
		t.Fatalf("version du depot = %q, attendu 17.10.0", got)
	}
}

// Un depot sans paquet n'annonce rien plutot que d'inventer un numero : c'est
// le serveur qui garde alors le sien.
func TestUnDepotVideNAnnonceAucuneVersion(t *testing.T) {
	if got := Ouvrir(t.TempDir()).Version(); got != "" {
		t.Fatalf("version = %q, attendu vide", got)
	}
	if got := Ouvrir("").Version(); got != "" {
		t.Fatalf("version sans repertoire = %q, attendu vide", got)
	}
}
