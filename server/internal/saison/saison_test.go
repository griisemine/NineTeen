package saison

import (
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"testing"
	"time"
)

func t0(jour int) time.Time {
	return time.Date(2026, 9, jour, 12, 0, 0, 0, time.UTC)
}

func m(pseudo, jeu string, score int64, mult float64, parties, jour int) Meilleur {
	return Meilleur{Pseudo: pseudo, Jeu: jeu, JeuNom: jeu, Score: score,
		Multiplicateur: mult, Parties: parties, Quand: t0(jour)}
}

/* ========================================================================== */
/* Les saisons                                                                */
/* ========================================================================== */

func TestUneSaisonEstUnMoisUTC(t *testing.T) {
	if got := Cle(time.Date(2026, 9, 1, 0, 0, 0, 0, time.UTC)); got != "2026-09" {
		t.Errorf("cle = %q", got)
	}
	// Le dernier instant du mois appartient encore au mois. Une seconde plus
	// tard, non : c'est la bascule, et elle doit se lire dans le test.
	if got := Cle(time.Date(2026, 9, 30, 23, 59, 59, 0, time.UTC)); got != "2026-09" {
		t.Errorf("dernier instant : %q", got)
	}
	if got := Cle(time.Date(2026, 10, 1, 0, 0, 0, 0, time.UTC)); got != "2026-10" {
		t.Errorf("premier instant du mois suivant : %q", got)
	}

	// EN UTC, ET PAS DANS LE FUSEAU DU LECTEUR. Sans ce choix, le podium
	// basculerait un jour plus tot ou plus tard selon d'ou on le regarde, et
	// deux joueurs verraient deux saisons differentes au meme instant.
	tokyo := time.FixedZone("JST", 9*3600)
	minuitTokyo := time.Date(2026, 10, 1, 8, 0, 0, 0, tokyo) // 30 sept 23 h UTC
	if got := Cle(minuitTokyo); got != "2026-09" {
		t.Errorf("le fuseau du lecteur ne doit pas changer la saison : %q", got)
	}
}

func TestUneCleAbsurdeEstRefusee(t *testing.T) {
	for _, cas := range []string{"", "2026-13", "2026-00", "202609", "2026-9",
		"1999-01", "2200-01", "abcd-ef", "2026-09-01"} {
		if CleValide(cas) {
			t.Errorf("%q acceptee", cas)
		}
		if _, _, ok := Bornes(cas); ok {
			t.Errorf("%q a rendu des bornes", cas)
		}
	}
	if !CleValide("2026-09") {
		t.Error("2026-09 refusee")
	}
}

func TestLesBornesCouvrentLeMoisEtRien(t *testing.T) {
	debut, fin, ok := Bornes("2026-02")
	if !ok {
		t.Fatal("bornes refusees")
	}
	if debut.Day() != 1 || debut.Month() != time.February {
		t.Errorf("debut = %v", debut)
	}
	// La fin est EXCLUE et vaut le premier du mois suivant : c'est ce qui rend
	// la requete SQL juste sans se demander si fevrier a 28 ou 29 jours.
	if !fin.Equal(time.Date(2026, 3, 1, 0, 0, 0, 0, time.UTC)) {
		t.Errorf("fin = %v", fin)
	}
	if Precedente("2026-01") != "2025-12" {
		t.Errorf("precedente de janvier = %q", Precedente("2026-01"))
	}
	if Libelle("2026-08") != "août 2026" {
		t.Errorf("libelle = %q", Libelle("2026-08"))
	}
}

func TestLesJoursRestantsFontLUrgence(t *testing.T) {
	if got := JoursRestants("2026-09", t0(1)); got != 29 {
		t.Errorf("le 1er septembre a midi : %d jours restants", got)
	}
	if got := JoursRestants("2026-09", t0(30)); got != 0 {
		t.Errorf("le dernier jour : %d", got)
	}
	// Une saison passee ne rend jamais un nombre negatif : la page afficherait
	// « il reste -12 jours ».
	if got := JoursRestants("2026-01", t0(1)); got != 0 {
		t.Errorf("saison finie : %d", got)
	}
}

