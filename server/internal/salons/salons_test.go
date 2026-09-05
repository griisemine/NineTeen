// salons_test.go — les regles du rendez-vous, verifiees sans base ni socket.
//
// TOUT CE QUI EST TESTE ICI EST TESTABLE ICI. Le paquet ne connait ni SQL ni
// HTTP, et l'horloge lui est PASSEE : pas un `time.Sleep` dans ce fichier, donc
// pas un test qui devienne intermittent le jour ou la machine est chargee. Une
// peremption de douze secondes se verifie en avancant une variable, pas en
// attendant douze secondes.
package salons

import (
	"math"
	"strings"
	"testing"
	"time"

	"nineteen/internal/migrations"
)

// origine — une date fixe. Fixe et non `time.Now()` : un test qui depend de
// l'heure a laquelle on le lance a deja un pied dans l'intermittence.
var origine = time.Date(2026, 8, 31, 12, 0, 0, 0, time.UTC)

// neuf construit un salon en attente, son createur assis a la place 0.
func neuf(t *testing.T, places, camps int) *Salon {
	t.Helper()
	s := &Salon{
		Code: "K7M3QP", Nom: "Salon d'essai",
		Places: places, Camps: camps, Etat: Attente,
		Relais: 1234567890123, Proprietaire: 1, ProprietairePseudo: "un",
		CreeA: origine, ChangeA: origine,
	}
	if _, err := s.Asseoir(origine, 1, "un"); err != nil {
		t.Fatalf("le createur n'a pas pu s'asseoir : %v", err)
	}
	return s
}

/* ========================================================================== */
/* Places                                                                     */
/* ========================================================================== */

// TestAttributionDesPlaces — la place 0 au createur, puis la plus petite libre.
//
// Le client indexe des tableaux de `ROOM_CP_MAX_PLACES` entrees et le relais
// refuse toute place hors du salon : rendre autre chose qu'un indice contigu de
// 0..places-1 serait rendre une place que le transport refusera ensuite.
func TestAttributionDesPlaces(t *testing.T) {
	s := neuf(t, 4, 1)

	cas := []struct {
		joueur int64
		veut   int
	}{
		{2, 1}, {3, 2}, {4, 3},
	}
	for _, c := range cas {
		got, err := s.Asseoir(origine, c.joueur, "j")
		if err != nil {
			t.Fatalf("joueur %d refuse : %v", c.joueur, err)
		}
		if got != c.veut {
			t.Errorf("joueur %d a la place %d, attendu %d", c.joueur, got, c.veut)
		}
	}
	if n := s.Occupes(origine); n != 4 {
		t.Errorf("%d places occupees, attendu 4", n)
	}
}

// TestPlaceLibereeEstReattribuee — le coeur de la regle des places.
//
// Sans elle, un salon de huit ou trois joueurs sont passes distribuerait la
// place 8 au quatrieme, que le relais refuserait (`place >= attendues`) et que
// le client indexerait hors de son tableau.
func TestPlaceLibereeEstReattribuee(t *testing.T) {
	s := neuf(t, 4, 1)
	for _, j := range []int64{2, 3, 4} {
		if _, err := s.Asseoir(origine, j, "j"); err != nil {
			t.Fatalf("joueur %d refuse : %v", j, err)
		}
	}

	// Celui de la place 1 s'en va.
	if parti, _ := s.Lever(origine, 2); !parti {
		t.Fatal("le joueur 2 n'etait pas assis")
	}

	place, err := s.Asseoir(origine, 5, "cinq")
	if err != nil {
		t.Fatalf("entree refusee alors qu'une place venait de se liberer : %v", err)
	}
	if place != 1 {
		t.Errorf("place %d attribuee, attendu la place liberee 1", place)
	}
}

// TestJamaisDePlaceHorsBornes — la propriete, pas les cas.
//
// Ecrite comme une propriete parce que la liste de cas ci-dessus vieillira : ce
// qui doit rester vrai, c'est qu'AUCUNE suite d'entrees et de sorties ne fasse
// sortir une place de 0..places-1.
func TestJamaisDePlaceHorsBornes(t *testing.T) {
	for places := PlacesMin; places <= PlacesMax; places++ {
		s := neuf(t, places, 1)
		var joueur int64 = 100
		for tour := 0; tour < 200; tour++ {
			joueur++
			place, err := s.Asseoir(origine, joueur, "j")
			if err != nil {
				// Complet : on libere quelqu'un et on recommence.
				vivants := s.Vivants(origine)
				s.Lever(origine, vivants[tour%len(vivants)].PlayerID)
				continue
			}
			if place < 0 || place >= places {
				t.Fatalf("salon de %d places : place %d attribuee", places, place)
			}
		}
	}
}

// TestLesVivantsSortentDansLOrdreDesPlaces — un arrivant est ajoute en fin de
// tableau, pas a sa place.
//
// Sans le tri, la reponse qui suit une entree listerait le nouveau venu apres
// ceux dont la place est plus grande : le tableau des scores changerait d'ordre
// entre deux rafraichissements, et le client, qui indexe par la place, lirait
// une ligne pour une autre.
func TestLesVivantsSortentDansLOrdreDesPlaces(t *testing.T) {
	s := neuf(t, 4, 1)
	for _, j := range []int64{2, 3, 4} {
		if _, err := s.Asseoir(origine, j, "j"); err != nil {
			t.Fatal(err)
		}
	}
	// La place 1 se libere, puis un nouveau la reprend : il est alors le dernier
	// du tableau interne tout en portant la plus petite place libre.
	s.Lever(origine, 2)
	if p, err := s.Asseoir(origine, 9, "neuf"); err != nil || p != 1 {
		t.Fatalf("place %d, erreur %v ; attendu la place 1", p, err)
	}

	vivants := s.Vivants(origine)
	for i := 1; i < len(vivants); i++ {
		if vivants[i-1].Place >= vivants[i].Place {
			t.Fatalf("occupants rendus dans le desordre : %d avant %d",
				vivants[i-1].Place, vivants[i].Place)
		}
	}
}

