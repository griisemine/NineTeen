// Package salons — les REGLES d'un salon du Couperet, sans base ni reseau.
//
// CE QUE CE PAQUET REPARE
// -----------------------
// Le mode competitif se joue en ligne depuis qu'il existe, et le seul moyen de
// s'y retrouver a plusieurs etait de convenir HORS BANDE d'un numero de salon
// et d'un numero de place, puis de les taper en ligne de commande :
// « --couperet-en-ligne=hote:port,salon,place,places » (room/main.c). Deux
// joueurs qui choisissent la meme place ne se voient jamais ; un joueur qui se
// trompe de nombre de places est refuse par le relais sans savoir pourquoi. Ce
// n'est pas un mode difficile a lancer, c'est un mode qu'on ne peut pas lancer
// sans se parler d'abord ailleurs.
//
// Un salon est le rendez-vous qui manquait : un code de six caracteres qu'on se
// dit a voix haute, et le serveur attribue les places.
//
// POURQUOI CE PAQUET NE CONNAIT NI SQL NI HTTP
// --------------------------------------------
// Tout ce qui decide ici — quelle place est libre, quel etat suit lequel, qui
// herite du salon, qui a disparu — se teste sans base et sans socket, avec une
// horloge passee en parametre. C'est la structure que `internal/runs` a deja :
// la regle d'un cote, le transport de l'autre. Le magasin charge un salon,
// appelle ces methodes, et reecrit ce qu'elles ont change ; il ne rejuge rien.
//
// LA PROPRIETE DE SECURITE DU MODULE
// ----------------------------------
// LE CODE D'ACCES N'EST PAS LA CAPACITE.
//
// Le code fait six caracteres, parce qu'il est tape a la main et souvent lu a
// voix haute : c'est une contrainte d'ergonomie, pas un budget de securite. Six
// caracteres sur trente-deux symboles font 2^30 combinaisons — assez pour que
// deviner un salon precis soit sans espoir a la cadence que la limitation de
// debit laisse passer, mais bien trop peu pour tenir lieu de secret durable.
//
// Il ne sert donc JAMAIS d'identifiant de session sur le relais. Le relais, lui,
// n'a aucune notion de compte : `rejoindre` (internal/duel/relay.go) apparie sur
// le seul identifiant numerique presente dans le JOIN, et quiconque le connait
// entre dans la manche. Cet identifiant-la est tire avec `crypto/rand` a la
// creation du salon (voir `TirerRelais`), il n'apparait jamais dans une liste ni
// dans le classement live, et il n'est rendu qu'a un joueur AUTHENTIFIE qui a
// obtenu une place.
//
// Autrement dit : connaitre le code permet de DEMANDER une place ; c'est le
// serveur qui decide, et lui seul livre de quoi se brancher.
package salons

import (
	"crypto/rand"
	"errors"
	"fmt"
	"slices"
	"strings"
	"time"
)

/* ========================================================================== */
/* Bornes                                                                     */
/* ========================================================================== */

const (
	// Les bornes d'un salon. Elles ne sont PAS choisies ici : ce sont celles du
	// relais (`placesMin` et `placesMax`, internal/duel/relay.go) et celles du
	// mode (`ROOM_CP_MAX_PLACES`, room/room_couperet.h). Un salon que le relais
	// refuserait de servir n'aurait aucune raison d'exister, et un salon plus
	// grand que ce que le mode sait afficher non plus.
	PlacesMin = 2
	PlacesMax = 8

	// Les camps. Un seul camp veut dire « chacun pour soi » — c'est le mode
	// individuel, ou `room_cp_place.camp` vaut la place. Deux, ce sont les
	// equipes. Le mode n'en connait pas d'autre.
	CampsMin = 1
	CampsMax = 2

	// La longueur d'un nom de salon, en runes. Meme ordre que les 24 d'un
	// pseudo : c'est une etiquette dans une liste, pas un texte.
	NomMax = 32

	// La largeur du nom de borne publie par un battement, alignee sur
	// `ROOM_CP_JEU` (room/room_couperet.h).
	BorneMax = 24

	// La longueur du code d'acces. Voir l'en-tete du fichier pour ce que six
	// caracteres achetent et ce qu'ils n'achetent pas.
	CodeLong = 6
)

