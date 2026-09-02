// Package saison calcule le classement CLASSE : une saison par mois, des points
// tires du rang, et des paliers.
//
// POURQUOI LE CLASSEMENT GENERAL NE POUVAIT PAS SERVIR DE LADDER
// --------------------------------------------------------------
// Le classement general somme `score * multiplicateur` sur tous les jeux. Les
// echelles n'ont rien a voir entre elles, et la table `games` le dit
// elle-meme : `max_plausible_score` vaut 5 000 pour Envol et 999 999 pour
// Aplomb, soit un facteur DEUX CENTS. Additionner ces nombres ne classe pas
// des joueurs, ca classe des machines : celui qui joue Aplomb bat celui qui
// joue Envol avant d'avoir commence.
//
// Ici un score brut ne vaut jamais rien par lui-meme. Ce qui compte est le
// RANG qu'il donne dans son creneau. Toutes les bornes valent alors la meme
// chose au sommet, et il devient rentable d'aller sur celle que personne ne
// touche — ce qui est exactement ce qu'on veut d'une salle de dix-neuf bornes
// dont huit portent un jeu.
//
// CE QUI REND UNE SAISON ADDICTIVE, ET CHAQUE POINT EST UN CHOIX
// --------------------------------------------------------------
//  1. LA SAISON SE REMET A ZERO, PAS LE PALMARES. Le premier du mois, tout le
//     monde repart de zero. Le palier atteint, lui, est fige et se garde. On
//     peut perdre son rang, jamais son meilleur mois.
//  2. TOUTES LES BORNES VALENT PAREIL AU SOMMET. Le chemin le plus court vers
//     le haut est d'etre sur le podium d'un creneau que personne ne joue.
//  3. UNE PARTIE VALIDE RAPPORTE TOUJOURS AU MOINS UN POINT. Aucune session
//     n'est perdue, ce qui est la seule facon d'ouvrir le jeu a quelqu'un qui
//     arrive le 28.
//  4. L'ECART EST AFFICHE. « 42 points derriere le deuxieme » est le nombre le
//     plus motivant d'un classement, et le seul que personne ne calcule seul.
//  5. LE PROCHAIN PAS EST NOMME. Pas « joue plus » : « il te manque 340 points
//     sur Piano pour prendre la deuxieme place, qui en vaut 15 ».
//
// CE PAQUET NE TOUCHE NI BASE NI RESEAU. Il recoit des lignes deja lues et rend
// un classement. C'est ce qui permet a `saison_test.go` de couvrir les egalites,
// les saisons vides et les bornes de paliers sans PostgreSQL.
package saison

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
	"time"
)

// PlacementRequis — le nombre de parties valides avant d'etre CLASSE.
//
// Trois, et pas une. Une seule partie chanceuse suffirait a poser quelqu'un sur
// un podium qu'il ne tiendrait pas, et le classement perdrait le peu qu'on lui
// demande : dire qui joue bien. Trois est aussi assez court pour qu'un
// nouveau venu soit classe dans sa premiere soiree — le seuil doit filtrer le
// hasard, pas decourager l'arrivee.
const PlacementRequis = 3

// PointsDuRang — ce que rapporte une place dans un creneau, avant
// multiplicateur.
//
// La decroissance est d'environ 20 % par place sur les cinq premieres, puis
// s'aplatit. Consequence voulue : la premiere place vaut plus que les places 4
// a 10 reunies, donc viser le sommet d'UN creneau bat le fait d'etre tiede
// partout. C'est ce qui pousse a choisir une machine et a la travailler.
//
// Le plancher est de 1 : toute partie valide rapporte. Le zero n'existe pas
// dans ce bareme, et c'est delibere.
var bareme = [...]int{100, 80, 65, 52, 42, 34, 27, 22, 17, 14, 11, 9, 7, 6, 5, 4, 3, 2, 2, 1}

func PointsDuRang(rang int) int {
	if rang < 1 {
		return 0
	}
	if rang > len(bareme) {
		return 1
	}
	return bareme[rang-1]
}