func TestSalonComplet(t *testing.T) {
	s := neuf(t, 2, 1)
	if _, err := s.Asseoir(origine, 2, "deux"); err != nil {
		t.Fatalf("seconde place refusee : %v", err)
	}
	if _, err := s.Asseoir(origine, 3, "trois"); err != ErrComplet {
		t.Errorf("erreur %v, attendu ErrComplet", err)
	}
}

// TestEntreeIdempotente — rejouer sa demande ne consomme pas une seconde place.
//
// C'est le comportement normal d'un client sur un reseau reel : une reponse se
// perd, il redemande. Un service qui compterait deux entrees remplirait le salon
// de fantomes portant le meme nom.
func TestEntreeIdempotente(t *testing.T) {
	s := neuf(t, 8, 1)
	a, err := s.Asseoir(origine, 2, "deux")
	if err != nil {
		t.Fatal(err)
	}
	b, err := s.Asseoir(origine.Add(time.Second), 2, "deux")
	if err != nil {
		t.Fatal(err)
	}
	if a != b {
		t.Errorf("deux places (%d puis %d) pour un seul joueur", a, b)
	}
	if n := s.Occupes(origine.Add(time.Second)); n != 2 {
		t.Errorf("%d occupants, attendu 2", n)
	}
}

/* ========================================================================== */
/* Etats                                                                      */
/* ========================================================================== */

func TestTransitions(t *testing.T) {
	cas := []struct {
		de, vers Etat
		permise  bool
	}{
		{Attente, Manche, true},
		{Attente, Fini, true},
		{Manche, Fini, true},

		// Les interdits. Chacun a une consequence concrete si on l'ouvre :
		// revenir en attente rouvrirait des places dans une manche deja
		// arbitree, et le relais, lui, refuse toute entree apres son START.
		{Manche, Attente, false},
		{Fini, Attente, false},
		{Fini, Manche, false},

		// Un etat vers lui-meme n'est pas une transition : les appelants s'en
		// servent pour savoir s'il y a quelque chose a ecrire.
		{Attente, Attente, false},
		{Manche, Manche, false},
		{Fini, Fini, false},
	}
	for _, c := range cas {
		if got := TransitionPermise(c.de, c.vers); got != c.permise {
			t.Errorf("TransitionPermise(%s, %s) = %v, attendu %v", c.de, c.vers, got, c.permise)
		}
	}
}

// TestSeulLeProprietaireDemarre — `commence` n'est pas un bouton public.
func TestSeulLeProprietaireDemarre(t *testing.T) {
	s := neuf(t, 4, 1)
	if _, err := s.Asseoir(origine, 2, "deux"); err != nil {
		t.Fatal(err)
	}

	// Le joueur 2 le demande : rien ne bouge, mais son battement reste pris en
	// compte pour le reste. Refuser tout le battement pour un champ de trop le
	// ferait disparaitre du classement.
	if err := s.Battre(origine, 2, Battement{Commence: true, Points: 42, Vivante: true}); err != nil {
		t.Fatal(err)
	}
	if s.Etat != Attente {
		t.Errorf("un non-proprietaire a lance la manche (etat %s)", s.Etat)
	}
	if p := s.Vivants(origine)[1].Points; p != 42 {
		t.Errorf("le reste du battement a ete jete : points = %d", p)
	}

	// Le proprietaire, lui, lance.
	quand := origine.Add(5 * time.Second)
	if err := s.Battre(quand, 1, Battement{Commence: true, Vivante: true}); err != nil {
		t.Fatal(err)
	}
	if s.Etat != Manche {
		t.Fatalf("le proprietaire n'a pas lance la manche (etat %s)", s.Etat)
	}
	if !s.ChangeA.Equal(quand) {
		t.Errorf("ChangeA = %v, attendu la date de la transition %v", s.ChangeA, quand)
	}
}

// TestUnSalonLanceNeSeRejointPlus — la moitie visible de la regle 5.
func TestUnSalonLanceNeSeRejointPlus(t *testing.T) {
	for _, etat := range []Etat{Manche, Fini} {
		s := neuf(t, 8, 1)
		s.Etat = etat
		if _, err := s.Asseoir(origine, 9, "tard"); err != ErrFerme {
			t.Errorf("salon %s : erreur %v, attendu ErrFerme", etat, err)
		}
	}
}

// TestBattreUnSalonFiniEstRefuse — le client doit apprendre que c'est termine,
// et l'apprendre par la route qu'il interroge deja.
func TestBattreUnSalonFiniEstRefuse(t *testing.T) {
	s := neuf(t, 4, 1)
	s.Etat = Fini
	if err := s.Battre(origine, 1, Battement{Vivante: true}); err != ErrFerme {
		t.Errorf("erreur %v, attendu ErrFerme", err)
	}
}

func TestBattreSansPlaceEstRefuse(t *testing.T) {
	s := neuf(t, 4, 1)
	if err := s.Battre(origine, 77, Battement{Vivante: true}); err != ErrPasAssis {
		t.Errorf("erreur %v, attendu ErrPasAssis", err)
	}
}