/* ========================================================================== */
/* Le bareme et les paliers                                                   */
/* ========================================================================== */

func TestLeBaremeRecompenseLaProfondeurSansPunirLaLargeur(t *testing.T) {
	// DEUX PROPRIETES, et le bareme n'existe que pour les tenir toutes les deux.
	//
	// La premiere : LE PLUS GROS SAUT DE TOUTE LA TABLE EST CELUI QUI MENE A LA
	// PREMIERE PLACE. C'est ce qui fait qu'on se bat pour elle plutot que de se
	// contenter d'etre deuxieme.
	saut1 := PointsDuRang(1) - PointsDuRang(2)
	for r := 3; r <= 25; r++ {
		if s := PointsDuRang(r-1) - PointsDuRang(r); s >= saut1 {
			t.Errorf("passer %d -> %d rapporte %d, autant que prendre la premiere place (%d)",
				r, r-1, s, saut1)
		}
	}

	// La seconde : LA PROFONDEUR BAT LA MEDIOCRITE ETALEE. Etre premier une
	// fois vaut plus qu'etre sixieme, septieme et huitieme reunis.
	//
	// Mais la largeur paie quand meme, et c'est VOULU : la salle a dix-neuf
	// bornes, et un classement qui n'en recompenserait qu'une n'en ferait
	// visiter qu'une. Trois quatriemes places (156) valent donc plus qu'une
	// premiere (100) — il faut seulement y avoir joue trois fois plus.
	if etale := PointsDuRang(6) + PointsDuRang(7) + PointsDuRang(8); PointsDuRang(1) <= etale {
		t.Errorf("premier %d contre %d pour les places 6, 7 et 8",
			PointsDuRang(1), etale)
	}
	// Strictement decroissant : une place gagnee doit TOUJOURS rapporter.
	for r := 2; r <= 25; r++ {
		if PointsDuRang(r) > PointsDuRang(r-1) {
			t.Errorf("rang %d rapporte plus que %d", r, r-1)
		}
	}
	// Le plancher est 1, jamais 0 : une partie valide rapporte toujours.
	if PointsDuRang(1000) != 1 {
		t.Errorf("millieme = %d, le plancher doit valoir 1", PointsDuRang(1000))
	}
	if PointsDuRang(0) != 0 || PointsDuRang(-3) != 0 {
		t.Error("un rang absurde ne rapporte rien")
	}
}

func TestOnEstNonClasseTantQueLePlacementNEstPasFait(t *testing.T) {
	if p := PalierDe(5000, PlacementRequis-1); p.Niveau != 0 {
		t.Errorf("deux parties et 5000 points : %s", p.Nom)
	}
	if p := PalierDe(0, PlacementRequis); p.Nom != "JETON" {
		t.Errorf("trois parties et zero point : %s", p.Nom)
	}
	// Chaque seuil est atteint pile a sa valeur, et pas un point plus tard.
	for _, p := range Paliers() {
		if got := PalierDe(p.Seuil, PlacementRequis); got.Nom != p.Nom {
			t.Errorf("%d points rend %s, %s attendu", p.Seuil, got.Nom, p.Nom)
		}
		if p.Seuil > 0 {
			if got := PalierDe(p.Seuil-1, PlacementRequis); got.Nom == p.Nom {
				t.Errorf("%d points rend deja %s", p.Seuil-1, p.Nom)
			}
		}
	}
	// Les seuils montent, et les niveaux avec eux.
	prec := Palier{Seuil: -1, Niveau: 0}
	for _, p := range Paliers() {
		if p.Seuil <= prec.Seuil || p.Niveau != prec.Niveau+1 {
			t.Fatalf("palier %s casse l'ordre", p.Nom)
		}
		prec = p
	}
	if _, ok := PalierSuivant(Paliers()[len(Paliers())-1]); ok {
		t.Error("le sommet ne doit rien avoir au-dessus")
	}
}

