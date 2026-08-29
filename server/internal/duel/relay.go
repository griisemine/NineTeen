// Package duel relaie les trames d'un duel en pas verrouille entre deux
// clients, et rien de plus.
//
// Ce qu'il fait
// -------------
// Il apparie deux clients qui presentent le meme identifiant de duel, leur
// donne une graine commune, puis RECOPIE chaque trame de l'un vers l'autre.
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
package duel

import (
	"crypto/rand"
	"encoding/binary"
	"errors"
	"io"
	"log/slog"
	"net"
	"sync"
	"time"
)

const (
	// La charge maximale d'une trame. La plus grosse que le protocole definisse
	// est le HELLO, a 33 octets. 512 laisse de la place pour en ajouter, et
	// borne ce qu'un pair peut faire allouer : un decodeur qui croit une
	// longueur annoncee sans la borner est le defaut de base d'un lecteur de
	// socket.
	frameMax = 512

	// Un duel ou personne ne parle est un duel mort. Le pas verrouille envoie
	// une trame par pas de simulation, soit 120 par seconde : dix secondes de
	// silence ne sont pas de la latence, c'est une deconnexion.
	idleTimeout = 10 * time.Second

	// Le temps laisse au second joueur pour arriver.
	pairTimeout = 60 * time.Second

	// Plafond de duels simultanes. Sans lui, ouvrir des connexions avec des
	// identifiants differents suffirait a faire grossir la table sans fin.
	maxDuels = 256

	fHello = 0x01
	fStart = 0x02
	fBye   = 0x05
)

type side struct {
	slot byte
	out  chan []byte
	// `closed` est garde par le mutex du relais, et il existe pour une raison
	// precise : sans lui, deux departs SIMULTANES se terminent par un envoi sur
	// un canal deja ferme, ce qui panique la routine et emporte le processus.
	// Un relais dont on peut provoquer l'arret en raccrochant des deux cotes en
	// meme temps n'est pas un relais.
	closed bool
}

type session struct {
	id    uint64
	sides [2]*side
}

// Relay est le serveur TCP du duel. Il est independant du serveur HTTP : il
// n'ecoute pas le meme port, ne partage aucun etat avec lui, et son arret ne
// touche pas au classement.
type Relay struct {
	log *slog.Logger

	mu       sync.Mutex
	sessions map[uint64]*session

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

// post depose une trame dans la file d'un cote, sous le mutex, et seulement si
// ce cote est encore ouvert. Tous les envois passent par ici — c'est ce qui
// rend l'invariant verifiable en un seul endroit.
//
// Une file pleine fait TOMBER la trame plutot que bloquer : le pair ne lit
// plus, le pas verrouille s'arretera de lui-meme, et un arret se voit la ou un
// blocage serait un gel silencieux.
func (r *Relay) post(sd *side, b []byte) {
	if sd == nil {
		return
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if sd.closed {
		return
	}
	select {
	case sd.out <- b:
	default:
	}
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

func (r *Relay) serve(c net.Conn) {
	defer c.Close()
	_ = c.SetReadDeadline(time.Now().Add(pairTimeout))

	typ, body, err := readFrame(c)
	if err != nil || typ != fHello || len(body) < 9 {
		r.log.Debug("duel : premiere trame invalide", "err", err)
		return
	}
	id := binary.LittleEndian.Uint64(body[:8])
	slot := body[8]
	if slot > 1 {
		return
	}

	s, me, err := r.join(id, slot)
	if err != nil {
		r.log.Debug("duel : appariement refuse", "duel", id, "slot", slot, "err", err)
		return
	}
	defer r.leave(s, slot)

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

	for {
		_ = c.SetReadDeadline(time.Now().Add(idleTimeout))
		typ, body, err := readFrame(c)
		if err != nil {
			break
		}
		if typ == fHello {
			continue // un seul HELLO par connexion
		}
		r.forward(s, slot, frame(typ, body))
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

func (r *Relay) join(id uint64, slot byte) (*session, *side, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	s := r.sessions[id]
	if s == nil {
		if len(r.sessions) >= maxDuels {
			return nil, nil, errors.New("trop de duels ouverts")
		}
		s = &session{id: id}
		r.sessions[id] = s
	}
	if s.sides[slot] != nil {
		return nil, nil, errors.New("place deja prise")
	}
	me := &side{slot: slot, out: make(chan []byte, 256)}
	s.sides[slot] = me

	// Les deux sont la : on tire la graine et on lance.
	if s.sides[0] != nil && s.sides[1] != nil {
		var seed [8]byte
		if _, err := rand.Read(seed[:]); err != nil {
			return nil, nil, err
		}
		// Le bit de poids fort est efface : la graine traverse un `float`
		// nulle part, mais elle traverse des entiers signes, et un 64e bit a
		// deja coute une session de debogage a ce projet.
		seed[7] &= 0x7f
		start := frame(fStart, seed[:])
		// Envoi direct : on tient deja le mutex, et les deux cotes viennent
		// d'etre crees, donc aucun n'est ferme.
		for _, sd := range s.sides {
			select {
			case sd.out <- start:
			default:
			}
		}
		r.log.Info("duel : apparie", "duel", id)
	}
	return s, me, nil
}

func (r *Relay) forward(s *session, from byte, b []byte) {
	r.mu.Lock()
	other := s.sides[1-from]
	r.mu.Unlock()
	r.post(other, b)
}

func (r *Relay) leave(s *session, slot byte) {
	r.mu.Lock()
	s.sides[slot] = nil
	other := s.sides[1-slot]
	if other == nil {
		delete(r.sessions, s.id)
	}
	r.mu.Unlock()

	// Hors du mutex : `post` le reprend, et le prendre deux fois se bloquerait
	// lui-meme. Dire au revoir a l'autre est ce qui lui permet d'afficher « ton
	// adversaire est parti » au lieu d'attendre dix secondes un pas qui ne
	// viendra pas.
	r.post(other, frame(fBye, []byte{1}))
}