// TestLesFusiblesNeDescendentPasSousZero — `room_cp_place.fusibles` est un
// `int32_t` et le mode ne descend jamais sous zero.
func TestLesFusiblesNeDescendentPasSousZero(t *testing.T) {
	s := neuf(t, 4, 1)
	if err := s.Battre(origine, 1, Battement{Fusibles: -3, Vivante: true}); err != nil {
		t.Fatal(err)
	}
	if f := s.Vivants(origine)[0].Fusibles; f != 0 {
		t.Errorf("fusibles = %d, attendu 0", f)
	}
}

/* ========================================================================== */
/* Propriete                                                                  */
/* ========================================================================== */

// TestLaProprieteVaAuDoyen — le proprietaire qui part ne detruit pas le salon.
//
// Ce qui est verifie ici n'est pas « quelqu'un herite » mais « c'est le PLUS
// ANCIEN qui herite », et la nuance porte tout : les places se reattribuent,
// donc le plus petit numero peut appartenir a celui qui vient d'arriver.
func TestLaProprieteVaAuDoyen(t *testing.T) {
	s := neuf(t, 4, 1)
	if _, err := s.Asseoir(origine.Add(10*time.Second), 2, "deux"); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Asseoir(origine.Add(20*time.Second), 3, "trois"); err != nil {
		t.Fatal(err)
	}

	// Le doyen (joueur 2) part, puis un nouveau prend SA place — la 1 — alors
	// qu'il vient d'arriver. C'est le piege que le critere d'anciennete evite.
	maintenant := origine.Add(30 * time.Second)
	s.Lever(maintenant, 2)
	place, err := s.Asseoir(maintenant, 4, "quatre")
	if err != nil {
		t.Fatal(err)
	}
	if place != 1 {
		t.Fatalf("place %d, attendu la place liberee 1", place)
	}

	// Maintenant le proprietaire s'en va. Le doyen restant est le joueur 3
	// (entre a +20 s), pas le joueur 4 qui occupe pourtant la place 1.
	parti, vide := s.Lever(maintenant.Add(time.Second), 1)
	if !parti || vide {
		t.Fatalf("depart du proprietaire : parti=%v vide=%v", parti, vide)
	}
	if s.Proprietaire != 3 {
		t.Errorf("le salon revient au joueur %d, attendu le doyen 3", s.Proprietaire)
	}
	if s.ProprietairePseudo != "trois" {
		t.Errorf("pseudo du proprietaire %q, attendu \"trois\"", s.ProprietairePseudo)
	}
	if s.Etat != Attente {
		t.Errorf("le salon est passe en %s alors qu'il restait du monde", s.Etat)
	}
}

// TestLaPlaceTrancheAEgaliteDHorodatage — deux entrees dans la meme
// milliseconde, ce que PostgreSQL peut rendre. Sans second critere, l'heritier
// dependrait de l'ordre des lignes, qui n'est pas garanti.
func TestLaPlaceTrancheAEgaliteDHorodatage(t *testing.T) {
	s := neuf(t, 4, 1)
	if _, err := s.Asseoir(origine, 2, "deux"); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Asseoir(origine, 3, "trois"); err != nil {
		t.Fatal(err)
	}
	// On inverse l'ordre du tableau : le resultat ne doit pas en dependre.
	s.Occupants[1], s.Occupants[2] = s.Occupants[2], s.Occupants[1]

	s.Lever(origine, 1)
	if s.Proprietaire != 2 {
		t.Errorf("le salon revient au joueur %d, attendu 2 (place 1, la plus petite)", s.Proprietaire)
	}
}

// TestSalonVideEstFini — l'invariant, quel que soit le chemin qui y mene.
func TestSalonVideEstFini(t *testing.T) {
	cas := []struct {
		nom   string
		vider func(s *Salon, quand time.Time)
	}{
		{"tout le monde part", func(s *Salon, quand time.Time) {
			s.Lever(quand, 1)
			s.Lever(quand, 2)
		}},
		{"tout le monde se tait", func(s *Salon, quand time.Time) {
			s.Faucher(quand.Add(TTLOccupant))
		}},
	}
	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			s := neuf(t, 4, 1)
			if _, err := s.Asseoir(origine, 2, "deux"); err != nil {
				t.Fatal(err)
			}
			c.vider(s, origine.Add(time.Minute))
			if s.Etat != Fini {
				t.Errorf("salon vide en etat %s, attendu %s", s.Etat, Fini)
			}
		})
	}
}

/* ========================================================================== */
/* Le faucheur                                                                */
/* ========================================================================== */

// TestFaucheur — la peremption, avec une horloge qu'on avance a la main.
//
// La table dit la seule chose qui compte : ou se trouve la frontiere. Juste
// avant `TTLOccupant`, l'occupant est la ; a `TTLOccupant` pile, il n'y est
// plus. Un test qui n'irait pas voir la frontiere laisserait passer un
// comparateur strict devenu large, ou l'inverse.
func TestFaucheur(t *testing.T) {
	cas := []struct {
		nom     string
		silence time.Duration
		reste   bool
	}{
		{"un battement manque", PeriodeClient, true},
		{"la peremption du client, 3 s", 3 * time.Second, true},
		{"juste avant la borne", TTLOccupant - time.Millisecond, true},
		{"la borne, pile", TTLOccupant, false},
		{"bien apres", time.Hour, false},
	}
	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			s := neuf(t, 4, 1)
			if _, err := s.Asseoir(origine, 2, "deux"); err != nil {
				t.Fatal(err)
			}
			// Le proprietaire continue de battre ; le joueur 2 se tait.
			maintenant := origine.Add(c.silence)
			if err := s.Battre(maintenant, 1, Battement{Vivante: true}); err != nil {
				t.Fatal(err)
			}

			retires, ferme := s.Faucher(maintenant)
			if ferme {
				t.Fatal("le salon a ete ferme alors que le proprietaire bat encore")
			}
			present := false
			for _, o := range s.Vivants(maintenant) {
				if o.PlayerID == 2 {
					present = true
				}
			}
			if present != c.reste {
				t.Errorf("apres %v de silence : present=%v, attendu %v",
					c.silence, present, c.reste)
			}
			if !c.reste && len(retires) != 1 {
				t.Errorf("%d places rendues, attendu 1", len(retires))
			}
		})
	}
}

