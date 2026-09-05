// Package duel relaie les trames d'un duel en pas verrouille entre deux
// clients, et rien de plus.
//
// Ce qu'il fait
// -------------
// Il apparie deux clients qui presentent le meme identifiant de duel, leur
// donne une graine commune, puis RECOPIE chaque trame de l'un vers l'autre.
//
// Il fait desormais la MEME chose a N places — voir « L'arene » plus bas. Meme
// relais, meme betise volontaire, un type de trame de plus.
//
// Ce qu'il ne fait pas, et pourquoi c'est deliberé
// ------------------------------------------------
// Il ne simule rien, ne valide aucune regle de jeu, ne connait aucun score.
// L'autorite sur les scores reste la ou elle est depuis M6 : le journal de
// partie scelle par HMAC, envoye par HTTP, et RECALCULE par le serveur. Un
// relais qui arbitrerait serait une deuxieme autorite, donc une deuxieme
// surface a defendre — et il faudrait alors y porter les regles des huit jeux,
// en Go, en double de leur version C. C'est exactement le genre de duplication
// qui finit par diverger.
//
// La consequence est claire et il vaut mieux l'ecrire : deux clients complices
// peuvent se mentir l'un a l'autre pendant un duel. Ils ne peuvent pas pour
// autant faire enregistrer un faux score — c'est le chemin HTTP qui en decide,
// et il n'a pas change.
//
// La graine vient d'ICI
// ---------------------
// Elle est tiree par le relais avec `crypto/rand`, jamais proposee par un
// client. C'est la meme regle que pour le billet de partie : celui qui joue ne
// choisit pas ce sur quoi il joue.
//
// L'ARENE : le meme relais, a 2 a 8 places
// ----------------------------------------
// Deux a huit joueurs dans la meme salle en meme temps. Le relais apparie et
// DIFFUSE ; il ne connait ni score, ni classement, ni couperet, ni minuterie.
// La regle du jeu vit en C, une seule fois, chez le joueur de la place 0 — qui
// est l'arbitre et publie son verdict comme n'importe quelle autre trame.
//
// La place 0 peut donc mentir. C'est la meme concession que celle du duel, et
// elle est assumee pour la meme raison, qui est ecrite six lignes plus haut :
// l'autorite sur les scores ENREGISTRES est ailleurs, et rien de ce fichier ne
// la touche.
//
// UN TYPE DE TRAME NOUVEAU, pas un HELLO etendu
// ---------------------------------------------
// Le duel a deux est livre et tourne. Un « if len(charge) >= 34 » sur le HELLO
// ferait dependre le SENS d'une trame de sa LONGUEUR : le jour ou un client
// installe allonge son HELLO d'un champ, il se reveille dans l'arene. `fJoin`
// (0x10) est donc un type a part entiere, et le chemin du duel n'est pas
// touche : meme HELLO, memes places 0 et 1, meme recopie vers l'AUTRE et non
// une diffusion.
//
// L'IDENTITE DE L'EMETTEUR EST INSEREE, et voici le raisonnement
// --------------------------------------------------------------
// Toute trame de type >= 0x20 est rediffusee sans etre interpretee — c'est par
// la que passeront l'etat des joueurs, les sabotages et le verdict du couperet,
// et le relais n'a pas a savoir ce que c'est. Il ecrit devant elle UN OCTET :
// la place de l'emetteur.
//
// L'autre option — diffuser brut — a ete pesee et ecartee :
//
//   - Le prix reel de l'identite est d'UN OCTET sur le fil, pas d'une copie de
//     plus. Le relais recopie deja chaque charge une fois (`readFrame` alloue,
//     `frame` recopie ailleurs) ; `frameDePlace` alloue une fois et recopie une
//     fois, exactement comme `frame`. Il n'y a pas de tampon supplementaire.
//   - Sans elle, un sabotage « de la part de la place 3 » se fabrique en
//     changeant un octet dans SA propre trame. Le mode entier repose sur qui a
//     envoye quoi : c'est la seule chose que le relais soit en position de
//     garantir, et elle ne lui coute pas de connaitre les regles.
//   - Le client n'y gagnerait rien : il devrait de toute facon ecrire sa place
//     dans la charge pour que la trame veuille dire quelque chose. On ne
//     supprime donc pas un octet, on decide QUI l'ecrit — et celui qui l'ecrit
//     ici ne peut pas se tromper de place.
//
// Ce que ca ne ferme PAS, et il faut le dire aussi net : la place 0 arbitre le
// couperet, et un arbitre qui ment ment de sa propre place. L'usurpation fermee
// est celle d'un joueur qui se fait passer pour un autre, pas celle d'un
// arbitre malhonnete, qui est assumee plus haut.
//
// Le cadrage, et tous les types
// -----------------------------
//
//	uint16 longueur (petit-boutiste) | uint8 type | charge
//
//	0x01 fHello   client -> relais  uint64 duel | uint8 place | char jeu[16] | char diff[8]
//	0x02 fStart   relais -> tous    uint64 graine
//	0x05 fBye     relais -> tous    uint8 motif (duel) — uint8 place partie (arene)
//	0x10 fJoin    client -> relais  uint64 salon | uint8 place | uint8 places_attendues | char pseudo[24]
//	0x11 fRoster  relais -> tous    uint8 n | { uint8 place ; char pseudo[24] } * n
//	>= 0x20       client -> relais  diffuse tel quel aux AUTRES places, precede
//	                                de l'octet de place de l'emetteur
//
// 0x03 et 0x04 — l'entree et l'empreinte du pas verrouille — traversent le duel
// sans que le relais les regarde ; il n'a jamais eu a les connaitre. Dans
// l'arene, les types < 0x20 sont reserves au relais et ne sont JAMAIS
// rediffuses : c'est ce qui empeche un joueur de fabriquer un START ou un
// tableau des places.
package duel