// Palier — un niveau de saison.
type Palier struct {
	Nom   string `json:"nom"`
	Seuil int    `json:"seuil"`
	// Rang du palier, 0 pour NON CLASSE. Sert a l'affichage, qui veut une
	// couleur et une hauteur sans avoir a chercher le nom dans une liste.
	Niveau int `json:"niveau"`
}

// LES PALIERS. Les noms viennent de la salle et de rien d'autre : un jeton
// qu'on insere, la relance, la serie de jours, le record, la plaque doree que
// la vitrine vend, la lame du Couperet, et les dix-neuf bornes au sommet.
//
// LES SEUILS SONT MESURES CONTRE LA TABLE `games`. La somme des
// multiplicateurs des quatorze creneaux vaut 21,0, donc une saison parfaite —
// premier partout — vaut 2 100 points. DIX-NEUF a 1 900 demande donc 90 % d'une
// saison parfaite : il doit rester rare, sinon il ne veut rien dire.
// `TestLeSommetResteAtteignableEtRare` verifie cette arithmetique contre la
// vraie table, et rougit le jour ou une borne est ajoutee ou retiree.
var paliers = []Palier{
	{Nom: "JETON", Seuil: 0, Niveau: 1},
	{Nom: "RELANCE", Seuil: 120, Niveau: 2},
	{Nom: "SERIE", Seuil: 300, Niveau: 3},
	{Nom: "RECORD", Seuil: 600, Niveau: 4},
	{Nom: "PLAQUE", Seuil: 1000, Niveau: 5},
	{Nom: "LAME", Seuil: 1500, Niveau: 6},
	{Nom: "DIX-NEUF", Seuil: 1900, Niveau: 7},
}

// NonClasse — celui qui n'a pas fini son placement. Ce n'est pas un echec et le
// nom ne doit pas le laisser croire : c'est un etat qui dure trois parties.
var NonClasse = Palier{Nom: "NON CLASSE", Seuil: 0, Niveau: 0}

// Paliers rend la liste, du plus bas au plus haut. L'affichage en a besoin pour
// dessiner l'echelle entiere, pas seulement le palier courant : voir de combien
// on est loin du suivant est la moitie de l'interet.
func Paliers() []Palier {
	out := make([]Palier, len(paliers))
	copy(out, paliers)
	return out
}

// PalierDe rend le palier atteint. Moins de `PlacementRequis` parties et l'on
// est NON CLASSE, quel que soit le nombre de points — trois parties peuvent
// suffire a beaucoup de points si les creneaux sont vides.
func PalierDe(points, parties int) Palier {
	if parties < PlacementRequis {
		return NonClasse
	}
	atteint := paliers[0]
	for _, p := range paliers {
		if points >= p.Seuil {
			atteint = p
		}
	}
	return atteint
}

// PalierSuivant rend le palier au-dessus, et faux s'il n'y en a plus.
func PalierSuivant(courant Palier) (Palier, bool) {
	for _, p := range paliers {
		if p.Seuil > courant.Seuil {
			return p, true
		}
	}
	return Palier{}, false
}

/* ========================================================================== */
/* Les saisons                                                                */
/* ========================================================================== */

// Cle rend la cle d'une saison : « 2026-09 ». En UTC, et c'est un choix : une
// saison qui changerait de date selon le fuseau du lecteur ferait basculer le
// podium d'un jour a l'autre selon d'ou on le regarde.
func Cle(t time.Time) string {
	u := t.UTC()
	return fmt.Sprintf("%04d-%02d", u.Year(), int(u.Month()))
}

// CleValide dit si une chaine a la forme d'une cle de saison. Les bornes — 2020
// et 2100 — n'ont rien de profond : elles evitent qu'une requete demande
// l'annee 999999 et fasse calculer une plage absurde.
func CleValide(cle string) bool {
	if len(cle) != 7 || cle[4] != '-' {
		return false
	}
	an, err := strconv.Atoi(cle[:4])
	if err != nil || an < 2020 || an > 2100 {
		return false
	}
	mois, err := strconv.Atoi(cle[5:])
	return err == nil && mois >= 1 && mois <= 12
}