// TestLeFaucheurTransmetLaPropriete — un proprietaire qui disparait sans le dire
// est le cas COURANT : c'est ce que fait une fenetre qu'on ferme.
func TestLeFaucheurTransmetLaPropriete(t *testing.T) {
	s := neuf(t, 4, 1)
	if _, err := s.Asseoir(origine.Add(time.Second), 2, "deux"); err != nil {
		t.Fatal(err)
	}

	maintenant := origine.Add(TTLOccupant)
	// Le joueur 2 bat encore ; le proprietaire s'est tu a `origine`.
	if err := s.Battre(maintenant, 2, Battement{Vivante: true}); err != nil {
		t.Fatal(err)
	}
	retires, ferme := s.Faucher(maintenant)
	if ferme {
		t.Fatal("salon ferme alors qu'il restait quelqu'un")
	}
	if len(retires) != 1 || retires[0] != 0 {
		t.Fatalf("places rendues %v, attendu la place 0", retires)
	}
	if s.Proprietaire != 2 {
		t.Errorf("le salon revient au joueur %d, attendu 2", s.Proprietaire)
	}
}

// TestPerime — la retention d'un salon termine ne s'applique qu'a un salon
// termine. Une manche pleine dure 315 s et n'a pas a etre effacee parce qu'elle
// dure.
func TestPerime(t *testing.T) {
	cas := []struct {
		nom  string
		etat Etat
		age  time.Duration
		veut bool
	}{
		{"une manche qui dure n'est pas perimee", Manche, time.Hour, false},
		{"un salon en attente non plus", Attente, time.Hour, false},
		{"un salon fini, frais", Fini, time.Minute, false},
		{"un salon fini, juste avant la borne", Fini, RetentionFini - time.Millisecond, false},
		{"un salon fini, a la borne", Fini, RetentionFini, true},
	}
	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			s := &Salon{Etat: c.etat, ChangeA: origine}
			if got := s.Perime(origine.Add(c.age)); got != c.veut {
				t.Errorf("Perime = %v, attendu %v", got, c.veut)
			}
		})
	}
}

// TestLesDelaisSuiventLeRythmeDuClient — les nombres, et d'ou ils viennent.
//
// Ce test ne verifie pas des valeurs choisies : il verifie les RAPPORTS qui les
// justifient. Si quelqu'un divise `TTLOccupant` par quatre, ce n'est plus le
// rythme du client qui la fixe, et il faut alors reecrire le raisonnement plutot
// que la constante.
func TestLesDelaisSuiventLeRythmeDuClient(t *testing.T) {
	// Le client publie a 4 Hz : `NS_RT_PERIOD_MS` et `NS_ARENE_PERIODE_MS`
	// valent tous deux 250 ms.
	if PeriodeClient != 250*time.Millisecond {
		t.Errorf("PeriodeClient = %v ; le client publie a 250 ms "+
			"(NS_RT_PERIOD_MS, NS_ARENE_PERIODE_MS)", PeriodeClient)
	}

	battements := TTLOccupant / PeriodeClient
	if battements < 40 {
		t.Errorf("TTLOccupant vaut %d battements du client ; en dessous de "+
			"quarante, un hoquet reseau expulse un joueur d'une manche", battements)
	}

	// Le client, lui, oublie l'etat d'un pair apres 3 s
	// (`NS_ARENE_PEREMPTION_MS`). Le serveur doit etre PLUS patient : son oubli
	// a lui coute une place, celui du client coute une ligne grise.
	const peremptionClient = 3 * time.Second
	if TTLOccupant <= peremptionClient {
		t.Errorf("TTLOccupant (%v) n'est pas plus patient que la peremption "+
			"d'affichage du client (%v)", TTLOccupant, peremptionClient)
	}

	// Une manche pleine de huit places dure 315 s (room/room_couperet.h). La
	// retention doit la depasser, sans quoi le classement final disparaitrait
	// avant qu'on ait fini de le lire.
	const manchePleine = 315 * time.Second
	if RetentionFini <= manchePleine {
		t.Errorf("RetentionFini (%v) est plus courte qu'une manche pleine (%v)",
			RetentionFini, manchePleine)
	}
}

/* ========================================================================== */
/* Camps                                                                      */
/* ========================================================================== */