// Alphabet — les symboles d'un code de salon.
//
// Un code se tape a la main et se lit a voix haute : les couples que l'oeil
// confond y coutent plus cher que la taille de l'espace qu'ils font perdre. Sont
// retires O et 0, puis I et 1. Le L majuscule reste : ce qu'on lui confond est
// le chiffre 1, qui vient d'etre retire, et le l minuscule ne peut pas
// apparaitre puisque l'alphabet est en capitales.
//
// 24 lettres et 8 chiffres, soit 32 symboles. `EspaceDesCodes` en tire le
// nombre de codes possibles — CALCULE, jamais recopie a la main.
const Alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

// seuilRejet — le plus grand multiple de la taille de l'alphabet qui tienne
// dans un octet.
//
// C'est ce qui rend le tirage UNIFORME. Sans lui, `octet % taille` favorise les
// premiers symboles des que 256 n'est pas un multiple de la taille : avec 30
// symboles, les 16 premiers sortiraient 9 fois sur 256 et les 14 autres 8, soit
// 12 % de plus pour la moitie de l'alphabet.
//
// Avec les 32 symboles d'aujourd'hui, 256 est un multiple exact : le seuil vaut
// 256 et AUCUN octet n'est jamais rejete. Le rejet est ecrit quand meme, parce
// qu'il ne coute qu'une comparaison et qu'il est la seule chose qui garde le
// tirage uniforme le jour ou l'on retire un symbole de plus.
const seuilRejet = 256 - 256%len(Alphabet)

/* ========================================================================== */
/* Rythme et peremption                                                       */
/* ========================================================================== */

const (
	// PeriodeClient — le battement du client, tel qu'il est ecrit chez lui.
	//
	// 250 ms des deux cotes : `NS_RT_PERIOD_MS` pour la presence
	// (engine/net/ns_realtime.h) et `NS_ARENE_PERIODE_MS` pour l'etat d'arene
	// (engine/net/ns_arene.h). Un client n'a aucune raison de battre plus vite
	// vers ce service-ci : il n'aurait rien de plus frais a dire, puisque ce
	// qu'il publie est exactement ce qu'il vient de publier au relais.
	PeriodeClient = 250 * time.Millisecond

	// TTLOccupant — au bout de combien de silence une place est rendue.
	//
	// QUARANTE-HUIT BATTEMENTS, soit douze secondes. Le chiffre vient de trois
	// mesures et d'une asymetrie :
	//
	//   - le client bat au plus a 4 Hz (`PeriodeClient` ci-dessus) ;
	//   - le client lui-meme oublie l'etat d'un pair apres 3 s, soit DOUZE
	//     battements manques (`NS_ARENE_PEREMPTION_MS`) ;
	//   - la presence, qui emprunte le meme transport HTTP chez le meme client,
	//     tient 12 s (`presenceTTL`, internal/api).
	//
	// L'asymetrie est voulue : l'affichage du client PARDONNE — il grise une
	// ligne et la repeint des qu'une trame revient — tandis que le serveur
	// EXPULSE, et une expulsion ne se repeint pas. Ce qui coute une ligne grise
	// chez l'un coute une manche chez l'autre, donc le serveur doit etre le plus
	// lent des deux. Quatre fois plus lent que la peremption d'affichage, et
	// exactement aussi patient que la presence, qui passe par la meme socket
	// rouverte a chaque battement.
	TTLOccupant = 48 * PeriodeClient

	// RetentionFini — combien de temps un salon termine reste lisible.
	//
	// Le classement live est ce qu'on regarde a la FIN d'une manche : la fermer
	// et l'effacer dans le meme geste ferait disparaitre le resultat au moment
	// exact ou on le cherche. Une manche pleine de huit places dure 315 s (le
	// chiffre est celui de room/room_couperet.h) ; dix minutes laissent donc le
	// temps d'arriver apres la fin et de lire encore.
	RetentionFini = 10 * time.Minute
)

/* ========================================================================== */
/* Etats                                                                      */
/* ========================================================================== */

// Etat — ou en est un salon. Les trois valeurs sont celles que le client lit
// dans le champ « etat » ; elles sont ecrites en toutes lettres plutot qu'en
// nombres pour qu'un journal ou une reponse se lisent sans table de conversion.
type Etat string

