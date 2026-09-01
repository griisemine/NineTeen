// site_test.go — le contrôle qui empêche le site de pourrir.
//
// # POURQUOI CE FICHIER EXISTE
//
// Le site a annoncé « Quinze bornes » et « 64 sources lumineuses » pendant deux
// versions entières, alors que la scène en déclarait dix-neuf et vingt-six, et
// il servait une page « Version 15.0.0 » dont les liens de téléchargement
// pointaient vers des paquets qui n'ont jamais existé. Aucun de ces défauts
// n'était visible : la page s'affichait parfaitement, elle disait simplement
// autre chose que la vérité.
//
// Rien ne peut empêcher quelqu'un d'écrire un mauvais chiffre. Ce qui est
// possible, c'est de faire échouer la construction quand il l'écrit. Trois
// familles de contrôles :
//
//  1. TOUT FICHIER RÉFÉRENCÉ EXISTE. Une image ou une vidéo renommée dans
//     `tools/site-media.py` sans être renommée dans la page donne un 404 que
//     personne ne voit — le reste de la page s'affiche.
//
//  2. LES CHIFFRES DE LA PAGE SUIVENT LE MANIFESTE. Chaque nombre écrit en dur
//     dans `index.html` porte un `data-chiffre` ; le manifeste, écrit par le
//     script de capture, dit ce que le média contient réellement.
//
//  3. LE MANIFESTE SUIT LA SCÈNE. Et la scène est la source. Ce dernier maillon
//     a besoin du dépôt complet, que l'image Docker n'embarque pas ; il est donc
//     le seul à se laisser sauter, et il le dit quand il le fait.
package web

import (
	"encoding/json"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

/* ========================================================================== */
/* Lecture du site embarqué                                                   */
/* ========================================================================== */

func lireAsset(t *testing.T, nom string) []byte {
	t.Helper()
	données, err := fs.ReadFile(FS, "assets/"+nom)
	if err != nil {
		t.Fatalf("asset introuvable dans le binaire : %s (%v)", nom, err)
	}
	return données
}

func index(t *testing.T) string {
	t.Helper()
	return string(lireAsset(t, "index.html"))
}

// LES CINQ PAGES, et pas seulement l'accueil.
//
// Les controles de ce fichier ne lisaient qu'`index.html`, du temps ou le site
// tenait sur une page et demie. Il en a cinq : accueil, Couperet, classement,
// telechargement, compte. Une image sans alt ou une ressource tierce sur l'une
// des quatre autres passait donc sans etre vue.
//
// La liste est LUE dans le systeme de fichiers embarque et non ecrite ici : une
// sixieme page tombe sous les memes controles le jour ou elle apparait, sans
// que personne ait a y penser.
func pages(t *testing.T) map[string]string {
	t.Helper()
	entrees, err := fs.ReadDir(FS, "assets")
	if err != nil {
		t.Fatalf("site embarque illisible : %v", err)
	}
	out := map[string]string{}
	for _, e := range entrees {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".html") {
			continue
		}
		out[e.Name()] = string(lireAsset(t, e.Name()))
	}
	if len(out) == 0 {
		t.Fatal("aucune page HTML dans le site embarque")
	}
	return out
}

// Les scripts du site, meme lecture et meme raison.
func scripts(t *testing.T) map[string]string {
	t.Helper()
	entrees, err := fs.ReadDir(FS, "assets")
	if err != nil {
		t.Fatalf("site embarque illisible : %v", err)
	}
	out := map[string]string{}
	for _, e := range entrees {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".js") {
			continue
		}
		out[e.Name()] = string(lireAsset(t, e.Name()))
	}
	if len(out) == 0 {
		t.Fatal("aucun script dans le site embarque")
	}
	return out
}

/* ========================================================================== */
/* 1. Tout fichier référencé existe                                           */
/* ========================================================================== */

// Les attributs qui désignent une ressource locale. `poster` et `<source src>`
// comptent autant que `src` : ce sont eux qui portent la vidéo et son affiche,
// c'est-à-dire précisément ce que cette version ajoute.
var motifRessource = regexp.MustCompile(`(?:src|href|poster)="(/[^"]+)"`)