import (
	"bytes"
	"crypto/rand"
	"encoding/binary"
	"errors"
	"io"
	"log/slog"
	"net"
	"sync"
	"time"
	"unicode/utf8"
)

const (
	// La charge maximale d'une trame. La plus grosse que le protocole definisse
	// est le JOIN, a 34 octets. 512 laisse de la place pour en ajouter, et
	// borne ce qu'un pair peut faire allouer : un decodeur qui croit une
	// longueur annoncee sans la borner est le defaut de base d'un lecteur de
	// socket.
	frameMax = 512

	// La charge maximale d'une trame LIBRE d'arene, celle que le relais
	// rediffuse en ecrivant la place devant.
	//
	// 511 et non 512, parce que l'octet de place doit tenir dans la meme trame.
	// Une charge de 512 produirait une trame de 513 que `frame` tronquerait en
	// silence — et un octet perdu au bout d'une charge n'est pas une trame trop
	// longue, c'est une charge qui veut dire autre chose. On ferme la connexion
	// au lieu de rogner.
	areneChargeMax = frameMax - 1

	// Un duel ou personne ne parle est un duel mort. Le pas verrouille envoie
	// une trame par pas de simulation, soit 120 par seconde : dix secondes de
	// silence ne sont pas de la latence, c'est une deconnexion.
	idleTimeout = 10 * time.Second

	// Le temps laisse au second joueur pour arriver — et, dans l'arene, au
	// salon pour se remplir.
	pairTimeout = 60 * time.Second

	// La profondeur d'une file de sortie, en trames. C'est la valeur du duel
	// depuis le premier jour ; elle sert de base au calcul du plafond ci-dessous.
	fileProfondeur = 256

	// Plafond de SESSIONS dans la table — duels et salons confondus. Sans lui,
	// ouvrir des connexions avec des identifiants differents suffirait a faire
	// grossir la table sans fin. C'est l'ancien `maxDuels`, au mot pres.
	maxSessions = 256

	// Plafond de PLACES ouvertes, toutes sessions confondues. C'est le second
	// verrou, et c'est lui qui borne la MEMOIRE — l'autre ne borne qu'un nombre
	// de cles.
	//
	// Pourquoi deux verrous : une place coute une file, et un salon en a
	// jusqu'a QUATRE FOIS plus qu'un duel. Continuer a ne compter que les
	// sessions aurait quadruple le pire cas en silence le jour de l'arene.
	//
	// Le calcul, par place et au pire :
	//
	//	file            256 emplacements x 24 octets (en-tete de tranche) =   6 144
	//	trames retenues 256 x (3 + 512) octets                            = 131 840
	//	                                                                    -------
	//	                                                          137 984 octets = 134,8 Kio
	//
	// C'est bien le PIRE et pas la moyenne : il suppose une file pleine de
	// trames maximales dont chacune n'est plus retenue que par cette file-la
	// (une trame diffusee est UNE allocation partagee par ses destinataires,
	// pas une par destinataire). Il ne compte ni l'en-tete du canal ni les
	// piles des deux routines, qui sont du menu fretin devant, et il ignore
	// l'arrondi de l'allocateur, qui joue contre nous.
	//
	//	512 places = 70 647 808 octets = 67,4 Mio
	//
	// 512 places, c'est exactement ce que 256 duels ont TOUJOURS pu couter : le
	// budget ne bouge pas, seule la facon de le depenser change. Il paie 256
	// duels, ou 64 salons de huit, ou n'importe quel melange des deux.
	//
	// Ce que le plafond de sessions aurait donne SEUL, pour comparaison : 256
	// salons pleins = 2 048 places = 269,5 Mio. Quatre fois le budget, pour un
	// relais qui doit tenir dans un conteneur a cote du serveur HTTP.
	maxPlacesOuvertes = 512

	// Les bornes d'un salon. Deux, parce qu'en dessous c'est un duel et qu'il a
	// deja son protocole. Huit, parce que c'est le nombre de bornes que la
	// salle montre a la fois, et parce que le tableau des places doit tenir
	// dans une trame : 1 + 8 x 25 = 201 octets.
	placesMin = 2
	placesMax = 8

	// La largeur du champ de pseudo sur le fil, et le nombre d'octets qu'on y
	// laisse reellement passer. Voir `pseudoPropre` pour le 23.
	pseudoLarg  = 24
	pseudoUtile = pseudoLarg - 1

	// La taille exacte d'un JOIN : 8 + 1 + 1 + 24.
	joinMin = 10 + pseudoLarg

	fHello  = 0x01
	fStart  = 0x02
	fBye    = 0x05
	fJoin   = 0x10
	fRoster = 0x11

	// Le premier type que le relais ne regarde pas. Tout ce qui est au-dessus
	// est diffuse tel quel dans l'arene ; tout ce qui est en dessous lui
	// appartient.
	fLibre = 0x20
)