// TestCampDeLaPlace — le camp vient de la PLACE, jamais du client.
//
// Deux proprietes, et la seconde est une propriete de securite du mode. En
// individuel, le camp EST la place : `room_cp_place.camp` est documente
// « 0..7 ; en individuel, camp = place », et la respecter evite que le
// classement live regroupe huit joueurs solitaires dans un camp 0 commun.
//
// A deux camps, l'alternance suit la place — 0-1-0-1, la regle de
// `couperet_ouvrir`. Et surtout, RIEN de ce que le client envoie n'entre : le
// Couperet classe les camps et la lame descend dans le camp dernier, donc un
// camp declare par le joueur est un camp qu'il change pour rejoindre celui qui
// mene, a l'abri de la lame.
func TestCampDeLaPlace(t *testing.T) {
	cas := []struct {
		nom          string
		camps, place int
		veut         int
	}{
		{"individuel : le camp est la place", 1, 5, 5},
		{"individuel : place 0", 1, 0, 0},
		{"equipes : les places paires tiennent le camp 0", 2, 4, 0},
		{"equipes : les places impaires tiennent le camp 1", 2, 5, 1},
		{"equipes : l'alternance commence a la place 0", 2, 0, 0},
		{"equipes : et pas par blocs", 2, 1, 1},
	}
	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			if got := CampDeLaPlace(c.camps, c.place); got != c.veut {
				t.Errorf("CampDeLaPlace(%d, %d) = %d, attendu %d",
					c.camps, c.place, got, c.veut)
			}
		})
	}
}

// TestCampIgnoreLeBattement — le battement ne peut pas changer de camp.
//
// C'est le test qui garde la propriete : il echouerait si quelqu'un
// rebranchait un jour `b.Camp` sur `o.Camp` pour « respecter ce que le client
// annonce ». Un salon a deux camps, un joueur a la place 1 — donc camp 1 — qui
// bat en pretendant le camp 0, celui de l'adversaire qui mene.
func TestCampIgnoreLeBattement(t *testing.T) {
	t0 := time.Date(2026, 8, 31, 21, 0, 0, 0, time.UTC)
	s := &Salon{
		Code: "KMPQRS", Places: 4, Camps: 2, Etat: Attente,
		Proprietaire: 1, CreeA: t0, ChangeA: t0,
	}
	if _, err := s.Asseoir(t0, 1, "Zoe"); err != nil {
		t.Fatalf("Asseoir hote : %v", err)
	}
	if _, err := s.Asseoir(t0, 2, "Malik"); err != nil {
		t.Fatalf("Asseoir invite : %v", err)
	}

	avant := s.Occupants[1].Camp
	if avant != 1 {
		t.Fatalf("la place 1 doit tenir le camp 1, pas %d", avant)
	}
	if err := s.Battre(t0, 2, Battement{Points: 999, Camp: 0, Vivante: true}); err != nil {
		t.Fatalf("Battre : %v", err)
	}
	if s.Occupants[1].Camp != 1 {
		t.Errorf("le camp a suivi le battement : %d, attendu 1 — un joueur "+
			"pourrait rejoindre le camp qui mene et echapper a la lame",
			s.Occupants[1].Camp)
	}
	if s.Occupants[1].Points != 999 {
		t.Errorf("le reste du battement doit passer : points = %d",
			s.Occupants[1].Points)
	}
}

// TestFaucheGardeLeTableauFinal — une manche qui se vide garde ses lignes.
//
// Le defaut, mesure sur la page web avant correction : « MANCHE TERMINEE »
// au-dessus de « Personne pour l'instant. » Un client cesse de battre quand la
// manche s'arrete — il n'a plus rien a publier — donc le faucheur retirait les
// occupants dans les secondes suivantes, et le tableau se vidait sous les yeux
// de qui venait voir qui avait gagne.
//
// L'attente ne beneficie pas de cette regle, et c'est voulu : un salon que tout
// le monde a quitte avant de commencer n'a rien a montrer.
func TestFaucheGardeLeTableauFinal(t *testing.T) {
	t0 := time.Date(2026, 8, 31, 22, 0, 0, 0, time.UTC)
	tard := t0.Add(TTLOccupant + time.Second)

	enManche := func() *Salon {
		s := &Salon{Code: "KMPQRS", Places: 4, Camps: 2, Etat: Attente,
			Proprietaire: 1, CreeA: t0, ChangeA: t0}
		if _, err := s.Asseoir(t0, 1, "Zoe"); err != nil {
			t.Fatalf("Asseoir : %v", err)
		}
		if _, err := s.Asseoir(t0, 2, "Malik"); err != nil {
			t.Fatalf("Asseoir : %v", err)
		}
		if err := s.Battre(t0, 1, Battement{Points: 500, Vivante: true, Commence: true}); err != nil {
			t.Fatalf("Battre : %v", err)
		}
		if err := s.Battre(t0, 2, Battement{Points: 340, Vivante: true}); err != nil {
			t.Fatalf("Battre : %v", err)
		}
		return s
	}

	s := enManche()
	if s.Etat != Manche {
		t.Fatalf("le salon devait etre en manche, il est %q", s.Etat)
	}
	// AUCUNE place rendue, et le salon ferme. `retires` est la liste que
	// l'appelant efface EN BASE : la rendre non vide gardait le tableau en
	// memoire et le supprimait dans la foulee, si bien que la page web
	// affichait quand meme un classement final vide. Mesure ainsi contre un
	// vrai PostgreSQL, apres que la version precedente de ce test soit passee.
	retires, ferme := s.Faucher(tard)
	if len(retires) != 0 {
		t.Fatalf("aucune place ne doit etre retiree d'une manche finie : %v", retires)
	}
	if !ferme {
		t.Fatalf("le salon devait se fermer")
	}
	if s.Etat != Fini {
		t.Errorf("etat = %q, attendu %q", s.Etat, Fini)
	}
	if len(s.Occupants) != 2 {
		t.Fatalf("le tableau final a ete efface : %d ligne(s), attendu 2", len(s.Occupants))
	}
	if s.Occupants[0].Points != 500 || s.Occupants[1].Points != 340 {
		t.Errorf("les points ont bouge : %d et %d",
			s.Occupants[0].Points, s.Occupants[1].Points)
	}

	// Et une deuxieme fauche sur un salon deja fini ne touche plus rien.
	if r, f := s.Faucher(tard.Add(time.Hour)); r != nil || f {
		t.Errorf("un salon fini se fauchait encore : %v, %v", r, f)
	}
	if len(s.Occupants) != 2 {
		t.Errorf("la deuxieme fauche a vide le tableau : %d", len(s.Occupants))
	}

	// En ATTENTE, au contraire, il n'y a rien a garder.
	a := &Salon{Code: "KMPQRT", Places: 4, Camps: 1, Etat: Attente,
		Proprietaire: 1, CreeA: t0, ChangeA: t0}
	if _, err := a.Asseoir(t0, 1, "Zoe"); err != nil {
		t.Fatalf("Asseoir : %v", err)
	}
	if _, ferme := a.Faucher(tard); !ferme {
		t.Errorf("un salon en attente vide doit se fermer")
	}
	if len(a.Occupants) != 0 {
		t.Errorf("un salon en attente ne garde personne : %d", len(a.Occupants))
	}
}