const (
	Attente Etat = "attente" // il se remplit ; rien ne court
	Manche  Etat = "manche"  // le couperet est lance
	Fini    Etat = "fini"    // termine, ou vide
)

// TransitionPermise dit si un etat peut succeder a un autre.
//
// Le graphe est une FLECHE, jamais un cycle : attente -> manche -> fini. Rien
// ne revient en arriere, et c'est ce qui rend « fini » definitif. Un salon qui
// pourrait repasser en attente reouvrirait des places dans une manche deja
// arbitree, et le relais, lui, refuse une entree apres son START
// (`s.demarre`, internal/duel/relay.go) : le service annoncerait une place que
// le transport refuserait ensuite, sans que rien ne dise laquelle des deux
// moities a tort.
//
// « attente -> fini » est permis parce qu'un salon peut mourir avant de courir :
// tout le monde est parti, ou le proprietaire l'a ferme.
//
// Un etat vers LUI-MEME n'est pas une transition et rend faux : les appelants
// s'en servent pour savoir s'il y a quelque chose a ecrire.
func TransitionPermise(de, vers Etat) bool {
	switch de {
	case Attente:
		return vers == Manche || vers == Fini
	case Manche:
		return vers == Fini
	default:
		return false
	}
}

/* ========================================================================== */
/* Erreurs                                                                    */
/* ========================================================================== */

var (
	ErrComplet         = errors.New("salon complet")
	ErrFerme           = errors.New("salon ferme")
	ErrPasProprietaire = errors.New("seul le proprietaire peut faire cela")
	ErrPasAssis        = errors.New("place non occupee")
	ErrTropDeSalons    = errors.New("trop de salons ouverts par ce joueur")
	ErrBornes          = errors.New("places ou camps hors bornes")
	ErrNom             = errors.New("nom de salon vide")
)

/* ========================================================================== */
/* Le salon                                                                   */
/* ========================================================================== */

// Occupant — une place prise, et ce que son joueur publie.
//
// `Points` et `Fusibles` sont des `int32` parce que c'est le type du mode
// (`room_cp_place`, room/room_couperet.h) : les recopier plus larges ici
// laisserait entrer des valeurs que le client ne saurait pas relire, et plus
// etroits perdrait des points qu'il compte vraiment.
type Occupant struct {
	Place    int
	PlayerID int64
	Pseudo   string
	Camp     int
	Points   int32
	Fusibles int32
	Vivante  bool
	Borne    string
	// EntreA sert a l'heritage du salon : le plus ancien reprend la main. Voir
	// `Lever`.
	EntreA time.Time
	BattuA time.Time
}

// Salon — l'etat complet d'un rendez-vous.
type Salon struct {
	Code   string
	Nom    string
	Places int
	Camps  int
	Prive  bool
	Etat   Etat

	// Relais — l'identifiant de SESSION sur le relais de duel.
	//
	// Il n'a rien a voir avec `Code`, et c'est tout le sujet : voir l'en-tete du
	// fichier. Ce champ ne doit sortir que vers un joueur authentifie et assis.
	Relais int64

	Proprietaire       int64
	ProprietairePseudo string

	CreeA time.Time
	// ChangeA — la date de la derniere TRANSITION D'ETAT, pas celle de la
	// creation. C'est ce qui permet de dire « en attente depuis 40 s » a un
	// joueur qui choisit un salon, et « manche en cours depuis 2 min » a un
	// spectateur, avec un seul champ et sans que l'un des deux mente.
	ChangeA time.Time

	Occupants []Occupant
}

// frais dit si un occupant a battu assez recemment pour compter.
//
// LE SEUL ENDROIT OU LA PEREMPTION EST DECIDEE. Les lectures s'en servent pour
// n'afficher que les vivants, `Asseoir` pour savoir qu'une place est rendue, et
// `Faucher` pour savoir quoi jeter : trois usages, une regle.
func frais(o Occupant, maintenant time.Time) bool {
	return maintenant.Sub(o.BattuA) < TTLOccupant
}