// protocole dit lequel des deux services une session rend.
//
// Un seul type `session` les porte tous les deux : l'appariement, les files, la
// fermeture et le compte des places sont identiques mot pour mot, et seules
// deux choses different — a qui l'on recopie, et ce qu'on annonce en arrivant.
// Deux types auraient duplique le reste, c'est-a-dire la partie ou se trouvent
// les pieges.
type protocole uint8

const (
	protoDuel  protocole = iota // 2 places, recopie vers l'AUTRE
	protoArene                  // 2 a 8 places, diffusion a TOUTES les autres
)

type side struct {
	place byte
	// Le pseudo tel qu'il sera redistribue aux autres places. Vide pour un
	// duel, qui n'a pas de tableau des places.
	pseudo string
	out    chan []byte
	// `closed` est garde par le mutex du relais, et il existe pour une raison
	// precise : sans lui, deux departs SIMULTANES se terminent par un envoi sur
	// un canal deja ferme, ce qui panique la routine et emporte le processus.
	// Un relais dont on peut provoquer l'arret en raccrochant des deux cotes en
	// meme temps n'est pas un relais.
	closed bool
}

type session struct {
	id    uint64
	proto protocole
	// Dimensionnee a l'arrivee du PREMIER joueur : 2 pour un duel, la valeur
	// annoncee pour un salon. Les arrivants suivants doivent annoncer la meme.
	sides []*side
	// La graine a ete tiree et le START est parti. L'arene refuse alors toute
	// entree : un joueur qui arriverait au pas 40 000 avec la seule graine
	// serait au pas 0, et le relais n'a rien pour le rattraper — ce serait a
	// l'arbitre de le faire, donc a une regle de jeu, donc pas ici.
	demarre bool
}