// Bornes rend le debut inclus et la fin EXCLUE de la saison.
func Bornes(cle string) (debut, fin time.Time, ok bool) {
	if !CleValide(cle) {
		return time.Time{}, time.Time{}, false
	}
	an, _ := strconv.Atoi(cle[:4])
	mois, _ := strconv.Atoi(cle[5:])
	debut = time.Date(an, time.Month(mois), 1, 0, 0, 0, 0, time.UTC)
	return debut, debut.AddDate(0, 1, 0), true
}

// Precedente rend la cle du mois d'avant. Utile pour le palmares, qui montre la
// saison qui vient de se fermer.
func Precedente(cle string) string {
	debut, _, ok := Bornes(cle)
	if !ok {
		return ""
	}
	return Cle(debut.AddDate(0, -1, 0))
}

// Libelle rend « septembre 2026 ». Les mois sont ecrits ici plutot que tires du
// systeme : `time.Month.String()` est anglais, et le site est francais.
var mois = [...]string{
	"janvier", "février", "mars", "avril", "mai", "juin",
	"juillet", "août", "septembre", "octobre", "novembre", "décembre",
}

func Libelle(cle string) string {
	debut, _, ok := Bornes(cle)
	if !ok {
		return cle
	}
	return fmt.Sprintf("%s %d", mois[int(debut.Month())-1], debut.Year())
}

// JoursRestants rend le nombre de jours entiers avant la fin de la saison, 0 si
// elle est finie. C'est l'urgence, et c'est ce qui fait revenir le 29.
func JoursRestants(cle string, maintenant time.Time) int {
	_, fin, ok := Bornes(cle)
	if !ok {
		return 0
	}
	reste := fin.Sub(maintenant.UTC())
	if reste <= 0 {
		return 0
	}
	return int(reste.Hours() / 24)
}

/* ========================================================================== */
/* Le calcul du classement                                                    */
/* ========================================================================== */

// Meilleur — une ligne telle que le magasin la rend : le meilleur score d'un
// joueur sur un creneau, pendant la saison.
type Meilleur struct {
	Pseudo         string
	Jeu            string // slug du creneau, « envol-hard »
	JeuNom         string // « Envol »
	Difficulte     string
	Score          int64
	Multiplicateur float64
	Parties        int // parties valides de ce joueur sur ce creneau
	Quand          time.Time
}

// Creneau — ce qu'un joueur a fait sur une borne, une fois classe.
type Creneau struct {
	Jeu        string    `json:"jeu"`
	JeuNom     string    `json:"jeuNom"`
	Difficulte string    `json:"difficulte"`
	Score      int64     `json:"score"`
	Rang       int       `json:"rang"`    // sa place sur ce creneau
	Joueurs    int       `json:"joueurs"` // combien s'y sont mesures
	Points     int       `json:"points"`  // ce que cette place rapporte
	Parties    int       `json:"parties"`
	Quand      time.Time `json:"quand"`
}

// Prochain — LE PAS SUIVANT, nomme.
//
// Un classement qui dit seulement « tu es douzieme » ne donne aucune prise. Ce
// champ rend la prise : le creneau ou gagner une place rapporte le plus, le
// score qu'il faut battre, et ce que ca vaut. C'est la difference entre un
// tableau et un objectif.
type Prochain struct {
	Jeu       string `json:"jeu"`
	JeuNom    string `json:"jeuNom"`
	RangVise  int    `json:"rangVise"`
	ScoreVise int64  `json:"scoreVise"`
	Gain      int    `json:"gain"` // points gagnes en prenant cette place
}

// Ligne — un joueur dans le classement de la saison.
type Ligne struct {
	Rang    int       `json:"rang"`
	Pseudo  string    `json:"pseudo"`
	Points  int       `json:"points"`
	Parties int       `json:"parties"`
	Palier  Palier    `json:"palier"`
	Ecart   int       `json:"ecart"` // points qui separent du rang au-dessus
	Podiums int       `json:"podiums"`
	Ors     int       `json:"ors"` // premieres places de creneau
	Vue     time.Time `json:"derniereActivite"`

	// Le detail, trie du creneau le plus rapportant au moins rapportant.
	Creneaux []Creneau `json:"creneaux,omitempty"`
	Prochain *Prochain `json:"prochain,omitempty"`
}

