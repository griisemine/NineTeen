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

/* ========================================================================== */
/* 1. Tout fichier référencé existe                                           */
/* ========================================================================== */

// Les attributs qui désignent une ressource locale. `poster` et `<source src>`
// comptent autant que `src` : ce sont eux qui portent la vidéo et son affiche,
// c'est-à-dire précisément ce que cette version ajoute.
var motifRessource = regexp.MustCompile(`(?:src|href|poster)="(/[^"]+)"`)

func TestToutFichierReferenceExiste(t *testing.T) {
	page := index(t)

	// Les chemins servis par le serveur mais qui ne sont pas des fichiers du
	// site : ils sont produits par l'API, pas par `assets/`.
	dynamiques := map[string]bool{"/": true}

	trouvés, manquants := 0, 0
	for _, m := range motifRessource.FindAllStringSubmatch(page, -1) {
		url := m[1]
		if dynamiques[url] || strings.HasPrefix(url, "/api/") {
			continue
		}
		// Une ancre (« #audit ») n'est pas une ressource.
		if i := strings.IndexByte(url, '#'); i >= 0 {
			url = url[:i]
		}
		if url == "" || url == "/" {
			continue
		}
		trouvés++
		if _, err := fs.Stat(FS, "assets"+url); err != nil {
			manquants++
			t.Errorf("index.html référence %s, qui n'est pas dans le site embarqué", url)
		}
	}
	if trouvés == 0 {
		t.Fatal("aucune ressource locale trouvée dans index.html : le motif de " +
			"détection ne reconnaît plus la page, donc ce test ne contrôle plus rien")
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
	for _, balise := range motifImg.FindAllString(index(t), -1) {
		m := motifAlt.FindStringSubmatch(balise)
		if m == nil {
			t.Errorf("image sans attribut alt : %s", résumé(balise))
			continue
		}
		if strings.TrimSpace(m[1]) == "" {
			t.Errorf("image à alt vide : %s", résumé(balise))
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
	page := index(t)
	for _, m := range motifExterne.FindAllStringSubmatch(page, -1) {
		hôte := m[2]
		// `href` sur une balise `<a>` est un lien, pas un chargement. On ne
		// retient que ce qui est chargé par la page.
		balise := contexteBalise(page, m[0])
		if strings.HasPrefix(balise, "<a ") {
			continue
		}
		t.Errorf("ressource tierce chargée par la page : %s (dans %s)", hôte, résumé(balise))
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
		"version":    m.Version,
	}

	vus := map[string]int{}
	for _, occ := range motifChiffre.FindAllStringSubmatch(index(t), -1) {
		clé, valeur := occ[1], strings.TrimSpace(occ[2])
		veut, connu := attendu[clé]
		if !connu {
			t.Errorf(`data-chiffre="%s" n'a pas de source : ajouter la clé dans ce `+
				`test, ou retirer l'attribut`, clé)
			continue
		}
		vus[clé]++
		if valeur != veut {
			t.Errorf(`la page annonce %s = %q ; la source dit %q`, clé, valeur, veut)
		}
	}

	// Le contrôle du contrôle : si la page cessait de porter ces attributs, ce
	// test passerait en ne vérifiant rien du tout.
	for clé := range attendu {
		if vus[clé] == 0 {
			t.Errorf(`aucun data-chiffre="%s" dans index.html : le chiffre n'est plus contrôlé`, clé)
		}
	}
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

// La page ne doit pas offrir un téléchargement qui n'existe pas.
//
// Elle le faisait : `app.js` bâtissait trois liens vers
// `github.com/griisemine/NineTeen/releases/download/v<version>/…` à partir du
// seul numéro de version, en supposant que la release existe. Mesuré contre
// l'API GitHub le jour où ce test a été écrit : le dépôt répond 200,
// `releases/tags/v17.0.0` répond 404, et la liste des releases est vide. Les
// trois boutons « Télécharger » — ce pour quoi la page existe — étaient trois
// 404, et un bouton mort fait douter du reste de la page.
//
// Le test ne va PAS interroger GitHub : un test qui dépend du réseau échoue
// pour des raisons qui ne le regardent pas. Il vérifie la seule chose qui soit
// à nous — que la page demande au serveur si la version est publiée, au lieu
// de le supposer, et qu'elle a de quoi le dire quand la réponse est non.
func TestLaPageNAffirmePasQuUnPaquetExiste(t *testing.T) {
	js := string(lireAsset(t, "app.js"))
	if !strings.Contains(js, "data.publiee") {
		t.Error("app.js ne lit pas « publiee » : les liens de téléchargement " +
			"sont donc bâtis sans savoir si la release existe")
	}
	// Les deux moitiés de la règle. « ne jamais poser de href » passerait la
	// première seule, et le téléchargement cesserait d'exister.
	if !strings.Contains(js, "el.removeAttribute(\"href\")") {
		t.Error("app.js ne retire pas le href quand la version n'est pas publiée")
	}
	if !strings.Contains(js, "el.href = ") {
		t.Error("app.js ne pose jamais de href : plus personne ne peut télécharger")
	}
	if !strings.Contains(js, "aria-disabled") {
		t.Error("un bouton qui n'est plus un lien doit le dire à un lecteur d'écran")
	}

	html := string(lireAsset(t, "index.html"))
	if !strings.Contains(html, `id="note-attente"`) {
		t.Error("index.html n'a pas la note qui explique l'attente")
	}
	if !strings.Contains(html, "hidden") {
		t.Error("la note d'attente doit être masquée par défaut : c'est app.js " +
			"qui la découvre, une fois la réponse du serveur connue")
	}
}

/* ========================================================================== */
/* 4. Le site ne suppose pas où il est servi                                  */
/* ========================================================================== */

// TestLeSiteNeSupposePasSonPropreHote — la page doit fonctionner ailleurs que
// sur `localhost`.
//
// Le site est servi depuis un `embed.FS` : le même fichier part sur la machine
// du développeur, dans le conteneur, et derrière le proxy TLS d'un vrai
// déploiement. Une adresse d'API écrite en dur y serait juste pour un seul des
// trois, et le symptôme sur les deux autres est une page qui s'affiche
// parfaitement avec un classement vide et des erreurs en console.
//
// La règle est donc : `app.js` appelle l'API en RELATIF, et rien d'absolu ne
// désigne un hôte à nous. Ce qui reste permis, ce sont les liens vers GitHub —
// ce sont des liens, pas des appels d'API, et ils pointent vers un service qui
// n'est pas celui-ci.
//
// Mesuré au moment d'écrire ce test : `app.js` ne contenait déjà aucun
// `localhost`, et son helper `api(path)` passe le chemin tel quel à `fetch`
// avec `credentials: "same-origin"`. Le test cloue cet état plutôt que de le
// supposer conservé.
func TestLeSiteNeSupposePasSonPropreHote(t *testing.T) {
	js := string(lireAsset(t, "app.js"))
	html := string(lireAsset(t, "index.html"))

	for _, interdit := range []string{"localhost", "127.0.0.1", "0.0.0.0", "://[::1]"} {
		if strings.Contains(js, interdit) {
			t.Errorf("app.js contient %q : le site ne marcherait que là", interdit)
		}
		if strings.Contains(html, interdit) {
			t.Errorf("index.html contient %q : le site ne marcherait que là", interdit)
		}
	}

	// Les appels d'API partent en relatif. Le helper est le seul point de
	// passage, et il reçoit `path` sans préfixe.
	if !strings.Contains(js, `const response = await fetch(path, {`) {
		t.Error("app.js n'appelle plus l'API par son helper relatif : vérifier " +
			"qu'aucune origine n'a été écrite en dur")
	}
	if !strings.Contains(js, `credentials: "same-origin"`) {
		t.Error("app.js n'envoie plus les cookies en same-origin : la session " +
			"cesserait de suivre dès que l'origine change")
	}

	// Aucun `api("http…")` : le seul argument admis est un chemin.
	if motif := regexp.MustCompile("api\\(\\s*[\"'`]\\s*https?://"); motif.MatchString(js) {
		t.Error("app.js appelle l'API par une URL absolue")
	}
}

// TestLaPageDonneLAdresseAuJoueur — la page dit quoi taper dans `--server=`,
// et elle le tient du SERVEUR.
//
// Ce qui manquait : la page livrait un binaire et aucune adresse. Le joueur
// téléchargeait le jeu et devait deviner l'URL — alors que le serveur, lui, la
// connaît. `NINETEEN_PUBLIC_URL` la lui donne, `/api/v1/version` la publie, et
// la page l'écrit telle qu'on la tape.
//
// Le piège que ce test interdit : bâtir la ligne à partir de `window.location`.
// Les deux coïncident en développement et divergent dès qu'un proxy TLS est
// devant — la page est alors jointe en `https://`, que le client du jeu refuse
// explicitement d'ouvrir. On donnerait donc au joueur une URL inutilisable
// précisément sur le déploiement où l'aide compte le plus.
func TestLaPageDonneLAdresseAuJoueur(t *testing.T) {
	js := string(lireAsset(t, "app.js"))
	html := string(lireAsset(t, "index.html"))

	if !strings.Contains(html, `id="note-serveur"`) || !strings.Contains(html, `id="ligne-serveur"`) {
		t.Error("index.html n'a pas de quoi afficher l'adresse du serveur")
	}
	if !strings.Contains(js, "data.serveur") {
		t.Error("app.js ne lit pas « serveur » dans /api/v1/version : la page ne " +
			"peut donc pas dire au joueur quoi passer à --server=")
	}
	if !strings.Contains(js, "--server=") {
		t.Error("app.js n'écrit pas la ligne complète : une adresse sans l'option " +
			"laisse encore deviner")
	}
	// Masquée par défaut : un serveur qui n'annonce rien ne doit pas afficher
	// une ligne à trou.
	if !strings.Contains(js, `noteServeur.hidden = adresse === ""`) {
		t.Error("le bloc n'est pas caché quand le serveur n'annonce aucune adresse")
	}
	// Rien n'entre en HTML sur cette page, y compris une valeur de configuration.
	if !strings.Contains(js, "ligneServeur.textContent") {
		t.Error("l'adresse doit être posée en textContent, comme tout le reste")
	}
	if strings.Contains(js, "window.location.origin") || strings.Contains(js, "location.host") {
		t.Error("la ligne --server= est bâtie depuis l'origine de la page : elle " +
			"donnerait une adresse https derrière un proxy TLS, que le jeu refuse " +
			"d'ouvrir. C'est au serveur de dire son adresse joignable")
	}
}