// Vivants rend les occupants qui comptent encore, TOUJOURS dans l'ordre des
// places.
//
// L'ordre est impose ici et non laisse a la requete, parce qu'il ne vient pas
// tous du meme endroit : la base rend les lignes triees, mais un arrivant est
// ajoute en fin de tableau et sortirait donc apres ceux dont la place est plus
// grande. Un tableau de scores qui change d'ordre entre deux rafraichissements
// est illisible, et le client indexe ses propres tableaux par la place.
func (s *Salon) Vivants(maintenant time.Time) []Occupant {
	out := make([]Occupant, 0, len(s.Occupants))
	for _, o := range s.Occupants {
		if frais(o, maintenant) {
			out = append(out, o)
		}
	}
	slices.SortFunc(out, func(a, b Occupant) int { return a.Place - b.Place })
	return out
}

// Tableau rend les lignes A AFFICHER, dans l'ordre des places.
//
// C'est `Vivants` tant que la manche court, et TOUT LE MONDE une fois qu'elle
// est finie. Deux methodes et non une, parce que ce sont deux questions
// differentes : `Vivants` sert a decider — qui herite, quelle place est libre,
// le salon est-il mort — et la fraicheur y est la bonne reponse. Le tableau,
// lui, se lit apres coup, et un client cesse de battre des que la manche
// s'arrete. Filtrer sur la fraicheur donnait donc « MANCHE TERMINEE » au-dessus
// d'un tableau vide, mesure sur la page web : le classement final disparaissait
// dans les douze secondes qui suivaient la fin.
func (s *Salon) Tableau(maintenant time.Time) []Occupant {
	if s.Etat != Fini {
		return s.Vivants(maintenant)
	}
	out := make([]Occupant, len(s.Occupants))
	copy(out, s.Occupants)
	slices.SortFunc(out, func(a, b Occupant) int { return a.Place - b.Place })
	return out
}

// Occupes compte les places reellement tenues.
func (s *Salon) Occupes(maintenant time.Time) int {
	n := 0
	for _, o := range s.Occupants {
		if frais(o, maintenant) {
			n++
		}
	}
	return n
}

// Ouvert dit si le salon accepte encore une entree.
//
// C'est aussi le predicat de la LISTE PUBLIQUE : ne lister que ce qu'on peut
// rejoindre, sans quoi la liste propose des salons dont chaque clic est un 410.
func (s *Salon) Ouvert() bool { return s.Etat == Attente }

// PlaceLibre rend la plus petite place disponible.
//
// LA PLUS PETITE, et non la suivante : une place liberee doit etre
// REATTRIBUABLE. Le client indexe des tableaux de `ROOM_CP_MAX_PLACES` entrees
// et le relais refuse toute place hors du salon (`place >= attendues`), donc
// distribuer 0, 1, 2 puis 3 apres le depart du 1 laisserait un trou et
// finirait par sortir des bornes. On rend donc toujours un indice de
// 0..Places-1, et jamais davantage.
func (s *Salon) PlaceLibre(maintenant time.Time) (int, bool) {
	prise := make([]bool, s.Places)
	for _, o := range s.Occupants {
		if o.Place >= 0 && o.Place < s.Places && frais(o, maintenant) {
			prise[o.Place] = true
		}
	}
	for i, p := range prise {
		if !p {
			return i, true
		}
	}
	return 0, false
}

// indexDe rend l'indice interne d'un joueur assis, ou -1.
func (s *Salon) indexDe(playerID int64) int {
	for i := range s.Occupants {
		if s.Occupants[i].PlayerID == playerID {
			return i
		}
	}
	return -1
}

// PlaceDe rend la place d'un joueur assis.
func (s *Salon) PlaceDe(playerID int64) (int, bool) {
	if i := s.indexDe(playerID); i >= 0 {
		return s.Occupants[i].Place, true
	}
	return 0, false
}

// Asseoir place un arrivant et rend sa place.
//
// IDEMPOTENT : un joueur deja assis ressort avec SA place, sans en consommer
// une seconde. Le client rejoue sa demande des qu'une reponse se perd — c'est
// le comportement normal d'un aller-retour HTTP sur un reseau reel — et un
// service qui compterait deux entrees pour un joueur remplirait le salon de
// fantomes portant le meme nom.
//
// Un joueur deja assis retrouve donc sa place MEME EN MANCHE, ce qui est le
// seul moyen de recuperer apres une reponse perdue. Il faut dire ce que ca ne
// fait pas : le relais, lui, refuse toute entree apres son START (`s.demarre`,
// internal/duel/relay.go). Le salon rend ce qu'il sait, il ne promet pas que le
// transport rouvrira une session close.
func (s *Salon) Asseoir(maintenant time.Time, playerID int64, pseudo string) (int, error) {
	if i := s.indexDe(playerID); i >= 0 {
		s.Occupants[i].Pseudo = pseudo
		s.Occupants[i].BattuA = maintenant
		return s.Occupants[i].Place, nil
	}
	if !s.Ouvert() {
		return 0, ErrFerme
	}
	place, libre := s.PlaceLibre(maintenant)
	if !libre {
		return 0, ErrComplet
	}
	s.Occupants = append(s.Occupants, Occupant{
		Place:    place,
		PlayerID: playerID,
		Pseudo:   pseudo,
		Camp:     CampDeLaPlace(s.Camps, place),
		Vivante:  true,
		EntreA:   maintenant,
		BattuA:   maintenant,
	})
	return place, nil
}