// cle identifie un joueur sur un creneau. Au niveau du paquet et non dans
// `Classer`, parce que `prochainPas` la lit aussi.
type cle struct{ pseudo, jeu string }

// Classer transforme les meilleurs scores d'une saison en classement.
//
// L'ordre des egalites est FIXE et ne depend pas de l'ordre d'arrivee des
// lignes : a points egaux, le plus petit nombre de parties passe devant — avoir
// fait autant en jouant moins est le signe qu'on cherche a mesurer — puis le
// pseudo. Sans cette regle, deux appels successifs pouvaient rendre deux
// podiums differents, ce qu'aucune page ne saurait expliquer.
func Classer(meilleurs []Meilleur) []Ligne {
	// 1. Regrouper par creneau et classer a l'interieur.
	parCreneau := map[string][]Meilleur{}
	for _, m := range meilleurs {
		parCreneau[m.Jeu] = append(parCreneau[m.Jeu], m)
	}

	rangs := map[cle]int{}
	joueurs := map[string]int{}
	for jeu, lignes := range parCreneau {
		sort.Slice(lignes, func(i, j int) bool {
			if lignes[i].Score != lignes[j].Score {
				return lignes[i].Score > lignes[j].Score
			}
			// A score egal, le PREMIER ARRIVE passe devant. C'est la regle des
			// bornes depuis toujours : celui qui a pose le score le premier le
			// detient, et celui qui l'egale ne le lui prend pas.
			if !lignes[i].Quand.Equal(lignes[j].Quand) {
				return lignes[i].Quand.Before(lignes[j].Quand)
			}
			return lignes[i].Pseudo < lignes[j].Pseudo
		})
		joueurs[jeu] = len(lignes)
		for i, l := range lignes {
			rangs[cle{l.Pseudo, jeu}] = i + 1
		}
		parCreneau[jeu] = lignes
	}

	// 2. Additionner par joueur.
	type acc struct {
		points, parties, podiums, ors int
		vue                           time.Time
		creneaux                      []Creneau
	}
	joueur := map[string]*acc{}
	for jeu, lignes := range parCreneau {
		for _, m := range lignes {
			r := rangs[cle{m.Pseudo, jeu}]
			pts := int(float64(PointsDuRang(r))*m.Multiplicateur + 0.5)
			a := joueur[m.Pseudo]
			if a == nil {
				a = &acc{}
				joueur[m.Pseudo] = a
			}
			a.points += pts
			a.parties += m.Parties
			if r <= 3 {
				a.podiums++
			}
			if r == 1 {
				a.ors++
			}
			if m.Quand.After(a.vue) {
				a.vue = m.Quand
			}
			a.creneaux = append(a.creneaux, Creneau{
				Jeu: m.Jeu, JeuNom: m.JeuNom, Difficulte: m.Difficulte,
				Score: m.Score, Rang: r, Joueurs: joueurs[jeu],
				Points: pts, Parties: m.Parties, Quand: m.Quand,
			})
		}
	}

	// 3. Ordonner, poser les rangs, les ecarts et le pas suivant.
	out := make([]Ligne, 0, len(joueur))
	for pseudo, a := range joueur {
		sort.Slice(a.creneaux, func(i, j int) bool {
			if a.creneaux[i].Points != a.creneaux[j].Points {
				return a.creneaux[i].Points > a.creneaux[j].Points
			}
			return a.creneaux[i].Jeu < a.creneaux[j].Jeu
		})
		out = append(out, Ligne{
			Pseudo: pseudo, Points: a.points, Parties: a.parties,
			Palier: PalierDe(a.points, a.parties), Podiums: a.podiums,
			Ors: a.ors, Vue: a.vue, Creneaux: a.creneaux,
			Prochain: prochainPas(a.creneaux, parCreneau),
		})
	}

	sort.Slice(out, func(i, j int) bool {
		if out[i].Points != out[j].Points {
			return out[i].Points > out[j].Points
		}
		if out[i].Parties != out[j].Parties {
			return out[i].Parties < out[j].Parties
		}
		return out[i].Pseudo < out[j].Pseudo
	})

	for i := range out {
		out[i].Rang = i + 1
		if i > 0 {
			out[i].Ecart = out[i-1].Points - out[i].Points
		} else if len(out) > 1 {
			// Le premier n'a personne au-dessus : son ecart est son AVANCE sur
			// le second. C'est le seul nombre qui l'interesse, et le laisser a
			// zero laisserait croire qu'il est rejoint.
			out[i].Ecart = out[i].Points - out[1].Points
		}
	}
	return out
}