func TestToutFichierReferenceExiste(t *testing.T) {
	// Les chemins servis par le serveur mais qui ne sont pas des fichiers du
	// site : ils sont produits par l'API, pas par `assets/`.
	dynamiques := map[string]bool{"/": true}

	trouvés, manquants := 0, 0
	for nom, page := range pages(t) {
		for _, m := range motifRessource.FindAllStringSubmatch(page, -1) {
			url := m[1]
			if dynamiques[url] || strings.HasPrefix(url, "/api/") ||
				strings.HasPrefix(url, "/telechargements/") {
				continue
			}
			// Une ancre n'est pas une ressource.
			if i := strings.IndexByte(url, '#'); i >= 0 {
				url = url[:i]
			}
			if url == "" || url == "/" {
				continue
			}
			trouvés++
			if _, err := fs.Stat(FS, "assets"+url); err != nil {
				manquants++
				t.Errorf("%s référence %s, qui n'est pas dans le site embarqué", nom, url)
			}
		}
	}
	if trouvés == 0 {
		t.Fatal("aucune ressource locale trouvée : le motif de détection ne " +
			"reconnaît plus les pages, donc ce test ne contrôle plus rien")
	}
	t.Logf("%d ressources locales référencées, %d absente(s)", trouvés, manquants)
}

// Le manifeste nomme le média que le script a produit. Le site le lit pour
// bâtir ses galeries : un fichier qu'il annonce et qui manque donne une vignette
// vide, sans erreur.
func TestManifesteNeNommeQueDesFichiersPresents(t *testing.T) {
	m := manifeste(t)

	vérifier := func(champ, url string) {
		t.Helper()
		if url == "" {
			t.Errorf("manifeste : %s est vide", champ)
			return
		}
		if _, err := fs.Stat(FS, "assets"+url); err != nil {
			t.Errorf("manifeste : %s nomme %s, absent du site embarqué", champ, url)
		}
	}

	vérifier("salle.mp4", m.Salle.MP4)
	vérifier("salle.webm", m.Salle.WebM)
	vérifier("salle.affiche_avif", m.Salle.AfficheAVIF)
	vérifier("salle.affiche_jpg", m.Salle.AfficheJPG)

	for _, j := range m.Jeux {
		vérifier("jeu "+j.ID+" mp4", j.MP4)
		vérifier("jeu "+j.ID+" webm", j.WebM)
		vérifier("jeu "+j.ID+" affiche avif", j.AfficheAVIF)
		vérifier("jeu "+j.ID+" affiche jpg", j.AfficheJPG)
		if j.Alt == "" {
			t.Errorf("jeu %s : pas de texte alternatif", j.ID)
		}
	}
	for _, b := range m.Bornes {
		vérifier("borne "+b.Nom, b.Image)
		if b.Alt == "" {
			t.Errorf("borne %s : pas de texte alternatif", b.Nom)
		}
	}
	for _, v := range m.Vues {
		vérifier("vue "+v.Nom, v.Image)
	}
}

/* ========================================================================== */
/* Accessibilité : pas d'image muette                                         */
/* ========================================================================== */

var motifImg = regexp.MustCompile(`(?s)<img\s[^>]*>`)
var motifAlt = regexp.MustCompile(`\salt="([^"]*)"`)

// Une image sans `alt` est annoncée par son nom de fichier aux lecteurs
// d'écran — « vue tiret allee point jpg ». Un `alt` vide serait légitime pour
// une image décorative, mais aucune de celles-ci ne l'est : elles SONT le
// contenu de la page.
func TestChaqueImageAUnTexteAlternatif(t *testing.T) {
	for nom, page := range pages(t) {
		for _, balise := range motifImg.FindAllString(page, -1) {
			m := motifAlt.FindStringSubmatch(balise)
			if m == nil {
				t.Errorf("%s : image sans attribut alt : %s", nom, résumé(balise))
				continue
			}
			if strings.TrimSpace(m[1]) == "" {
				t.Errorf("%s : image à alt vide : %s", nom, résumé(balise))
			}
		}
	}
}

func résumé(s string) string {
	s = strings.Join(strings.Fields(s), " ")
	if len(s) > 110 {
		return s[:110] + "…"
	}
	return s
}

/* ========================================================================== */
/* Aucune ressource tierce                                                    */
/* ========================================================================== */

var motifExterne = regexp.MustCompile(`(?:src|href)="(https?:)?//([^"]+)"`)