// Lever retire un occupant, et dit si le salon vient de mourir.
//
// LE PROPRIETAIRE QUI PART NE DETRUIT PAS LE SALON tant qu'il reste du monde :
// la main passe au PLUS ANCIEN occupant restant.
//
// Pourquoi ne pas fermer : le proprietaire n'est proprietaire que parce qu'il a
// tape le code en premier. Ce n'est pas un role, c'est un ordre d'arrivee — et
// faire dependre la manche de sept autres joueurs d'une connexion qui n'a rien
// de particulier revient a donner a un seul d'entre eux le pouvoir de tout
// annuler, y compris par accident. Le mode dure plusieurs minutes ; il ne doit
// pas s'evaporer parce que quelqu'un a ferme sa fenetre.
//
// Pourquoi le PLUS ANCIEN, et non le plus petit numero de place : les places se
// reattribuent (voir `PlaceLibre`), donc le numero le plus bas peut appartenir a
// celui qui vient d'arriver. L'anciennete, elle, ne se recycle pas — c'est le
// seul ordre stable que le salon connaisse, et c'est aussi le plus defendable :
// celui qui attend depuis le plus longtemps est celui qui a le plus a perdre.
// Partir sans etre assis rend faux et ne change rien : la sortie est
// IDEMPOTENTE, comme l'entree, et pour la meme raison. L'heritage est reevalue
// dans les deux cas, pour que l'invariant « un salon sans personne est fini »
// tienne quel que soit le chemin qui y mene.
func (s *Salon) Lever(maintenant time.Time, playerID int64) (parti bool, vide bool) {
	if i := s.indexDe(playerID); i >= 0 {
		s.Occupants = append(s.Occupants[:i], s.Occupants[i+1:]...)
		parti = true
	}
	return parti, s.reprendreLaMain(maintenant)
}

// reprendreLaMain redonne le salon a quelqu'un, ou le ferme. Rend vrai quand il
// ne reste personne.
func (s *Salon) reprendreLaMain(maintenant time.Time) bool {
	restants := s.Vivants(maintenant)
	if len(restants) == 0 {
		if TransitionPermise(s.Etat, Fini) {
			s.Etat = Fini
			s.ChangeA = maintenant
		}
		return true
	}

	// Le proprietaire est-il encore la ?
	for _, o := range restants {
		if o.PlayerID == s.Proprietaire {
			return false
		}
	}

	doyen := restants[0]
	for _, o := range restants[1:] {
		// A egalite d'horodatage — deux entrees dans la meme milliseconde, ce
		// que PostgreSQL peut rendre — la place tranche. Sans ce second critere
		// l'heritier dependrait de l'ordre des lignes, qui n'est pas garanti.
		if o.EntreA.Before(doyen.EntreA) ||
			(o.EntreA.Equal(doyen.EntreA) && o.Place < doyen.Place) {
			doyen = o
		}
	}
	s.Proprietaire = doyen.PlayerID
	s.ProprietairePseudo = doyen.Pseudo
	return false
}

/* ========================================================================== */
/* Le battement                                                               */
/* ========================================================================== */