// occupees compte les places prises. Le mutex du relais doit etre tenu.
func (s *session) occupees() int {
	n := 0
	for _, sd := range s.sides {
		if sd != nil {
			n++
		}
	}
	return n
}

// Relay est le serveur TCP du duel et de l'arene. Il est independant du serveur
// HTTP : il n'ecoute pas le meme port, ne partage aucun etat avec lui, et son
// arret ne touche pas au classement.
type Relay struct {
	log *slog.Logger

	mu       sync.Mutex
	sessions map[uint64]*session
	// Le nombre de places ouvertes, toutes sessions confondues. Voir
	// `maxPlacesOuvertes` : c'est ce compteur-ci qui borne la memoire.
	places int

	ln net.Listener
}

func New(log *slog.Logger) *Relay {
	return &Relay{log: log, sessions: make(map[uint64]*session)}
}

// Listen ouvre le port et sert jusqu'a la fermeture de l'ecouteur.
func (r *Relay) Listen(addr string) error {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return err
	}
	r.ln = ln
	r.log.Info("duel : relais a l'ecoute", "addr", addr)
	return r.servir(ln)
}

// servir accepte jusqu'a la fermeture. Separe de `Listen` pour que le test
// puisse fournir SON ecouteur — et donc son port, tire par le systeme — sans
// que le chemin servi differe d'une ligne de celui de production.
func (r *Relay) servir(ln net.Listener) error {
	for {
		c, err := ln.Accept()
		if err != nil {
			if errors.Is(err, net.ErrClosed) {
				return nil
			}
			return err
		}
		go r.serve(c)
	}
}

func (r *Relay) Close() error {
	if r.ln == nil {
		return nil
	}
	return r.ln.Close()
}

// readFrame lit une trame complete : longueur, type, charge.
func readFrame(c net.Conn) (byte, []byte, error) {
	var head [3]byte
	if _, err := io.ReadFull(c, head[:]); err != nil {
		return 0, nil, err
	}
	n := binary.LittleEndian.Uint16(head[:2])
	if n > frameMax {
		return 0, nil, errors.New("trame trop longue")
	}
	body := make([]byte, n)
	if n > 0 {
		if _, err := io.ReadFull(c, body); err != nil {
			return 0, nil, err
		}
	}
	return head[2], body, nil
}

func frame(typ byte, payload []byte) []byte {
	// La longueur est BORNEE avant d'etre convertie, et pas seulement pour
	// faire taire l'analyseur : `payload` vient d'une lecture de socket. Une
	// charge de plus de 65 535 octets tronquerait sa longueur annoncee, et le
	// pair d'en face relirait la trame de travers a partir de la — c'est-a-dire
	// une desynchronisation du FLUX, plus difficile a diagnostiquer que
	// n'importe quelle divergence de jeu. `readFrame` refuse deja au-dela de
	// `frameMax` ; ce test-ci ferme le meme trou du cote de l'ecriture.
	if len(payload) > frameMax {
		payload = payload[:frameMax]
	}
	out := make([]byte, 3+len(payload))
	// #nosec G115 -- borne a frameMax (512) trois lignes plus haut.
	binary.LittleEndian.PutUint16(out[:2], uint16(len(payload)))
	out[2] = typ
	copy(out[3:], payload)
	return out
}

// frameDePlace construit une trame d'arene en ecrivant la place de l'emetteur
// devant la charge.
//
// UNE allocation et UNE recopie, comme `frame` : c'est tout ce que coute
// l'identite de l'emetteur, et c'est le chiffre sur lequel repose la decision
// ecrite en tete de fichier.
func frameDePlace(typ, place byte, payload []byte) []byte {
	if len(payload) > areneChargeMax {
		payload = payload[:areneChargeMax]
	}
	out := make([]byte, 4+len(payload))
	// #nosec G115 -- borne a areneChargeMax (511) trois lignes plus haut.
	binary.LittleEndian.PutUint16(out[:2], uint16(1+len(payload)))
	out[2] = typ
	out[3] = place
	copy(out[4:], payload)
	return out
}