// LE SOMMET DOIT RESTER ATTEIGNABLE ET RARE, et ce n'est verifiable que contre
// la vraie table des jeux.
//
// Les seuils sont des entiers ecrits a la main. Le jour ou une borne est
// ajoutee ou retiree de `0001_initial.sql`, le maximum d'une saison change, et
// DIX-NEUF devient soit impossible soit banal — sans qu'aucune ligne de code ne
// bouge. Ce test lit la migration et le dit.
func TestLeSommetResteAtteignableEtRare(t *testing.T) {
	brut, err := os.ReadFile(filepath.Join("..", "migrations", "0001_initial.sql"))
	if err != nil {
		t.Skipf("migration illisible d'ici : %v", err)
	}
	// Les lignes d'insertion : ( 1, 'envol-hard', 'Envol', 'hard', 2.0, 5000),
	motif := regexp.MustCompile(`\(\s*\d+,\s*'[a-z-]+',\s*'[^']+',\s*'[a-z]+',\s*([0-9.]+),`)
	total := 0.0
	creneaux := 0
	for _, occ := range motif.FindAllStringSubmatch(string(brut), -1) {
		mult, err := strconv.ParseFloat(occ[1], 64)
		if err != nil {
			t.Fatalf("multiplicateur illisible : %q", occ[1])
		}
		total += mult
		creneaux++
	}
	if creneaux == 0 {
		t.Fatal("aucun creneau lu dans la migration : ce test ne controle plus rien")
	}

	max := int(total * float64(PointsDuRang(1)))
	sommet := Paliers()[len(Paliers())-1]
	t.Logf("%d creneaux, multiplicateurs %.1f, saison parfaite %d points, sommet %d",
		creneaux, total, max, sommet.Seuil)

	if sommet.Seuil >= max {
		t.Errorf("DIX-NEUF (%d) est hors d'atteinte : le maximum vaut %d",
			sommet.Seuil, max)
	}
	// Rare : au moins 85 % d'une saison parfaite. Sous ce seuil, le sommet
	// cesse de vouloir dire quelque chose.
	if part := float64(sommet.Seuil) / float64(max); part < 0.85 {
		t.Errorf("DIX-NEUF ne demande que %.0f %% d'une saison parfaite", part*100)
	}
}

/* ========================================================================== */
/* Le classement                                                              */
/* ========================================================================== */

func TestUneSaisonVideNeCassePas(t *testing.T) {
	if got := Classer(nil); len(got) != 0 {
		t.Errorf("%d lignes pour rien", len(got))
	}
	if Trouver(nil, "personne") != nil {
		t.Error("trouver dans le vide")
	}
}

// LE POINT DE DEPART DE TOUT CE PAQUET : un score brut ne classe personne.
func TestUnGrosScoreNeBatPasUnPremierAilleurs(t *testing.T) {
	// Snake plafonne a 20 000, Envol a 5 000 — la table `games` le dit. Celui
	// qui fait 19 000 a Snake derriere quelqu'un d'autre ne doit pas depasser
	// celui qui est premier a Envol avec 12 points.
	lignes := Classer([]Meilleur{
		m("Gros", "snake-hard", 19000, 2.0, 5, 3),
		m("Roi", "snake-hard", 19500, 2.0, 5, 2),
		m("Roi", "envol-hard", 40, 2.0, 5, 2),
		m("Petit", "envol-hard", 12, 2.0, 5, 4),
	})
	if len(lignes) != 3 {
		t.Fatalf("%d joueurs", len(lignes))
	}
	if lignes[0].Pseudo != "Roi" {
		t.Errorf("premier = %s (deux premieres places)", lignes[0].Pseudo)
	}
	// Gros est second de Snake (80 x 2 = 160), Petit second d'Envol (160 aussi).
	// A egalite, celui qui a joue le MOINS de parties passe devant. Les deux en
	// ont cinq, donc c'est le pseudo qui tranche : Gros avant Petit.
	if lignes[1].Points != lignes[2].Points {
		t.Errorf("%d contre %d : les deux secondes places valent pareil",
			lignes[1].Points, lignes[2].Points)
	}
}