// Battement — ce qu'un occupant publie a chaque tour.
//
// C'est A LA FOIS le signe de vie qui garde sa place et la ligne de classement
// que la page live affiche. Les deux en un seul aller-retour, pour la meme
// raison que la presence : c'est le seul echange periodique du mode, et lui
// faire couter deux requetes doublerait le trafic sans rien apprendre de plus.
type Battement struct {
	Points   int32
	Fusibles int32
	Vivante  bool
	Borne    string
	Camp     int
	// Commence — le proprietaire declare que la manche part.
	//
	// C'est le SEUL declencheur de « attente -> manche », et il vient du
	// proprietaire parce que c'est lui qui arbitre : la place 0 porte les regles
	// du couperet cote client (voir l'en-tete de internal/duel/relay.go). Un
	// autre joueur qui l'envoie ne demarre rien — et son battement reste valide
	// pour le reste, parce que refuser tout le battement pour un champ de trop
	// le ferait disparaitre du classement.
	Commence bool
}

// CampDeLaPlace rend le camp d'une place. Le SERVEUR le decide, personne
// d'autre.
//
// LE CAMP N'EST PAS UNE ETIQUETTE D'AFFICHAGE, et c'est la seule chose a
// retenir de cette fonction. Le Couperet CLASSE LES CAMPS : la lame descend
// dans le camp dernier, puis dans le joueur dernier de ce camp. Un camp que le
// client declare est donc un camp qu'un joueur peut changer — et changer de
// camp en cours de manche pour rejoindre celui qui mene est exactement la
// facon d'echapper a la lame. Le champ « camp » du battement est encore lu
// dans le JSON, parce que le decodeur refuse les champs inconnus et qu'un
// client deja livre l'envoie ; sa VALEUR est ignoree.
//
// En individuel — un seul camp — le camp EST la place. Ce n'est pas une
// convention inventee ici : c'est celle du mode, ou `room_cp_place.camp` est
// documente « 0..7 ; en individuel, camp = place ». La respecter evite que le
// classement live regroupe huit joueurs solitaires dans un camp 0 commun.
//
// A deux camps, l'alternance suit la PLACE : 0-1-0-1 et non 0-0-1-1. C'est la
// regle de `couperet_ouvrir` dans room/main.c, et sa raison y est ecrite — les
// places suivent l'ordre du classement affiche, et deux coequipiers cote a
// cote s'y lisent comme un bloc qui mene, alors que c'est le camp qu'on veut
// lire.
//
// CE QUE CA DEBLOQUE. Les equipes en ligne etaient annoncees impossibles :
// « le protocole ne porte pas de camp dans son entree, et l'adopter depuis
// l'ETAT que chacun publie sur lui-meme laisserait un joueur changer de camp
// en cours de manche » (room/main.c, bloc du coup d'envoi). C'etait vrai du
// RELAIS, qui ne connait personne. Le salon, lui, sait qui s'est assis ou,
// parce qu'il l'a authentifie. Le camp n'a donc pas besoin de voyager sur le
// relais : il se lit ici, et il ne se discute pas.
func CampDeLaPlace(camps, place int) int {
	if camps <= 1 {
		return place
	}
	return place % camps
}

// Battre enregistre un battement et fait, le cas echeant, partir la manche.
func (s *Salon) Battre(maintenant time.Time, playerID int64, b Battement) error {
	if s.Etat == Fini {
		// Le client doit apprendre que c'est termine, et l'apprendre par la
		// meme route que celle qu'il interroge deja. Sans ce refus il battrait
		// dans le vide jusqu'a ce que quelqu'un ferme la fenetre.
		return ErrFerme
	}
	i := s.indexDe(playerID)
	if i < 0 {
		return ErrPasAssis
	}

	o := &s.Occupants[i]
	o.BattuA = maintenant
	o.Points = b.Points
	o.Fusibles = b.Fusibles
	if o.Fusibles < 0 {
		// `room_cp_place.fusibles` est un `int32_t` et le mode ne descend jamais
		// sous zero : une valeur negative est un client casse, pas une manche.
		o.Fusibles = 0
	}
	o.Vivante = b.Vivante
	o.Borne = b.Borne
	// Le camp NE VIENT PAS du battement, voir `CampDeLaPlace`. `b.Camp` est
	// lu par le decodeur et jete ici.
	o.Camp = CampDeLaPlace(s.Camps, o.Place)

	if b.Commence && playerID == s.Proprietaire && TransitionPermise(s.Etat, Manche) {
		s.Etat = Manche
		s.ChangeA = maintenant
	}
	return nil
}

/* ========================================================================== */
/* Le faucheur                                                                */
/* ========================================================================== */