// prochainPas cherche, parmi les creneaux deja joues, celui ou gagner une place
// rapporte le plus. Rien a rendre pour qui est premier partout.
func prochainPas(siens []Creneau, parCreneau map[string][]Meilleur) *Prochain {

	var meilleur *Prochain
	for _, c := range siens {
		if c.Rang <= 1 {
			continue
		}
		lignes := parCreneau[c.Jeu]
		devant := lignes[c.Rang-2] // le joueur juste au-dessus
		mult := 1.0
		if devant.Multiplicateur > 0 {
			mult = devant.Multiplicateur
		}
		gain := int(float64(PointsDuRang(c.Rang-1))*mult+0.5) - c.Points
		if gain <= 0 {
			continue
		}
		if meilleur == nil || gain > meilleur.Gain {
			meilleur = &Prochain{
				Jeu: c.Jeu, JeuNom: c.JeuNom,
				RangVise: c.Rang - 1, ScoreVise: devant.Score + 1, Gain: gain,
			}
		}
	}
	return meilleur
}

// Vierge — un creneau qu'un joueur n'a pas touche de la saison.
//
// C'est l'autre moitie du conseil, et souvent la plus rentable : sur une borne
// ou trois personnes se sont mesurees, arriver quatrieme rapporte 52 points
// avant multiplicateur, la ou grimper d'une place sur une borne disputee en
// rapporte deux.
type Vierge struct {
	Jeu     string `json:"jeu"`
	JeuNom  string `json:"jeuNom"`
	Joueurs int    `json:"joueurs"` // deja presents sur ce creneau
	Gain    int    `json:"gain"`    // ce que rapporterait la derniere place libre
}

// CreneauServeur — une borne telle que le serveur la declare, jouee ou non.
//
// C'est la liste `games`, et elle est indispensable ici : sans elle on ne
// saurait pas qu'un creneau existe tant que personne n'y a joue, ce qui est
// precisement le cas le plus interessant a signaler.
type CreneauServeur struct {
	Jeu            string
	JeuNom         string
	Difficulte     string
	Multiplicateur float64
}

// Vierges rend les creneaux non joues par `pseudo`, du plus rentable au moins
// rentable.
func Vierges(pseudo string, meilleurs []Meilleur, tous []CreneauServeur) []Vierge {
	joues := map[string]bool{}
	presents := map[string]int{}
	for _, m := range meilleurs {
		presents[m.Jeu]++
		if strings.EqualFold(m.Pseudo, pseudo) {
			joues[m.Jeu] = true
		}
	}

	out := []Vierge{}
	for _, c := range tous {
		if joues[c.Jeu] {
			continue
		}
		mult := c.Multiplicateur
		if mult <= 0 {
			mult = 1.0
		}
		// La place qu'on prendrait en arrivant : derriere ceux qui sont deja la.
		gain := int(float64(PointsDuRang(presents[c.Jeu]+1))*mult + 0.5)
		out = append(out, Vierge{Jeu: c.Jeu, JeuNom: c.JeuNom,
			Joueurs: presents[c.Jeu], Gain: gain})
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].Gain != out[j].Gain {
			return out[i].Gain > out[j].Gain
		}
		return out[i].Jeu < out[j].Jeu
	})
	return out
}

// Trouver rend la ligne d'un joueur, insensible a la casse comme l'est
// `citext` en base. Nil s'il n'a rien fait de la saison.
func Trouver(lignes []Ligne, pseudo string) *Ligne {
	for i := range lignes {
		if strings.EqualFold(lignes[i].Pseudo, pseudo) {
			return &lignes[i]
		}
	}
	return nil
}