func TestLesEgalitesSontTrancheesParLAnteriorite(t *testing.T) {
	// Meme score sur le meme creneau : celui qui l'a pose EN PREMIER le detient.
	// C'est la regle des bornes, et elle evite qu'un rang change tout seul.
	lignes := Classer([]Meilleur{
		m("Tard", "piano", 500, 1.0, 3, 20),
		m("Tot", "piano", 500, 1.0, 3, 2),
	})
	if lignes[0].Pseudo != "Tot" {
		t.Errorf("premier = %s, le plus ancien etait attendu", lignes[0].Pseudo)
	}
	if lignes[0].Creneaux[0].Rang != 1 || lignes[1].Creneaux[0].Rang != 2 {
		t.Error("les rangs de creneau ne suivent pas l'anteriorite")
	}
}

func TestLeClassementEstStableDUnAppelALAutre(t *testing.T) {
	// Deux appels sur les memes donnees dans un autre ordre doivent rendre le
	// meme podium. Sans regle d'egalite fixe, le parcours d'une map suffisait a
	// intervertir deux joueurs d'un rafraichissement a l'autre.
	brut := []Meilleur{
		m("A", "piano", 10, 1.0, 3, 5), m("B", "piano", 10, 1.0, 3, 5),
		m("C", "dedale", 10, 1.5, 3, 5), m("A", "dedale", 20, 1.5, 3, 4),
	}
	ref := Classer(brut)
	for i := 0; i < 12; i++ {
		autre := Classer([]Meilleur{brut[3], brut[1], brut[2], brut[0]})
		for j := range ref {
			if ref[j].Pseudo != autre[j].Pseudo || ref[j].Points != autre[j].Points {
				t.Fatalf("rang %d : %s(%d) puis %s(%d)", j+1,
					ref[j].Pseudo, ref[j].Points, autre[j].Pseudo, autre[j].Points)
			}
		}
	}
}

func TestLEcartEstCeQuiSepareDuRangAuDessus(t *testing.T) {
	lignes := Classer([]Meilleur{
		m("Un", "piano", 300, 1.0, 3, 2),    // 1er : 100
		m("Deux", "piano", 200, 1.0, 3, 3),  // 2e  :  80
		m("Trois", "piano", 100, 1.0, 3, 4), // 3e :  65
	})
	if lignes[1].Ecart != 20 {
		t.Errorf("le deuxieme est a %d points du premier, 20 attendus", lignes[1].Ecart)
	}
	if lignes[2].Ecart != 15 {
		t.Errorf("le troisieme est a %d du deuxieme, 15 attendus", lignes[2].Ecart)
	}
	// Le premier n'a personne au-dessus : son ecart est son AVANCE. Laisser
	// zero laisserait croire qu'il est rejoint.
	if lignes[0].Ecart != 20 {
		t.Errorf("l'avance du premier vaut %d, 20 attendus", lignes[0].Ecart)
	}
}

func TestLeProchainPasNommeLaBorneEtLeScore(t *testing.T) {
	lignes := Classer([]Meilleur{
		// Il est deuxieme partout, mais gagner une place ne rapporte pas
		// pareil : sur un creneau a multiplicateur 2,5, la place vaut plus.
		m("Roi", "piano", 900, 1.0, 3, 2),
		m("Moi", "piano", 800, 1.0, 3, 3),
		m("Roi", "demineur-hard", 90, 2.5, 3, 2),
		m("Moi", "demineur-hard", 80, 2.5, 3, 3),
	})
	moi := Trouver(lignes, "Moi")
	if moi == nil || moi.Prochain == nil {
		t.Fatal("aucun conseil pour un deuxieme")
	}
	if moi.Prochain.Jeu != "demineur-hard" {
		t.Errorf("conseil sur %s : le creneau le plus rentable etait demineur-hard",
			moi.Prochain.Jeu)
	}
	// Le score a battre est celui du joueur devant, plus un.
	if moi.Prochain.ScoreVise != 91 {
		t.Errorf("score vise %d, 91 attendu", moi.Prochain.ScoreVise)
	}
	if moi.Prochain.RangVise != 1 {
		t.Errorf("rang vise %d", moi.Prochain.RangVise)
	}
	// (100 - 80) x 2,5 = 50
	if moi.Prochain.Gain != 50 {
		t.Errorf("gain %d, 50 attendu", moi.Prochain.Gain)
	}
	// Celui qui est premier partout n'a rien a viser, et on ne lui invente rien.
	if roi := Trouver(lignes, "Roi"); roi == nil || roi.Prochain != nil {
		t.Error("un conseil a ete invente pour le premier de partout")
	}
}