/* ========================================================================== */
/* Le code d'acces                                                            */
/* ========================================================================== */

// TestAlphabetSansAmbiguite — ce qu'un code ne doit jamais contenir.
//
// Un code se dicte a l'oral et se recopie a la main : les couples que l'oeil
// confond y coutent plus cher que la taille de l'espace qu'ils font perdre.
func TestAlphabetSansAmbiguite(t *testing.T) {
	for _, interdit := range []byte{'O', '0', 'I', '1', 'l'} {
		if strings.IndexByte(Alphabet, interdit) >= 0 {
			t.Errorf("l'alphabet contient %q, qui se confond avec un autre symbole", interdit)
		}
	}
	vus := map[byte]bool{}
	for i := 0; i < len(Alphabet); i++ {
		c := Alphabet[i]
		if vus[c] {
			t.Errorf("le symbole %q apparait deux fois : le tirage n'est plus uniforme", c)
		}
		vus[c] = true
		if !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') {
			t.Errorf("symbole %q hors des capitales et des chiffres", c)
		}
	}
}

// TestEspaceDesCodes — le chiffre, CALCULE.
//
// Il est affiche meme quand le test passe : c'est le seul endroit ou l'on peut
// lire combien de codes l'alphabet retenu permet, et c'est ce nombre qui justifie
// la limitation de debit sur l'entree (voir internal/api).
func TestEspaceDesCodes(t *testing.T) {
	n := EspaceDesCodes()
	attendu := int64(1)
	for i := 0; i < CodeLong; i++ {
		attendu *= int64(len(Alphabet))
	}
	if n != attendu {
		t.Fatalf("EspaceDesCodes() = %d, attendu %d", n, attendu)
	}
	t.Logf("%d symboles ^ %d positions = %d codes", len(Alphabet), CodeLong, n)

	// Un code doit rester devinable difficilement, meme freine par la limitation
	// de debit : en dessous de cent millions, six caracteres ne suffiraient plus.
	if n < 100_000_000 {
		t.Errorf("seulement %d codes : l'alphabet a trop maigri pour six caracteres", n)
	}
}

func TestTirerCodeRespecteSaForme(t *testing.T) {
	for i := 0; i < 1000; i++ {
		code, err := TirerCode()
		if err != nil {
			t.Fatal(err)
		}
		if len(code) != CodeLong {
			t.Fatalf("code %q de longueur %d, attendu %d", code, len(code), CodeLong)
		}
		for j := 0; j < len(code); j++ {
			if strings.IndexByte(Alphabet, code[j]) < 0 {
				t.Fatalf("code %q : symbole %q hors alphabet", code, code[j])
			}
		}
		if NormaliserCode(code) != code {
			t.Fatalf("un code tire (%q) n'est pas accepte par NormaliserCode", code)
		}
	}
}