// posteVerrouille depose une trame dans la file d'un cote. Le mutex du relais
// DOIT etre tenu par l'appelant.
//
// Une file pleine fait TOMBER la trame plutot que bloquer : le pair ne lit
// plus, le pas verrouille s'arretera de lui-meme, et un arret se voit la ou un
// blocage serait un gel silencieux.
func posteVerrouille(sd *side, b []byte) {
	if sd == nil || sd.closed {
		return
	}
	select {
	case sd.out <- b:
	default:
	}
}

// post fait la meme chose en prenant le mutex. Tous les envois passent par l'un
// ou l'autre — c'est ce qui rend l'invariant « on n'ecrit jamais dans une file
// fermee » verifiable en deux endroits, et pas en dix.
func (r *Relay) post(sd *side, b []byte) {
	r.mu.Lock()
	defer r.mu.Unlock()
	posteVerrouille(sd, b)
}

// shutdownSide ferme la file d'un cote, une seule fois.
func (r *Relay) shutdownSide(sd *side) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if sd.closed {
		return
	}
	sd.closed = true
	close(sd.out)
}

// demarre dit si le START du salon est deja parti.
func (r *Relay) demarre(s *session) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	return s.demarre
}

func (r *Relay) serve(c net.Conn) {
	defer c.Close()
	_ = c.SetReadDeadline(time.Now().Add(pairTimeout))

	typ, body, err := readFrame(c)
	if err != nil {
		r.log.Debug("relais : premiere trame illisible", "err", err)
		return
	}

	var (
		s     *session
		me    *side
		place byte
	)
	switch typ {
	case fHello:
		if len(body) < 9 {
			r.log.Debug("duel : HELLO trop court", "octets", len(body))
			return
		}
		id := binary.LittleEndian.Uint64(body[:8])
		place = body[8]
		if place > 1 {
			return
		}
		s, me, err = r.rejoindre(id, place, 2, protoDuel, "")
		if err != nil {
			r.log.Debug("duel : appariement refuse", "duel", id, "slot", place, "err", err)
			return
		}

	case fJoin:
		if len(body) < joinMin {
			r.log.Debug("arene : JOIN trop court", "octets", len(body))
			return
		}
		id := binary.LittleEndian.Uint64(body[:8])
		place = body[8]
		attendues := body[9]
		if attendues < placesMin || attendues > placesMax || place >= attendues {
			r.log.Debug("arene : place hors bornes", "salon", id,
				"place", place, "attendues", attendues)
			return
		}
		s, me, err = r.rejoindre(id, place, attendues, protoArene, pseudoPropre(body[10:joinMin]))
		if err != nil {
			r.log.Debug("arene : entree refusee", "salon", id, "place", place, "err", err)
			return
		}

	default:
		r.log.Debug("relais : premiere trame de type inattendu", "type", typ)
		return
	}
	defer r.leave(s, place)

	// L'ecriture vit dans sa propre routine : sans elle, un pair lent bloquerait
	// la LECTURE de l'autre, et le duel s'arreterait des que l'un a une hoquet
	// reseau.
	done := make(chan struct{})
	go func() {
		defer close(done)
		for b := range me.out {
			_ = c.SetWriteDeadline(time.Now().Add(idleTimeout))
			if _, err := c.Write(b); err != nil {
				return
			}
		}
	}()

	// Vrai tant que le salon n'est pas complet. Le duel ne s'en sert pas : son
	// delai d'attente est celui d'hier, a la seconde pres.
	attente := s.proto == protoArene

	for {
		delai := idleTimeout
		if attente {
			if r.demarre(s) {
				attente = false
			} else {
				// UN SALON INCOMPLET ATTEND `pairTimeout`, PAS `idleTimeout`.
				// Un joueur qui patiente devant un salon a moitie plein n'a
				// rien a envoyer : lui appliquer les dix secondes du pas
				// verrouille le mettrait dehors au bout de dix secondes de
				// remplissage parfaitement normal.
				delai = pairTimeout
			}
		}
		_ = c.SetReadDeadline(time.Now().Add(delai))

		typ, body, err := readFrame(c)
		if err != nil {
			break
		}

		if s.proto == protoArene {
			if typ == fBye {
				// Le depart est annonce par le RELAIS, avec la place qu'il sait
				// etre la bonne (voir `leave`). Rediffuser ce BYE-ci laisserait
				// un joueur declarer le depart d'un autre.
				break
			}
			if typ < fLibre {
				continue // reserve au relais : jamais rediffuse
			}
			if len(body) > areneChargeMax {
				// La place ne tiendrait pas devant. On ferme, comme pour une
				// trame trop longue : rogner la charge en silence donnerait au
				// destinataire une trame qui veut dire autre chose.
				r.log.Debug("arene : charge trop longue pour la diffusion",
					"salon", s.id, "place", place, "octets", len(body))
				break
			}
			r.broadcast(s, place, frameDePlace(typ, place, body))
			continue
		}

		if typ == fHello {
			continue // un seul HELLO par connexion
		}
		r.broadcast(s, place, frame(typ, body))
		if typ == fBye {
			break
		}
	}
	// Fermer la file, PUIS attendre la routine d'ecriture. Dans cet ordre :
	// attendre sans fermer ne finirait jamais, fermer sans attendre laisserait
	// une ecriture en vol sur une connexion qu'on s'apprete a fermer.
	r.shutdownSide(me)
	<-done
}