// Le dépôt a déjà dû retirer treize images tierces de son paquet, et la
// politique de contenu du serveur interdit toute origine extérieure. Un lien
// vers un CDN ou une fonte Google ne casserait donc pas la page : il serait
// silencieusement bloqué, et le site s'afficherait sans sa fonte. Les liens de
// TEXTE vers GitHub restent permis — ils ne chargent rien.
func TestAucuneRessourceTierce(t *testing.T) {
	for nom, page := range pages(t) {
		for _, m := range motifExterne.FindAllStringSubmatch(page, -1) {
			hôte := m[2]
			// `href` sur une balise `<a>` est un lien, pas un chargement. On ne
			// retient que ce qui est chargé par la page.
			balise := contexteBalise(page, m[0])
			if strings.HasPrefix(balise, "<a ") {
				continue
			}
			t.Errorf("%s : ressource tierce chargée : %s (dans %s)", nom, hôte, résumé(balise))
		}
	}
}

// contexteBalise remonte au « < » qui ouvre la balise contenant l'occurrence.
func contexteBalise(page, occurrence string) string {
	i := strings.Index(page, occurrence)
	if i < 0 {
		return occurrence
	}
	début := strings.LastIndexByte(page[:i], '<')
	if début < 0 {
		return occurrence
	}
	fin := strings.IndexByte(page[début:], '>')
	if fin < 0 {
		return page[début:]
	}
	return page[début : début+fin+1]
}

/* ========================================================================== */
/* Le manifeste                                                               */
/* ========================================================================== */

type manifesteMédia struct {
	Version string `json:"version"`
	Salle   struct {
		MP4         string `json:"mp4"`
		WebM        string `json:"webm"`
		AfficheAVIF string `json:"affiche_avif"`
		AfficheJPG  string `json:"affiche_jpg"`
	} `json:"salle"`
	Jeux []struct {
		ID          string `json:"id"`
		Nom         string `json:"nom"`
		MP4         string `json:"mp4"`
		WebM        string `json:"webm"`
		AfficheAVIF string `json:"affiche_avif"`
		AfficheJPG  string `json:"affiche_jpg"`
		Alt         string `json:"alt"`
	} `json:"jeux"`
	Bornes []struct {
		Slot  int    `json:"slot"`
		Nom   string `json:"nom"`
		Jeu   string `json:"jeu"`
		Image string `json:"image"`
		Alt   string `json:"alt"`
	} `json:"bornes"`
	Vues []struct {
		Nom   string `json:"nom"`
		Image string `json:"image"`
	} `json:"vues"`
	Compte struct {
		Bornes      int `json:"bornes"`
		Jeux        int `json:"jeux"`
		Luminaires  int `json:"luminaires"`
		VuesNommées int `json:"vues_nommees"`
		Props       int `json:"props"`
	} `json:"compte"`
}

func manifeste(t *testing.T) manifesteMédia {
	t.Helper()
	var m manifesteMédia
	if err := json.Unmarshal(lireAsset(t, "media/manifeste.json"), &m); err != nil {
		t.Fatalf("manifeste illisible : %v", err)
	}
	return m
}

/* ========================================================================== */
/* 2. Les chiffres de la page suivent le manifeste                            */
/* ========================================================================== */

var motifChiffre = regexp.MustCompile(`data-chiffre="([a-z]+)"[^>]*>([^<]+)<`)

// Chaque nombre écrit en dur dans la page porte un `data-chiffre` qui dit de
// quoi il parle. Ce test confronte chacun à ce que le média contient
// réellement — et le média, lui, est produit depuis la scène.
//
// Le même `data-chiffre` peut apparaître plusieurs fois (le nombre de bornes est
// à la fois dans le texte d'accroche et dans le bandeau de chiffres) : toutes
// les occurrences sont vérifiées, ce qui attrape aussi le cas où l'une est mise
// à jour et l'autre oubliée.
func TestChiffresDeLaPageSuiventLeManifeste(t *testing.T) {
	m := manifeste(t)

	attendu := map[string]string{
		"bornes":     strconv.Itoa(m.Compte.Bornes),
		"jeux":       strconv.Itoa(m.Compte.Jeux),
		"luminaires": strconv.Itoa(m.Compte.Luminaires),
		"vues":       strconv.Itoa(m.Compte.VuesNommées),
		"props":      strconv.Itoa(m.Compte.Props),
	}

	// EXIGES SUR L'ACCUEIL, parce qu'ils sont l'identite du site. Les autres
	// sont facultatifs : « luminaires » et « vues nommees » disaient l'etat d'un
	// moteur et non ce qu'on vient jouer, et la page les a laisses tomber. Ce
	// test ne demande donc plus qu'ils soient affiches, il demande que ceux qui
	// LE SONT soient justes, ce qui est la seule chose qu'il peut promettre.
	obligatoires := []string{"bornes", "jeux"}

	vus := map[string]int{}
	for nom, page := range pages(t) {
		for _, occ := range motifChiffre.FindAllStringSubmatch(page, -1) {
			clé, valeur := occ[1], strings.TrimSpace(occ[2])
			veut, connu := attendu[clé]
			if !connu {
				t.Errorf(`%s : data-chiffre="%s" n'a pas de source : ajouter la clé `+
					`dans ce test, ou retirer l'attribut`, nom, clé)
				continue
			}
			vus[clé]++
			if valeur != veut {
				t.Errorf(`%s annonce %s = %q, la source dit %q`, nom, clé, valeur, veut)
			}
		}
	}

	// Le contrôle du contrôle : si la page cessait de porter ces attributs, ce
	// test passerait en ne vérifiant rien du tout.
	for _, clé := range obligatoires {
		if vus[clé] == 0 {
			t.Errorf(`aucun data-chiffre="%s" dans le site : le chiffre n'est plus contrôlé`, clé)
		}
	}
}