func TestLesCreneauxViergesSontLeVraiRaccourci(t *testing.T) {
	tous := []CreneauServeur{
		{Jeu: "piano", JeuNom: "Piano", Multiplicateur: 1.0},
		{Jeu: "dedale", JeuNom: "Dédale", Multiplicateur: 1.5},
		{Jeu: "demineur-hard", JeuNom: "Démineur", Multiplicateur: 2.5},
	}
	meilleurs := []Meilleur{
		m("Moi", "piano", 100, 1.0, 3, 2),
		// Trois joueurs se disputent Dedale, personne n'est sur le Demineur.
		m("A", "dedale", 300, 1.5, 3, 2),
		m("B", "dedale", 200, 1.5, 3, 2),
		m("C", "dedale", 100, 1.5, 3, 2),
	}
	v := Vierges("Moi", meilleurs, tous)
	if len(v) != 2 {
		t.Fatalf("%d creneaux vierges, 2 attendus", len(v))
	}
	// La borne DESERTE passe devant la borne disputee : y arriver donne la
	// premiere place, soit 100 x 2,5 = 250, contre une quatrieme place a
	// 52 x 1,5 = 78 sur Dedale.
	if v[0].Jeu != "demineur-hard" || v[0].Gain != 250 {
		t.Errorf("premier conseil : %s a %d points", v[0].Jeu, v[0].Gain)
	}
	if v[1].Jeu != "dedale" || v[1].Gain != 78 {
		t.Errorf("second conseil : %s a %d points", v[1].Jeu, v[1].Gain)
	}
	// La casse du pseudo ne doit pas faire reapparaitre un creneau deja joue :
	// `citext` en base ne distingue pas « Moi » de « moi ».
	if len(Vierges("MOI", meilleurs, tous)) != 2 {
		t.Error("la casse du pseudo change le resultat")
	}
}

func TestLesPodiumsEtLesOrsSeComptent(t *testing.T) {
	lignes := Classer([]Meilleur{
		m("Moi", "piano", 900, 1.0, 4, 2),
		m("Moi", "dedale", 100, 1.5, 3, 2),
		m("Autre", "dedale", 900, 1.5, 3, 2),
		m("Tiers", "dedale", 500, 1.5, 3, 2),
	})
	moi := Trouver(lignes, "Moi")
	if moi.Ors != 1 {
		t.Errorf("%d premieres places, 1 attendue", moi.Ors)
	}
	if moi.Podiums != 2 {
		t.Errorf("%d podiums, 2 attendus (premier et troisieme)", moi.Podiums)
	}
	if moi.Parties != 7 {
		t.Errorf("%d parties, 7 attendues", moi.Parties)
	}
	// La derniere activite est la plus recente des siennes.
	if !moi.Vue.Equal(t0(2)) {
		t.Errorf("derniere activite %v", moi.Vue)
	}
	// Le detail est trie du creneau le plus rapportant au moins rapportant.
	if len(moi.Creneaux) != 2 || moi.Creneaux[0].Jeu != "piano" {
		t.Errorf("detail mal trie : %+v", moi.Creneaux)
	}
}

func TestUnJoueurEnPlacementFigureMaisNonClasse(t *testing.T) {
	lignes := Classer([]Meilleur{
		m("Neuf", "piano", 900, 1.0, 1, 2),
		m("Vieux", "piano", 100, 1.0, 9, 3),
	})
	neuf := Trouver(lignes, "Neuf")
	if neuf == nil {
		t.Fatal("un joueur en placement doit quand meme figurer")
	}
	if neuf.Palier.Niveau != 0 {
		t.Errorf("palier %s : une seule partie ne classe pas", neuf.Palier.Nom)
	}
	if neuf.Points == 0 {
		t.Error("ses points sont comptes malgre tout : c'est ce qui montre la progression")
	}
	if v := Trouver(lignes, "Vieux"); v.Palier.Niveau == 0 {
		t.Error("neuf parties, il doit etre classe")
	}
}