// rejoindre place un arrivant, et annonce ce qu'il faut annoncer.
//
// Le duel et l'arene partagent tout jusqu'a la derniere ligne : la table, les
// deux plafonds, le refus d'une place prise. Ils divergent sur ce qui part
// ensuite — rien tant que le duel n'est pas complet, le tableau des places a
// CHAQUE arrivee pour l'arene.
func (r *Relay) rejoindre(id uint64, place, attendues byte, proto protocole,
	pseudo string) (*session, *side, error) {

	r.mu.Lock()
	defer r.mu.Unlock()

	s := r.sessions[id]
	if s == nil {
		if len(r.sessions) >= maxSessions {
			return nil, nil, errors.New("trop de sessions ouvertes")
		}
		s = &session{id: id, proto: proto, sides: make([]*side, attendues)}
		r.sessions[id] = s
	}
	if s.proto != proto {
		// Duels et salons partagent l'espace des identifiants. Deux protocoles
		// dans la meme session n'auraient aucun sens : le second arrivant est
		// refuse, il changera d'identifiant.
		return nil, nil, errors.New("identifiant deja pris par l'autre protocole")
	}
	if proto == protoArene {
		if len(s.sides) != int(attendues) {
			// Toutes les places d'un salon doivent annoncer le meme nombre. La
			// premiere annonce fait foi : sans cette regle, un salon de huit et
			// un salon de trois se partageraient une table et personne ne
			// recevrait jamais de START.
			return nil, nil, errors.New("places attendues differentes de la premiere annonce")
		}
		if s.demarre {
			return nil, nil, errors.New("salon deja lance")
		}
	}
	if int(place) >= len(s.sides) {
		return nil, nil, errors.New("place hors du salon")
	}
	if s.sides[place] != nil {
		return nil, nil, errors.New("place deja prise")
	}
	if r.places >= maxPlacesOuvertes {
		return nil, nil, errors.New("trop de places ouvertes")
	}

	me := &side{place: place, pseudo: pseudo, out: make(chan []byte, fileProfondeur)}
	s.sides[place] = me
	r.places++

	if proto == protoDuel {
		// Les deux sont la : on tire la graine et on lance.
		if s.sides[0] != nil && s.sides[1] != nil {
			graine, err := tirerGraine()
			if err != nil {
				r.retirer(s, place)
				return nil, nil, err
			}
			s.demarre = true
			start := frame(fStart, graine)
			// Envoi direct : on tient deja le mutex, et les deux cotes viennent
			// d'etre crees, donc aucun n'est ferme.
			for _, sd := range s.sides {
				posteVerrouille(sd, start)
			}
			r.log.Info("duel : apparie", "duel", id)
		}
		return s, me, nil
	}

	// LE TABLEAU DES PLACES PART A CHAQUE ARRIVEE, a tout le monde, l'arrivant
	// compris. C'est ce qui permet d'afficher un salon en train de se remplir
	// plutot qu'un ecran d'attente muet — et c'est aussi comment un client
	// apprend le pseudo des autres, qu'il ne verra nulle part ailleurs.
	tableau := frame(fRoster, rosterCharge(s))
	for _, sd := range s.sides {
		posteVerrouille(sd, tableau)
	}

	if s.occupees() == len(s.sides) {
		graine, err := tirerGraine()
		if err != nil {
			// Retirer ce qu'on vient de poser : `serve` n'appellera pas `leave`
			// pour une entree refusee, et la place resterait prise pour
			// toujours.
			r.retirer(s, place)
			return nil, nil, err
		}
		s.demarre = true
		start := frame(fStart, graine)
		for _, sd := range s.sides {
			posteVerrouille(sd, start)
		}
		r.log.Info("arene : salon complet", "salon", id, "places", len(s.sides))
	}
	return s, me, nil
}