// LE NUMERO DE VERSION ECRIT EN DUR DANS CHAQUE PAGE.
//
// Les cinq pages portent « Version 17.0.0 » dans leur pied, sous un
// `data-version` que le script remplace par ce que rend /api/v1/version. Ce
// texte-la n'est donc vu que pendant le chargement, ou quand le script ne
// s'execute pas, et c'est exactement le genre de valeur qui se demode sans que
// personne s'en apercoive.
//
// Il doit valoir la version du manifeste, qui suit elle-meme CMake par
// TestVersionDuMediaSuitCMake. La chaine tient donc de CMakeLists.txt au pied
// de page.
func TestLaVersionEcriteDansLesPagesSuitLaSource(t *testing.T) {
	m := manifeste(t)
	motif := regexp.MustCompile(`data-version[^>]*>([^<]*)<`)

	vus := 0
	for nom, page := range pages(t) {
		occurrences := motif.FindAllStringSubmatch(page, -1)
		if len(occurrences) == 0 {
			t.Errorf("%s ne porte aucun data-version", nom)
			continue
		}
		for _, occ := range occurrences {
			vus++
			if got := strings.TrimSpace(occ[1]); got != m.Version {
				t.Errorf("%s écrit la version %q, la source dit %q", nom, got, m.Version)
			}
		}
	}
	if vus == 0 {
		t.Fatal("aucun data-version dans le site : ce test ne contrôle plus rien")
	}
	t.Logf("%d mentions de version vérifiées", vus)
}

// Le manifeste doit décrire autant d'entrées qu'il en compte. Un décompte qui
// ne correspond pas à la liste veut dire que le script de capture s'est arrêté
// en chemin, et la page afficherait alors moins de bornes qu'elle n'en annonce.
func TestManifesteEstCoherentAvecLuiMeme(t *testing.T) {
	m := manifeste(t)
	if len(m.Bornes) != m.Compte.Bornes {
		t.Errorf("manifeste : %d bornes décrites pour %d comptées", len(m.Bornes), m.Compte.Bornes)
	}
	if len(m.Jeux) != m.Compte.Jeux {
		t.Errorf("manifeste : %d jeux décrits pour %d comptés", len(m.Jeux), m.Compte.Jeux)
	}
	if len(m.Vues) == 0 {
		t.Error("manifeste : aucune vue de la salle")
	}
}

/* ========================================================================== */
/* 3. Le manifeste suit la scène — et la version suit CMake                   */
/* ========================================================================== */