// TestTirageUniforme — LE BIAIS, MESURE.
//
// POURQUOI CE TEST EXISTE. `octet % taille` est le reflexe, et il est faux des
// que 256 n'est pas un multiple de la taille de l'alphabet. Avec trente
// symboles : 256 = 8 x 30 + 16, donc les seize premiers sortent 9 fois sur 256
// et les quatorze autres 8 — un ecart de +5,5 % et -6,2 % par rapport a
// l'uniforme. Personne ne le verrait, les codes resteraient des codes, et
// l'espace effectif retrecirait en silence.
//
// LE SEUIL EST CALCULE, PAS CHOISI. Sur N tirages de six caracteres, chaque
// symbole est attendu N x 6 / 32 fois, avec un ecart-type de
// sqrt(N x 6 x p x (1-p)) ou p = 1/32. Pour N = 200 000, cela fait 37 500
// attendus et 190,6 d'ecart-type, soit 0,51 % en relatif. Le seuil global est
// pose a 3 %, c'est-a-dire pres de SIX ecarts-types : la probabilite qu'un
// tirage honnete le franchisse est de l'ordre de 10^-7 pour l'ensemble des
// trente-deux symboles. Il attrape donc le biais decrit ci-dessus, qui vaut
// deux fois le seuil.
//
// Le controle par POSITION a son propre seuil, parce qu'il a six fois moins
// d'echantillons : 6 250 attendus, 77,8 d'ecart-type, 1,24 % en relatif. 8 %
// y valent plus de six ecarts-types.
//
// MESURE, sur cinq executions de ce test : l'ecart maximal observe est de
// 1,07 a 1,25 % en global (deux a deux ecarts-types et demi) et de 3,17 a
// 4,16 % par position (deux et demi a trois et demi). C'est exactement ce
// qu'on attend d'un maximum pris sur 32 et sur 192 cellules, et les seuils
// laissent respectivement deux fois et deux fois la marge.
func TestTirageUniforme(t *testing.T) {
	if testing.Short() {
		t.Skip("tirage de masse : passe en mode court")
	}
	const n = 200_000

	global := map[byte]int{}
	parPosition := make([]map[byte]int, CodeLong)
	for i := range parPosition {
		parPosition[i] = map[byte]int{}
	}

	for i := 0; i < n; i++ {
		code, err := TirerCode()
		if err != nil {
			t.Fatal(err)
		}
		for j := 0; j < CodeLong; j++ {
			global[code[j]]++
			parPosition[j][code[j]]++
		}
	}

	if len(global) != len(Alphabet) {
		t.Errorf("%d symboles distincts tires sur %d : certains ne sortent jamais",
			len(global), len(Alphabet))
	}

	attenduGlobal := float64(n*CodeLong) / float64(len(Alphabet))
	pireGlobal, pireSymbole := 0.0, byte(0)
	for i := 0; i < len(Alphabet); i++ {
		c := Alphabet[i]
		ecart := math.Abs(float64(global[c])-attenduGlobal) / attenduGlobal
		if ecart > pireGlobal {
			pireGlobal, pireSymbole = ecart, c
		}
	}
	t.Logf("%d tirages, %d symboles : ecart relatif maximal %.3f %% (sur %q), "+
		"seuil %.1f %%", n, len(Alphabet), pireGlobal*100, pireSymbole, 3.0)
	if pireGlobal > 0.03 {
		t.Errorf("ecart relatif de %.2f %% sur le symbole %q : le tirage est biaise",
			pireGlobal*100, pireSymbole)
	}

	attenduPosition := float64(n) / float64(len(Alphabet))
	pirePosition := 0.0
	for j := 0; j < CodeLong; j++ {
		for i := 0; i < len(Alphabet); i++ {
			ecart := math.Abs(float64(parPosition[j][Alphabet[i]])-attenduPosition) / attenduPosition
			if ecart > pirePosition {
				pirePosition = ecart
			}
		}
	}
	t.Logf("par position : ecart relatif maximal %.3f %%, seuil %.1f %%", pirePosition*100, 8.0)
	if pirePosition > 0.08 {
		t.Errorf("ecart relatif de %.2f %% sur une position : le tirage y est biaise",
			pirePosition*100)
	}
}

// TestSeuilDeRejetEstUnMultiple — la propriete qui rend le tirage uniforme.
//
// Elle survit a un changement d'alphabet, contrairement au « 256 est un multiple
// de 32 » d'aujourd'hui : c'est justement ce qui doit rester vrai le jour ou
// quelqu'un retire un symbole de plus.
func TestSeuilDeRejetEstUnMultiple(t *testing.T) {
	if seuilRejet%len(Alphabet) != 0 {
		t.Fatalf("seuilRejet = %d n'est pas un multiple de %d", seuilRejet, len(Alphabet))
	}
	if seuilRejet <= 0 || seuilRejet > 256 {
		t.Fatalf("seuilRejet = %d hors de 1..256", seuilRejet)
	}
	if 256-seuilRejet >= len(Alphabet) {
		t.Fatalf("seuilRejet = %d rejette %d octets sur 256, soit plus d'un tour "+
			"d'alphabet : le calcul est faux", seuilRejet, 256-seuilRejet)
	}
	t.Logf("alphabet de %d symboles : seuil %d, soit %d octets rejetes sur 256",
		len(Alphabet), seuilRejet, 256-seuilRejet)
}

// TestTirerRelaisResteDansUnEntierSigne — soixante-trois bits, jamais soixante-
// quatre.
//
// Ce nombre traverse un `bigint` PostgreSQL, qui est signe, puis l'analyseur
// JSON du client, qui lit ses entiers avec `SDL_strtoll` (`ns_json_get_i64`,
// engine/core/ns_json.c) — signe lui aussi. Un 64e bit deborderait des deux
// cotes, et le symptome serait un salon qu'on rejoint mais ou l'on ne trouve
// personne.
func TestTirerRelaisResteDansUnEntierSigne(t *testing.T) {
	vus := map[int64]bool{}
	for i := 0; i < 5000; i++ {
		v, err := TirerRelais()
		if err != nil {
			t.Fatal(err)
		}
		if v < 0 {
			t.Fatalf("identifiant negatif : %d", v)
		}
		if vus[v] {
			t.Fatalf("identifiant %d tire deux fois en %d essais", v, i+1)
		}
		vus[v] = true
	}
}

// TestLeRelaisNEstPasDeriveDuCode — la propriete de securite du module.
//
// Elle ne peut pas se prouver par un test, et il faut le dire : ce qu'on
// verifie ici est qu'un meme code ne donne pas deux fois le meme identifiant,
// donc que le second n'est pas une fonction du premier. C'est une condition
// necessaire, pas suffisante. La garantie reelle est que `TirerRelais` ne
// regarde PAS le code, et cela se lit dans sa signature — elle ne le prend pas.
func TestLeRelaisNEstPasDeriveDuCode(t *testing.T) {
	a, err := TirerRelais()
	if err != nil {
		t.Fatal(err)
	}
	b, err := TirerRelais()
	if err != nil {
		t.Fatal(err)
	}
	if a == b {
		t.Fatal("deux tirages identiques : l'identifiant de relais est previsible")
	}
}