// Faucher retire les occupants qui ne battent plus, et rend les places qu'il a
// liberees ainsi que la fermeture eventuelle du salon.
//
// C'est la MEME regle que la lecture applique pour n'afficher que les vivants
// (`frais`), executee ici pour de bon. La consequence tient en une phrase : un
// service ou le faucheur ne tournerait jamais afficherait exactement la meme
// chose, seule la table grossirait. C'est la propriete que la presence a deja —
// `TouchPresence` filtre sur `seen_at`, `PurgePresence` ne fait que borner la
// taille — et elle vaut qu'on la garde : elle interdit qu'une periodicite mal
// reglee change ce que les joueurs voient.
func (s *Salon) Faucher(maintenant time.Time) (retires []int, ferme bool) {
	// UN SALON FINI NE SE FAUCHE PLUS, et ses occupants restent.
	//
	// Ce sont les LIGNES DU CLASSEMENT FINAL. Un client cesse de battre quand
	// la manche s'arrete — c'est normal, il n'a plus rien a publier — et le
	// faucheur les retirait donc une a une dans les secondes qui suivent. Vu
	// depuis la page web : la manche se termine, et le tableau se vide sous les
	// yeux de qui vient regarder qui a gagne. Mesure a l'ecran avant
	// correction : « MANCHE TERMINEE » au-dessus de « Personne pour
	// l'instant. »
	//
	// Le salon disparait quand meme, mais par `Perime` et en bloc, apres sa
	// duree de retention. Une place fauchee sur un salon deja fini ne libere
	// rien : plus personne ne peut s'y asseoir.
	if s.Etat == Fini {
		return nil, false
	}

	gardes := s.Occupants[:0:0]
	for _, o := range s.Occupants {
		if frais(o, maintenant) {
			gardes = append(gardes, o)
			continue
		}
		retires = append(retires, o.Place)
	}
	if len(retires) == 0 {
		return nil, false
	}

	// UNE MANCHE QUI SE VIDE GARDE SON TABLEAU. Si la fauche ne laisse
	// personne alors qu'on jouait, la manche se termine — mais les lignes
	// restent, avec les points qu'elles avaient. Les effacer donnerait un
	// « MANCHE TERMINEE » au-dessus d'un tableau vide, c'est-a-dire la page
	// d'une partie dont on ne saura jamais rien. En ATTENTE, au contraire, il
	// n'y a rien a montrer : un salon que tout le monde a quitte avant de
	// commencer se vide pour de bon.
	etait := s.Etat
	if len(gardes) == 0 && etait == Manche {
		if TransitionPermise(s.Etat, Fini) {
			s.Etat = Fini
			s.ChangeA = maintenant
		}
		// ON NE REND AUCUNE PLACE A RETIRER, et c'est le point : `retires` est
		// la liste que l'appelant EFFACE en base. La rendre ici gardait le
		// tableau en memoire et le supprimait dans la foulee — le classement
		// final revenait vide malgre tout, ce qui est exactement ce qu'on
		// essaie d'eviter. Rien n'est retire : la manche est finie, plus
		// personne ne peut s'asseoir, et la place n'a plus a etre liberee.
		return nil, s.Etat == Fini && etait != Fini
	}

	s.Occupants = gardes
	s.reprendreLaMain(maintenant)
	return retires, s.Etat == Fini && etait != Fini
}

// Perime dit si un salon termine peut disparaitre de la base.
//
// Il ne repond que pour un salon FINI : tant qu'il court, son age ne veut rien
// dire — une manche pleine dure plus de cinq minutes et n'a pas a etre effacee
// parce qu'elle dure.
func (s *Salon) Perime(maintenant time.Time) bool {
	return s.Etat == Fini && maintenant.Sub(s.ChangeA) >= RetentionFini
}

/* ========================================================================== */
/* Tirages                                                                    */
/* ========================================================================== */

// EspaceDesCodes rend le nombre de codes que l'alphabet et la longueur retenus
// permettent.
//
// CALCULE, jamais recopie. Un chiffre ecrit a la main dans un commentaire
// devient faux le jour ou l'on retire un symbole, et personne ne le voit ; ici
// le test le lit d'ici et l'affiche.
func EspaceDesCodes() int64 {
	n := int64(1)
	for i := 0; i < CodeLong; i++ {
		n *= int64(len(Alphabet))
	}
	return n
}