// tirerGraine rend les huit octets de la graine commune.
func tirerGraine() ([]byte, error) {
	var seed [8]byte
	if _, err := rand.Read(seed[:]); err != nil {
		return nil, err
	}
	// Le bit de poids fort est efface : la graine traverse un `float` nulle
	// part, mais elle traverse des entiers signes, et un 64e bit a deja coute
	// une session de debogage a ce projet.
	seed[7] &= 0x7f
	return seed[:], nil
}

// rosterCharge construit la charge du tableau des places. Le mutex doit etre
// tenu.
//
// 1 + 8 x 25 = 201 octets au maximum, loin des 512 de `frameMax`.
func rosterCharge(s *session) []byte {
	out := make([]byte, 1, 1+len(s.sides)*(1+pseudoLarg))
	var n byte
	for _, sd := range s.sides {
		if sd == nil {
			continue
		}
		var nom [pseudoLarg]byte
		copy(nom[:], sd.pseudo)
		out = append(out, sd.place)
		out = append(out, nom[:]...)
		n++
	}
	out[0] = n
	return out
}

// pseudoPropre rend le pseudo tel qu'il sera REDIFFUSE aux autres places.
//
// Trois coupes, et aucune n'est une regle de jeu — c'est de l'hygiene de champ,
// au meme titre que la borne de `frameMax` :
//
//   - au premier octet nul : le champ fait 24 octets sur le fil, le nom fait ce
//     qu'il fait ;
//   - les octets de commande (moins de 0x20, et 0x7f) sont retires. Ce sont
//     ceux avec lesquels un joueur ecrit ce qu'il veut sur l'ecran d'un autre :
//     un retour a la ligne, un caractere d'echappement, et le tableau des
//     places affiche autre chose que des noms ;
//   - a 23 octets, sans couper une sequence UTF-8 en deux. 23 et non 24 pour
//     que le champ soit TOUJOURS termine par un zero : il atterrit dans un
//     `char[24]` en C, et un nom de 24 octets exactement y serait une chaine
//     sans terminateur. Un caractere en moins vaut mieux qu'une lecture hors
//     bornes chez celui qui affiche.
func pseudoPropre(brut []byte) string {
	if i := bytes.IndexByte(brut, 0); i >= 0 {
		brut = brut[:i]
	}
	propre := make([]byte, 0, len(brut))
	for _, o := range brut {
		if o < 0x20 || o == 0x7f {
			continue
		}
		propre = append(propre, o)
	}
	if len(propre) > pseudoUtile {
		propre = propre[:pseudoUtile]
		// Reculer tant que la fin est une sequence UTF-8 amputee. Un « é »
		// coupe en deux ne s'affiche pas mieux qu'un caractere de moins.
		for len(propre) > 0 {
			r, taille := utf8.DecodeLastRune(propre)
			if r != utf8.RuneError || taille > 1 {
				break
			}
			propre = propre[:len(propre)-1]
		}
	}
	return string(propre)
}