// racineDépôt remonte jusqu'au dossier qui porte `CMakeLists.txt`.
//
// Renvoie "" quand il n'y est pas : c'est le cas dans l'image Docker, dont le
// contexte de construction est `server/` seul. Les tests concernés le disent et
// se sautent, plutôt que d'échouer sur une absence qui n'est pas un défaut.
func racineDépôt() string {
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

type scène struct {
	Cabinets []struct {
		Name string `json:"name"`
		Game string `json:"game"`
	} `json:"cabinets"`
	Lights   []json.RawMessage `json:"lights"`
	Captures []json.RawMessage `json:"captures"`
}

func TestManifesteSuitLaScene(t *testing.T) {
	racine := racineDépôt()
	if racine == "" {
		t.Skip("dépôt complet absent (contexte server/ seul) : la scène n'est pas lisible d'ici")
	}

	brut, err := os.ReadFile(filepath.Join(racine, "assets", "scene", "salle.room.json"))
	if err != nil {
		t.Fatalf("scène illisible : %v", err)
	}
	var s scène
	if err := json.Unmarshal(brut, &s); err != nil {
		t.Fatalf("scène illisible : %v", err)
	}

	m := manifeste(t)

	if len(s.Cabinets) != m.Compte.Bornes {
		t.Errorf("la scène déclare %d bornes, le média en annonce %d — "+
			"régénérer avec `python3 tools/site-media.py`",
			len(s.Cabinets), m.Compte.Bornes)
	}
	if len(s.Lights) != m.Compte.Luminaires {
		t.Errorf("la scène déclare %d luminaires, le média en annonce %d",
			len(s.Lights), m.Compte.Luminaires)
	}
	if len(s.Captures) != m.Compte.VuesNommées {
		t.Errorf("la scène déclare %d points de vue, le média en annonce %d",
			len(s.Captures), m.Compte.VuesNommées)
	}

	// Les jeux : ceux que les bornes portent, moins le classement, qui est une
	// borne mais pas un jeu.
	jeux := map[string]bool{}
	for _, c := range s.Cabinets {
		if c.Game != "" && c.Game != "leaderboard" {
			jeux[c.Game] = true
		}
	}
	if len(jeux) != m.Compte.Jeux {
		t.Errorf("la scène porte %d jeux distincts, le média en annonce %d",
			len(jeux), m.Compte.Jeux)
	}

	// Chaque borne de la scène doit avoir sa photo, nommée par son emplacement.
	parNom := map[string]bool{}
	for _, b := range m.Bornes {
		parNom[b.Nom] = true
	}
	for _, c := range s.Cabinets {
		if !parNom[c.Name] {
			t.Errorf("la borne %q de la scène n'a pas de photo dans le média", c.Name)
		}
	}
}

// La page décrit la RÉPARTITION des bornes en toutes lettres : « Six jeux
// occupent une paire de bornes face à face […] Dédale et Piano en occupent trois
// chacun. » C'est une phrase, donc rien ne la relie à la scène — et c'est
// exactement la forme qu'avait « Quinze bornes ».
//
// Ce test ne lit pas la phrase : il vérifie que le FAIT qu'elle énonce tient
// toujours. S'il change, il échoue en disant quoi réécrire.
func TestRepartitionDesBornesEstCelleQueLaPageDecrit(t *testing.T) {
	racine := racineDépôt()
	if racine == "" {
		t.Skip("dépôt complet absent : la scène n'est pas lisible d'ici")
	}
	brut, err := os.ReadFile(filepath.Join(racine, "assets", "scene", "salle.room.json"))
	if err != nil {
		t.Fatalf("scène illisible : %v", err)
	}
	var s scène
	if err := json.Unmarshal(brut, &s); err != nil {
		t.Fatalf("scène illisible : %v", err)
	}

	parJeu := map[string]int{}
	for _, c := range s.Cabinets {
		parJeu[c.Game]++
	}

	paires, triplets := 0, 0
	for jeu, n := range parJeu {
		if jeu == "leaderboard" {
			if n != 1 {
				t.Errorf("le classement occupe %d bornes ; la page en annonce une seule", n)
			}
			continue
		}
		switch n {
		case 2:
			paires++
		case 3:
			triplets++
		default:
			t.Errorf("le jeu %q occupe %d bornes : ni une paire ni un triplet, "+
				"la phrase de la section « Les bornes » ne le décrit plus", jeu, n)
		}
	}

	if paires != 6 || triplets != 2 {
		t.Errorf("répartition : %d jeux en paire et %d en triplet ; la page annonce "+
			"6 et 2 — corriger la phrase de la section « Les bornes » d'index.html",
			paires, triplets)
	}
	if parJeu["dedale"] != 3 || parJeu["piano"] != 3 {
		t.Errorf("la page nomme Dédale et Piano comme les deux jeux à trois bornes ; "+
			"la scène en donne %d et %d", parJeu["dedale"], parJeu["piano"])
	}
}

var motifVersionCMake = regexp.MustCompile(`(?m)^\s*VERSION\s+(\d+\.\d+\.\d+)\s*$`)

// La version que le média annonce est celle de `CMakeLists.txt`. C'est le
// contrôle qui manquait quand le serveur est resté à 15.0.0 pendant deux
// versions ; `server/cmd/nineteend` porte le même, sur sa propre constante.
func TestVersionDuMediaSuitCMake(t *testing.T) {
	racine := racineDépôt()
	if racine == "" {
		t.Skip("dépôt complet absent : CMakeLists.txt n'est pas lisible d'ici")
	}
	brut, err := os.ReadFile(filepath.Join(racine, "CMakeLists.txt"))
	if err != nil {
		t.Fatalf("CMakeLists.txt illisible : %v", err)
	}
	m := motifVersionCMake.FindSubmatch(brut)
	if m == nil {
		t.Fatal("aucune ligne VERSION dans CMakeLists.txt")
	}
	if got, want := manifeste(t).Version, string(m[1]); got != want {
		t.Errorf("le média annonce la version %q, CMakeLists.txt dit %q — "+
			"régénérer avec `python3 tools/site-media.py`", got, want)
	}
}

/* ========================================================================== */
/* 4. Les cinq pages, et la ponctuation qu'on leur demande                    */
/* ========================================================================== */

// Le site a cinq pages et une seule barre de navigation. Chacune doit la
// porter, et se designer elle-meme par `aria-current`, sinon un visiteur ne sait
// plus ou il est.
//
// Ce test attrape le defaut le plus banal d'un site a plusieurs pages : une
// page ajoutee sans etre mise dans le menu des autres, ou un lien de menu
// oublie sur une page. Rien ne se voit, on ne peut simplement plus revenir.
func TestChaquePagePorteLaMemeNavigation(t *testing.T) {
	liens := []string{"/", "/couperet.html", "/classement.html", "/telecharger.html"}
	toutes := pages(t)

	// Les pages que le menu designe, par leur nom de fichier. La racine est
	// index.html : c'est le serveur de fichiers qui fait la correspondance, et
	// elle n'est ecrite qu'ici.
	cibles := map[string]bool{"index.html": true}
	for _, l := range liens {
		if l != "/" {
			cibles[strings.TrimPrefix(l, "/")] = true
		}
	}

	for nom, page := range toutes {
		for _, l := range liens {
			if !strings.Contains(page, `href="`+l+`"`) {
				t.Errorf("%s ne mène pas à %s", nom, l)
			}
		}
		if !strings.Contains(page, `id="compte-barre"`) {
			t.Errorf("%s n'a pas la barre de compte : on ne peut pas s'y connecter", nom)
		}
		if !strings.Contains(page, `src="/nineteen.js"`) {
			t.Errorf("%s ne charge pas le socle commun", nom)
		}
		// La page courante se marque, et une seule fois. Seules les pages du
		// MENU le font : compte.html se rejoint par la barre de compte, en haut
		// a droite, qui n'est pas le menu et n'a pas d'entree a souligner.
		attendu := 0
		if _, dansLeMenu := cibles[nom]; dansLeMenu {
			attendu = 1
		}
		if n := strings.Count(page, `aria-current="page"`); n != attendu {
			t.Errorf("%s porte %d fois aria-current=\"page\", il en faut %d", nom, n, attendu)
		}
	}

	// Le contrôle du contrôle : les quatre liens du menu doivent correspondre à
	// des pages qui existent, sauf la racine qui est index.html.
	for _, l := range liens {
		if l == "/" {
			continue
		}
		if _, ok := toutes[strings.TrimPrefix(l, "/")]; !ok {
			t.Errorf("le menu mène à %s, qui n'est pas une page du site", l)
		}
	}
}

// LA POLITIQUE DE CONTENU INTERDIT LE STYLE ET LE SCRIPT EN LIGNE.
//
// `securityHeaders` pose `style-src 'self'` et `script-src 'self'`, sans
// `'unsafe-inline'`. Un attribut `style=` ou un `<script>` sans `src` ne
// produirait donc aucune erreur visible : il serait silencieusement bloque, et
// la page s'afficherait de travers sur le seul deploiement qui compte, celui du
// public. C'est le genre de defaut qu'on ne voit jamais en developpement.
func TestAucunStyleNiScriptEnLigne(t *testing.T) {
	scriptEnLigne := regexp.MustCompile(`<script(?:\s[^>]*)?>`)
	for nom, page := range pages(t) {
		if strings.Contains(page, "style=\"") {
			t.Errorf("%s porte un attribut style en ligne, que la politique de "+
				"contenu du serveur bloque", nom)
		}
		if strings.Contains(page, "<style") {
			t.Errorf("%s porte une balise <style>, que la politique de contenu bloque", nom)
		}
		for _, balise := range scriptEnLigne.FindAllString(page, -1) {
			if !strings.Contains(balise, "src=") {
				t.Errorf("%s porte un script en ligne (%s), que la politique de "+
					"contenu bloque", nom, balise)
			}
		}
	}
}

// LA PONCTUATION DEMANDEE PAR LE PROPRIETAIRE DU DEPOT.
//
// Ni point-virgule, ni tiret long, dans le texte du site. La demande est de
// forme et non de fond, et elle se tient : ce sont les deux signes qui font
// reconnaitre un texte ecrit par une machine, et un site de jeu qui a l'air
// ecrit par une machine se croit moins.
//
// La regle est verifiable parce que les pages ont ete ecrites pour l'etre : pas
// une seule entite HTML (`&nbsp;` en porte un), pas un attribut `style`, pas de
// script en ligne. Tout point-virgule dans un .html est donc du texte, ou un
// attribut, et aucun des deux n'a de raison d'en porter.
//
// Cote script, seuls les LITTERAUX DE CHAINE sont regardes : le code en est
// forcement plein. La seule exception admise est le separateur `";"` de
// `document.cookie`, qui n'est pas du texte affiche.
func TestLaPonctuationDuSiteEstCelleDemandee(t *testing.T) {
	interdits := map[rune]string{
		';': "point-virgule",
		'—': "tiret cadratin",
		'–': "tiret demi-cadratin",
	}

	for nom, page := range pages(t) {
		for i, r := range page {
			if quoi, mauvais := interdits[r]; mauvais {
				t.Errorf("%s:%d porte un %s : %s", nom, ligneDe(page, i), quoi,
					résumé(extrait(page, i)))
			}
		}
	}

	for nom, js := range scripts(t) {
		for i, r := range js {
			if r == '—' || r == '–' {
				t.Errorf("%s:%d porte un tiret long : %s", nom, ligneDe(js, i),
					résumé(extrait(js, i)))
			}
		}
		for _, ch := range chaînesJS(js) {
			// Le séparateur de `document.cookie.split(";")`. Un caractère
			// seul n'est jamais une phrase.
			if ch.texte == ";" {
				continue
			}
			if strings.ContainsRune(ch.texte, ';') {
				t.Errorf("%s:%d : chaîne avec point-virgule : %q", nom, ch.ligne, ch.texte)
			}
		}
	}
}

// LE SITE PARLE DU JEU, PAS DE SA REFONTE.
//
// Il portait une section « Le chantier » qui comparait le rendu, l'eclairage,
// la physique et la securite « avant » et « apres », et un renvoi vers l'audit
// du code de 2020. C'est l'histoire du depot, et le proprietaire l'a dit sans
// detour : ce n'est pas ce que vient lire quelqu'un qui veut jouer.
//
// La liste ci-dessous est courte et litterale a dessein. Elle n'essaie pas de
// juger un texte, elle rappelle une decision au moment ou l'on s'appreterait a
// la defaire sans y penser.
func TestLeSiteParleDuJeuEtPasDeSaRefonte(t *testing.T) {
	proscrits := []string{
		"Le chantier",
		"L'audit",
		"SECURITY_AUDIT",
		"reconstruit",
		"refonte",
		"code d'origine",
		"site d'origine",
	}
	for nom, page := range pages(t) {
		for _, mot := range proscrits {
			if strings.Contains(page, mot) {
				t.Errorf("%s parle de la refonte du projet (%q) : le site est celui "+
					"du jeu", nom, mot)
			}
		}
	}
}

// LE SITE NE SUPPOSE PAS SON PROPRE HOTE.
//
// Toutes les requetes partent en relatif, avec `credentials: "same-origin"`. Un
// chemin absolu code en dur marcherait sur la machine de developpement et
// echouerait derriere un nom de domaine, ce qui est le pire moment pour
// l'apprendre.
func TestLeSiteNeSupposePasSonPropreHote(t *testing.T) {
	motif := regexp.MustCompile(`["'` + "`" + `]https?://[^"'` + "`" + `]+`)
	for nom, js := range scripts(t) {
		for _, occ := range motif.FindAllString(js, -1) {
			t.Errorf("%s contient une URL absolue en dur : %s", nom, occ)
		}
		if strings.Contains(js, "window.location.host") ||
			strings.Contains(js, "window.location.origin") {
			t.Errorf("%s bâtit une adresse depuis l'origine de la page : le jeu ne "+
				"sait pas ouvrir une URL https, et le serveur est le seul à connaître "+
				"son adresse joignable", nom)
		}
	}
}

// LA PAGE DE TELECHARGEMENT NE BATIT AUCUN LIEN.
//
// C'est le defaut qu'elle a porte pendant deux versions : elle assemblait trois
// URL de release GitHub a partir du seul numero de version, sans jamais
// verifier que la release existe ni que les fichiers s'y trouvent. Mesure a
// l'epoque contre l'API GitHub : le depot repondait 200, la release de la
// version repondait 404, et les trois boutons de la page qui existe pour
// telecharger etaient trois 404. Les noms de fichier etaient faux tous les
// trois par-dessus le marche, ce que la garde « la release est-elle publiee »
// ne pouvait pas voir.
//
// Le serveur tranche maintenant, une fois, en Go, ou cela se teste. La page
// affiche ce qu'il rend et n'invente aucune adresse.
func TestLaPageDeTelechargementNeBatitAucunLien(t *testing.T) {
	js := string(lireAsset(t, "telecharger.js"))

	if !strings.Contains(js, "/api/v1/telechargements") {
		t.Error("telecharger.js ne demande pas au serveur ce qu'il a sous la main")
	}
	if strings.Contains(js, "releases/download") || strings.Contains(js, "github.com") {
		t.Error("telecharger.js assemble une URL de release : c'est le serveur qui " +
			"décide, parce que lui seul sait ce qui existe")
	}
	if !strings.Contains(js, "f.url") {
		t.Error("telecharger.js n'utilise pas l'URL rendue par le serveur")
	}

	html := string(lireAsset(t, "telecharger.html"))
	if strings.Contains(html, "data-dl") {
		t.Error("telecharger.html porte encore des boutons écrits à la main")
	}
	if !strings.Contains(html, `id="liste-telechargements"`) {
		t.Error("telecharger.html n'a plus le conteneur que le script remplit")
	}
}

/* -------------------------------------------------------------------------- */
/* Outillage des deux tests ci-dessus                                         */
/* -------------------------------------------------------------------------- */

func ligneDe(source string, octet int) int {
	return strings.Count(source[:octet], "\n") + 1
}

// extrait rend le voisinage d'un octet, pour que le message dise OU chercher.
func extrait(source string, octet int) string {
	début := octet - 40
	if début < 0 {
		début = 0
	}
	fin := octet + 40
	if fin > len(source) {
		fin = len(source)
	}
	return source[début:fin]
}

type chaîneJS struct {
	ligne int
	texte string
}

// chaînesJS rend les littéraux de chaîne d'un source JavaScript.
//
// C'EST UN LECTEUR, PAS UN ANALYSEUR. Il suit les guillemets simples, doubles et
// obliques, saute les commentaires de ligne et de bloc, et honore
// l'échappement. Il ne comprend ni les expressions régulières littérales ni
// l'interpolation, et il n'en a pas besoin : ce qu'on lui demande est de
// répondre « ce point-virgule est-il dans une chaîne », et les quatre scripts du
// site n'écrivent une barre oblique que dans un chemin, toujours entre
// guillemets.
//
// Un analyseur complet serait plus juste et beaucoup plus long, pour une
// question à laquelle celui-ci répond déjà sur ce corpus. S'il se met à mentir,
// il mentira bruyamment : un littéral mal fermé fait diverger tout le reste du
// fichier, donc le test échouera plutôt que de laisser passer.
func chaînesJS(src string) []chaîneJS {
	var out []chaîneJS
	r := []rune(src)
	ligne := 1
	for i := 0; i < len(r); i++ {
		c := r[i]
		switch {
		case c == '\n':
			ligne++
		case c == '/' && i+1 < len(r) && r[i+1] == '/':
			for i < len(r) && r[i] != '\n' {
				i++
			}
			ligne++
		case c == '/' && i+1 < len(r) && r[i+1] == '*':
			i += 2
			for i+1 < len(r) && !(r[i] == '*' && r[i+1] == '/') {
				if r[i] == '\n' {
					ligne++
				}
				i++
			}
			i++
		case c == '"' || c == '\'' || c == '`':
			départ := ligne
			var b strings.Builder
			i++
			for i < len(r) && r[i] != c {
				if r[i] == '\\' {
					i += 2
					continue
				}
				if r[i] == '\n' {
					ligne++
				}
				b.WriteRune(r[i])
				i++
			}
			out = append(out, chaîneJS{ligne: départ, texte: b.String()})
		}
	}
	return out
}