// TirerCode rend un code d'acces uniformement tire.
//
// Le rejet des octets au-dessus de `seuilRejet` est ce qui rend le tirage
// uniforme ; voir la constante pour ce qu'il coute et ce qu'il evite.
func TirerCode() (string, error) {
	out := make([]byte, 0, CodeLong)
	buf := make([]byte, CodeLong)
	for len(out) < CodeLong {
		if _, err := rand.Read(buf); err != nil {
			return "", fmt.Errorf("tirage du code : %w", err)
		}
		for _, b := range buf {
			if int(b) >= seuilRejet {
				continue
			}
			out = append(out, Alphabet[int(b)%len(Alphabet)])
			if len(out) == CodeLong {
				break
			}
		}
	}
	return string(out), nil
}

// TirerRelais tire l'identifiant de SESSION sur le relais.
//
// C'EST LUI, LA CAPACITE. Le relais n'a aucune notion de compte : il apparie sur
// ce seul nombre, et quiconque le connait entre dans la manche. Il est donc tire
// avec `crypto/rand` — jamais derive du code, jamais incremente, jamais
// previsible — et il ne sort que vers un joueur authentifie qui a obtenu une
// place. Le classement live, lui, ne le publie pas : voir l'en-tete du fichier.
//
// SOIXANTE-TROIS BITS et non soixante-quatre. Le bit de poids fort est efface,
// pour la raison exacte que la graine de partie l'efface deja (`randomSeed`,
// internal/api) et que le relais efface le sien (`tirerGraine`,
// internal/duel) : ce nombre traverse un `bigint` PostgreSQL, qui est SIGNE,
// puis l'analyseur JSON du client, qui le lit avec `SDL_strtoll`
// (`ns_json_get_i64`, engine/core/ns_json.c) — signe lui aussi. Un 64e bit
// deborderait des deux cotes, et le symptome serait un salon qu'on rejoint
// mais ou l'on ne trouve personne.
//
// Ce que 63 bits laissent : 9,2 x 10^18 valeurs, contre 1,07 x 10^9 pour le
// code. Neuf ordres de grandeur d'ecart entre ce qu'on tape et ce qui donne
// acces, et c'est precisement l'ecart qu'on cherche.
func TirerRelais() (int64, error) {
	var b [8]byte
	if _, err := rand.Read(b[:]); err != nil {
		return 0, fmt.Errorf("tirage de l'identifiant de relais : %w", err)
	}
	var v int64
	for _, x := range b {
		v = v<<8 | int64(x)
	}
	return v & 0x7FFFFFFFFFFFFFFF, nil
}

/* ========================================================================== */
/* Entrees                                                                    */
/* ========================================================================== */

// NormaliserCode rend le code tel qu'il est stocke, ou la chaine vide si ce n'en
// est pas un.
//
// Les capitales sont imposees ici plutot que demandees au joueur : le code est
// dicte a l'oral et recopie a la main, et refuser « k7m3qp » pour la casse
// serait un refus que rien ne justifie. Tout ce qui n'est pas dans l'alphabet
// est en revanche refuse SANS interroger la base : un code mal forme n'est
// jamais un salon, et le dire sans requete evite qu'un balayage coute une
// lecture a chaque essai.
func NormaliserCode(brut string) string {
	brut = strings.ToUpper(strings.TrimSpace(brut))
	if len(brut) != CodeLong {
		return ""
	}
	for i := 0; i < len(brut); i++ {
		if strings.IndexByte(Alphabet, brut[i]) < 0 {
			return ""
		}
	}
	return brut
}

// Valider controle les dimensions demandees a la creation.
//
// Le nom n'est PAS assaini ici : il l'est par `sanitizeShort` (internal/api),
// qui traite deja les pseudos et les noms de borne. Deux assainissements pour
// une meme sorte de champ finiraient par diverger, et c'est le genre de
// divergence qui ne se voit que le jour ou l'un des deux laisse passer ce que
// l'autre refusait.
func Valider(nom string, places, camps int) error {
	if strings.TrimSpace(nom) == "" {
		return ErrNom
	}
	if len([]rune(nom)) > NomMax {
		return ErrNom
	}
	if places < PlacesMin || places > PlacesMax {
		return ErrBornes
	}
	if camps < CampsMin || camps > CampsMax {
		return ErrBornes
	}
	return nil
}