// broadcast envoie une trame a toutes les places SAUF a son emetteur.
//
// C'est l'ancien `forward` : pour un duel a deux places, « toutes sauf moi »
// est « l'autre », et pas un octet ne change de chemin.
func (r *Relay) broadcast(s *session, de byte, b []byte) {
	// Le tableau est local et fait `placesMax` : la collecte des cibles ne
	// coute donc pas une allocation par trame. A 120 trames par seconde et par
	// joueur, ce serait la seule allocation que le relais ferait sans raison.
	var tampon [placesMax]*side
	cibles := tampon[:0]

	r.mu.Lock()
	for _, sd := range s.sides {
		if sd == nil || sd.place == de {
			continue
		}
		cibles = append(cibles, sd)
	}
	r.mu.Unlock()

	for _, sd := range cibles {
		r.post(sd, b)
	}
}

// retirer libere une place. Le mutex DOIT etre tenu.
func (r *Relay) retirer(s *session, place byte) {
	if s.sides[place] == nil {
		return
	}
	s.sides[place] = nil
	r.places--
	if s.occupees() == 0 {
		r.oublier(s)
	}
}

// oublier retire la session de la table, si c'est bien CELLE-CI qui y est.
//
// L'identite est verifiee, et pas seulement l'identifiant : un salon ferme a
// une place laisse son dernier joueur connecte a une session qui n'est plus
// dans la table. Si quelqu'un a repris l'identifiant entre-temps, un `delete` a
// l'aveugle emporterait le salon des AUTRES. Le cas n'existe pas dans le duel,
// ou aucune session n'est retiree tant qu'un cote reste ; il existe dans
// l'arene, donc le controle est ici pour les deux.
func (r *Relay) oublier(s *session) {
	if r.sessions[s.id] == s {
		delete(r.sessions, s.id)
	}
}

func (r *Relay) leave(s *session, place byte) {
	r.mu.Lock()
	r.retirer(s, place)

	if s.proto == protoDuel {
		other := s.sides[1-place]
		r.mu.Unlock()

		// Hors du mutex : `post` le reprend, et le prendre deux fois se
		// bloquerait lui-meme. Dire au revoir a l'autre est ce qui lui permet
		// d'afficher « ton adversaire est parti » au lieu d'attendre dix
		// secondes un pas qui ne viendra pas.
		r.post(other, frame(fBye, []byte{1}))
		return
	}

	var tampon [placesMax]*side
	cibles := tampon[:0]
	for _, sd := range s.sides {
		if sd != nil {
			cibles = append(cibles, sd)
		}
	}
	// Le BYE porte la PLACE du partant, pas un motif : c'est la seule chose que
	// les restants ne peuvent pas deviner, et l'arbitre en a besoin pour retirer
	// quelqu'un de son classement.
	bye := frame(fBye, []byte{place})
	tableau := frame(fRoster, rosterCharge(s))

	if len(cibles) == 1 {
		// UN SALON A UNE PLACE N'EST PLUS UN SALON. On le retire de la table :
		// son identifiant redevient libre, et personne ne tombera dedans en
		// croyant entrer dans une arene.
		//
		// On ne coupe PAS pour autant la socket du dernier. Une socket fermee
		// par le relais est indiscernable d'une panne de reseau, et c'est
		// justement le joueur a qui il faut montrer une fin de mode : il a le
		// BYE et le tableau qui suivent, il raccrochera lui-meme.
		r.oublier(s)
		r.log.Info("arene : salon ferme, une seule place restante", "salon", s.id)
	}
	r.mu.Unlock()

	// Le BYE d'abord, le tableau ensuite : on apprend qui est parti, puis on
	// recoit la salle telle qu'elle est. L'ordre inverse ferait disparaitre
	// quelqu'un avant de dire qu'il s'en va.
	for _, sd := range cibles {
		r.post(sd, bye)
		r.post(sd, tableau)
	}
}