func TestNormaliserCode(t *testing.T) {
	cas := []struct {
		nom, entree, veut string
	}{
		{"un code tel quel", "K7M3QP", "K7M3QP"},
		{"la casse est rattrapee : on le recopie a la main", "k7m3qp", "K7M3QP"},
		{"les blancs autour sont rognes", "  K7M3QP  ", "K7M3QP"},

		{"vide", "", ""},
		{"trop court", "K7M3Q", ""},
		{"trop long", "K7M3QPX", ""},
		{"un symbole hors alphabet : le O ambigu", "K7M3QO", ""},
		{"un symbole hors alphabet : le 1 ambigu", "K7M31P", ""},
		{"de la ponctuation", "K7M3Q-", ""},
		{"un accent, qui ne tient meme pas en un octet", "K7M3QÉ", ""},
		{"une tentative d'injection", "' OR 1", ""},
	}
	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			if got := NormaliserCode(c.entree); got != c.veut {
				t.Errorf("NormaliserCode(%q) = %q, attendu %q", c.entree, got, c.veut)
			}
		})
	}
}

/* ========================================================================== */
/* Validation                                                                 */
/* ========================================================================== */

func TestValider(t *testing.T) {
	cas := []struct {
		nom           string
		salon         string
		places, camps int
		veut          error
	}{
		{"un salon ordinaire", "Chez Nine", 8, 1, nil},
		{"deux places, le minimum", "Duo", 2, 2, nil},
		{"deux camps", "Equipes", 8, 2, nil},

		{"sans nom", "", 8, 1, ErrNom},
		{"un nom de blancs", "   ", 8, 1, ErrNom},
		{"un nom trop long", strings.Repeat("x", NomMax+1), 8, 1, ErrNom},

		// Une place de moins qu'un duel n'est pas un salon, et une de plus que
		// huit est une place que le relais refusera et que le client indexera
		// hors de son tableau.
		{"une seule place", "Seul", 1, 1, ErrBornes},
		{"neuf places", "Trop", 9, 1, ErrBornes},
		{"zero camp", "Sans", 8, 0, ErrBornes},
		{"trois camps", "Trois", 8, 3, ErrBornes},
	}
	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			if got := Valider(c.salon, c.places, c.camps); got != c.veut {
				t.Errorf("Valider(%q, %d, %d) = %v, attendu %v",
					c.salon, c.places, c.camps, got, c.veut)
			}
		})
	}
}

// TestLesBornesSuiventLeRelaisEtLeMode — elles ne sont pas choisies ici.
func TestLesBornesSuiventLeRelaisEtLeMode(t *testing.T) {
	// `placesMin`/`placesMax` (internal/duel/relay.go) et `ROOM_CP_MAX_PLACES`
	// (room/room_couperet.h). Un salon que le relais refuserait de servir n'a
	// aucune raison d'exister.
	if PlacesMin != 2 || PlacesMax != 8 {
		t.Errorf("places %d..%d ; le relais sert 2..8 et le mode en montre 8",
			PlacesMin, PlacesMax)
	}
}

/* ========================================================================== */
/* L'accord avec le schema                                                    */
/* ========================================================================== */

// classeSQL rend la classe de caracteres POSIX qui decrit exactement
// l'alphabet, en compactant les suites consecutives.
func classeSQL(alphabet string) string {
	var b strings.Builder
	b.WriteByte('[')
	for i := 0; i < len(alphabet); {
		j := i
		for j+1 < len(alphabet) && alphabet[j+1] == alphabet[j]+1 {
			j++
		}
		switch j - i {
		case 0:
			b.WriteByte(alphabet[i])
		case 1:
			b.WriteByte(alphabet[i])
			b.WriteByte(alphabet[j])
		default:
			b.WriteByte(alphabet[i])
			b.WriteByte('-')
			b.WriteByte(alphabet[j])
		}
		i = j + 1
	}
	b.WriteByte(']')
	return b.String()
}

// TestLaMigrationDecritLeMemeAlphabet — la contrainte SQL et la constante Go
// disent la meme chose, ou la construction echoue.
//
// C'EST LE GENRE DE DIVERGENCE QUI NE SE VOIT PAS. La contrainte est une
// ceinture : elle ne se declenche que si le code applicatif a deja laisse passer
// quelque chose. Si l'alphabet Go gagne un symbole que le SQL refuse, tout
// marche jusqu'au tirage qui contient ce symbole — et le joueur recoit une
// erreur interne pour un code parfaitement legitime, une fois sur trente.
func TestLaMigrationDecritLeMemeAlphabet(t *testing.T) {
	sql, err := migrations.FS.ReadFile("0005_salons.sql")
	if err != nil {
		t.Fatalf("migration des salons introuvable : %v", err)
	}
	attendu := "'^" + classeSQL(Alphabet) + "{" + itoa(CodeLong) + "}$'"
	if !strings.Contains(string(sql), attendu) {
		t.Errorf("0005_salons.sql ne contient pas la contrainte %s, "+
			"qui est la forme exacte de salons.Alphabet et salons.CodeLong", attendu)
	}
}

// itoa, plutot que strconv pour un seul appel dans un test.
func itoa(n int) string {
	if n < 10 {
		return string(rune('0' + n))
	}
	return itoa(n/10) + string(rune('0'+n%10))
}
