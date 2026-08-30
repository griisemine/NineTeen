// Ce que ce fichier verifie, et pourquoi il existe.
//
// Le relais n'avait AUCUN test Go. Il en avait un en C — `tests/test_lockstep.c`
// — qui a l'immense qualite de faire tourner deux vrais clients contre un vrai
// relais, et le defaut de ne rien pouvoir dire des cas ou le relais doit
// REFUSER, ni des courses entre deux departs.
//
// Tout passe ici par un vrai `net.Listen("tcp", "127.0.0.1:0")` plutot que par
// `net.Pipe` : les delais de lecture, la fermeture d'une socket vue d'en face
// et le cadrage sur un flux qui peut se couper n'importe ou sont exactement ce
// qu'on veut eprouver, et `net.Pipe` n'en a aucun.
//
// La regle du fichier : ce qui est verifie du duel a deux places l'est parce
// que ce protocole est LIVRE et ne doit pas bouger d'un octet.
package duel

import (
	"encoding/binary"
	"errors"
	"io"
	"log/slog"
	"net"
	"sync"
	"testing"
	"time"
)

/* ========================================================================== */
/* L'outillage                                                                */
/* ========================================================================== */

// Les deux delais du fichier. Genereux devant ce qu'ils mesurent : un test qui
// echoue une fois sur cinquante sous `-race` ne prouve plus rien.
const (
	delaiRecu    = 3 * time.Second        // une trame attendue doit arriver
	delaiSilence = 300 * time.Millisecond // rien ne doit arriver
)

// relaisDeTest ouvre un relais sur un port tire par le systeme.
func relaisDeTest(t *testing.T) (*Relay, string) {
	t.Helper()
	r := New(slog.New(slog.NewTextHandler(io.Discard, &slog.HandlerOptions{Level: slog.LevelError})))
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("ecoute impossible : %v", err)
	}
	r.ln = ln
	go func() { _ = r.servir(ln) }()
	t.Cleanup(func() { _ = r.Close() })
	return r, ln.Addr().String()
}

type trame struct {
	typ    byte
	charge []byte
}

type pair struct {
	t *testing.T
	c net.Conn
}

func composer(t *testing.T, addr string) *pair {
	t.Helper()
	c, err := net.DialTimeout("tcp", addr, delaiRecu)
	if err != nil {
		t.Fatalf("connexion impossible : %v", err)
	}
	t.Cleanup(func() { _ = c.Close() })
	return &pair{t: t, c: c}
}

func (p *pair) envoyer(typ byte, charge []byte) {
	p.t.Helper()
	out := make([]byte, 3+len(charge))
	binary.LittleEndian.PutUint16(out[:2], uint16(len(charge)))
	out[2] = typ
	copy(out[3:], charge)
	_ = p.c.SetWriteDeadline(time.Now().Add(delaiRecu))
	if _, err := p.c.Write(out); err != nil {
		p.t.Fatalf("envoi impossible : %v", err)
	}
}

// brut envoie des octets tels quels, sans cadrage : c'est ce qu'il faut pour
// fabriquer une trame malformee.
func (p *pair) brut(b []byte) {
	p.t.Helper()
	_ = p.c.SetWriteDeadline(time.Now().Add(delaiRecu))
	if _, err := p.c.Write(b); err != nil {
		p.t.Fatalf("envoi impossible : %v", err)
	}
}

func (p *pair) lire(delai time.Duration) (trame, error) {
	var tete [3]byte
	_ = p.c.SetReadDeadline(time.Now().Add(delai))
	if _, err := io.ReadFull(p.c, tete[:]); err != nil {
		return trame{}, err
	}
	n := binary.LittleEndian.Uint16(tete[:2])
	charge := make([]byte, n)
	if n > 0 {
		if _, err := io.ReadFull(p.c, charge); err != nil {
			return trame{}, err
		}
	}
	return trame{typ: tete[2], charge: charge}, nil
}

// attendre lit jusqu'a trouver le type voulu, et echoue si rien ne vient.
func (p *pair) attendre(typ byte) []byte {
	p.t.Helper()
	fin := time.Now().Add(delaiRecu)
	for {
		reste := time.Until(fin)
		if reste <= 0 {
			p.t.Fatalf("trame 0x%02x jamais recue", typ)
		}
		tr, err := p.lire(reste)
		if err != nil {
			p.t.Fatalf("trame 0x%02x jamais recue : %v", typ, err)
		}
		if tr.typ == typ {
			return tr.charge
		}
	}
}

// vider ramasse tout ce qui arrive jusqu'au silence. C'est ce qui permet
// d'affirmer qu'une trame n'est PAS arrivee.
func (p *pair) vider() []trame {
	p.t.Helper()
	var recues []trame
	for {
		tr, err := p.lire(delaiSilence)
		if err != nil {
			return recues
		}
		recues = append(recues, tr)
	}
}

func (p *pair) silence(quoi string) {
	p.t.Helper()
	if recues := p.vider(); len(recues) != 0 {
		p.t.Fatalf("%s : %d trame(s) recue(s), dont une de type 0x%02x",
			quoi, len(recues), recues[0].typ)
	}
}

// mort verifie que le relais a ferme la connexion, et VITE. Le « vite » est
// l'assertion utile : un relais qui attendrait la charge annoncee resterait
// muet jusqu'au delai d'inactivite, soit dix secondes.
func (p *pair) mort(quoi string) {
	p.t.Helper()
	debut := time.Now()
	_, err := p.lire(delaiRecu)
	if err == nil {
		p.t.Fatalf("%s : le relais a repondu au lieu de fermer", quoi)
	}
	var netErr net.Error
	if errors.As(err, &netErr) && netErr.Timeout() {
		p.t.Fatalf("%s : la connexion est restee ouverte", quoi)
	}
	if ecoule := time.Since(debut); ecoule > idleTimeout {
		p.t.Fatalf("%s : fermeture apres %v, soit apres avoir attendu la charge", quoi, ecoule)
	}
}

func chargeHello(duel uint64, place byte) []byte {
	// 8 + 1 + 16 + 8 : exactement ce que `ns_lockstep_connect` ecrit.
	b := make([]byte, 33)
	binary.LittleEndian.PutUint64(b[:8], duel)
	b[8] = place
	copy(b[9:25], "flappy")
	copy(b[25:33], "normal")
	return b
}

func chargeJoin(salon uint64, place, attendues byte, pseudo string) []byte {
	b := make([]byte, joinMin)
	binary.LittleEndian.PutUint64(b[:8], salon)
	b[8] = place
	b[9] = attendues
	copy(b[10:], pseudo)
	return b
}

type inscrit struct {
	place  byte
	pseudo string
}

func lireRoster(t *testing.T, charge []byte) []inscrit {
	t.Helper()
	if len(charge) == 0 {
		t.Fatal("tableau des places vide")
	}
	n := int(charge[0])
	if len(charge) != 1+n*(1+pseudoLarg) {
		t.Fatalf("tableau des places : %d octets pour %d inscrits, attendu %d",
			len(charge), n, 1+n*(1+pseudoLarg))
	}
	out := make([]inscrit, 0, n)
	for i := 0; i < n; i++ {
		champ := charge[1+i*(1+pseudoLarg):]
		nom := champ[1 : 1+pseudoLarg]
		if nom[pseudoLarg-1] != 0 {
			t.Fatalf("pseudo de la place %d non termine par un zero", champ[0])
		}
		fin := 0
		for fin < len(nom) && nom[fin] != 0 {
			fin++
		}
		out = append(out, inscrit{place: champ[0], pseudo: string(nom[:fin])})
	}
	return out
}

// rejointArene ouvre une connexion, entre dans le salon, et rend le pair.
func rejointArene(t *testing.T, addr string, salon uint64, place, attendues byte, pseudo string) *pair {
	t.Helper()
	p := composer(t, addr)
	p.envoyer(fJoin, chargeJoin(salon, place, attendues, pseudo))
	return p
}

func sessionsOuvertes(r *Relay) (sessions, places int) {
	r.mu.Lock()
	defer r.mu.Unlock()
	return len(r.sessions), r.places
}

// attendreTableVide laisse aux departs le temps d'etre pris en compte : `leave`
// tourne dans la routine de la connexion, pas dans celle du test.
func attendreTableVide(t *testing.T, r *Relay) {
	t.Helper()
	fin := time.Now().Add(delaiRecu)
	for time.Now().Before(fin) {
		if s, p := sessionsOuvertes(r); s == 0 && p == 0 {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	s, p := sessionsOuvertes(r)
	t.Fatalf("la table n'est pas revenue a zero : %d session(s), %d place(s)", s, p)
}

/* ========================================================================== */
/* 1. Le duel a deux places, tel qu'il etait                                  */
/* ========================================================================== */

func TestDuelDeuxPlacesInchange(t *testing.T) {
	_, addr := relaisDeTest(t)

	un := composer(t, addr)
	deux := composer(t, addr)
	un.envoyer(fHello, chargeHello(4242, 0))
	deux.envoyer(fHello, chargeHello(4242, 1))

	graineUn := un.attendre(fStart)
	graineDeux := deux.attendre(fStart)

	if len(graineUn) != 8 {
		t.Fatalf("graine de %d octets, attendu 8", len(graineUn))
	}
	if string(graineUn) != string(graineDeux) {
		t.Fatalf("graines differentes : %x et %x", graineUn, graineDeux)
	}
	if graineUn[7]&0x80 != 0 {
		t.Fatalf("bit de poids fort present dans la graine %x", graineUn)
	}

	// LA TRAME EST RECOPIEE TELLE QUELLE. Pas de place inseree devant : c'est
	// l'arene qui prefixe, et le duel ne doit pas en heriter.
	entree := []byte{0x11, 0x22, 0x33, 0x44, 0x05, 0x03}
	un.envoyer(0x03, entree)

	recue, err := deux.lire(delaiRecu)
	if err != nil {
		t.Fatalf("l'entree n'a pas traverse : %v", err)
	}
	if recue.typ != 0x03 {
		t.Fatalf("type 0x%02x recu, attendu 0x03", recue.typ)
	}
	if string(recue.charge) != string(entree) {
		t.Fatalf("charge %x recue, attendu %x", recue.charge, entree)
	}
	un.silence("l'emetteur ne doit pas recevoir sa propre trame")

	// Le BYE du duel porte le motif 1, et pas une place. Deux clients installes
	// en dependent.
	_ = deux.c.Close()
	motif := un.attendre(fBye)
	if len(motif) != 1 || motif[0] != 1 {
		t.Fatalf("BYE de duel : charge %x, attendu 01", motif)
	}
}

func TestDuelPlaceDejaPrise(t *testing.T) {
	_, addr := relaisDeTest(t)

	un := composer(t, addr)
	un.envoyer(fHello, chargeHello(7, 0))

	intrus := composer(t, addr)
	intrus.envoyer(fHello, chargeHello(7, 0))
	intrus.mort("place de duel deja prise")
}

/* ========================================================================== */
/* 2. Le salon de quatre                                                      */
/* ========================================================================== */

func TestAreneStartApresLaDerniereArrivee(t *testing.T) {
	_, addr := relaisDeTest(t)

	const salon = 0xA1
	pairs := make([]*pair, 0, 4)
	for place := byte(0); place < 3; place++ {
		pairs = append(pairs, rejointArene(t, addr, salon, place, 4, "joueur"))
	}

	// Trois places sur quatre : le tableau circule, le START ne part pas.
	for i, p := range pairs {
		for _, tr := range p.vider() {
			if tr.typ == fStart {
				t.Fatalf("place %d : START recu alors que le salon est incomplet", i)
			}
			if tr.typ != fRoster {
				t.Fatalf("place %d : trame 0x%02x inattendue avant le START", i, tr.typ)
			}
		}
	}

	pairs = append(pairs, rejointArene(t, addr, salon, 3, 4, "joueur"))

	var graine []byte
	for i, p := range pairs {
		g := p.attendre(fStart)
		if len(g) != 8 {
			t.Fatalf("place %d : graine de %d octets, attendu 8", i, len(g))
		}
		if g[7]&0x80 != 0 {
			t.Fatalf("place %d : bit de poids fort present dans la graine %x", i, g)
		}
		if graine == nil {
			graine = g
			continue
		}
		if string(g) != string(graine) {
			t.Fatalf("place %d : graine %x, attendu %x", i, g, graine)
		}
	}
}

/* ========================================================================== */
/* 3. Le tableau des places grandit, puis retrecit                            */
/* ========================================================================== */

func TestAreneRosterGrandit(t *testing.T) {
	_, addr := relaisDeTest(t)

	const salon = 0xB2
	zero := rejointArene(t, addr, salon, 0, 3, "Nine")

	if liste := lireRoster(t, zero.attendre(fRoster)); len(liste) != 1 ||
		liste[0].place != 0 || liste[0].pseudo != "Nine" {
		t.Fatalf("premier tableau : %+v", liste)
	}

	deux := rejointArene(t, addr, salon, 2, 3, "Griise")
	for _, p := range []*pair{zero, deux} {
		liste := lireRoster(t, p.attendre(fRoster))
		if len(liste) != 2 || liste[0].place != 0 || liste[1].place != 2 ||
			liste[0].pseudo != "Nine" || liste[1].pseudo != "Griise" {
			t.Fatalf("tableau a deux : %+v", liste)
		}
	}

	un := rejointArene(t, addr, salon, 1, 3, "Mine")
	for _, p := range []*pair{zero, un, deux} {
		liste := lireRoster(t, p.attendre(fRoster))
		if len(liste) != 3 {
			t.Fatalf("tableau a trois : %+v", liste)
		}
		// Les places sortent DANS L'ORDRE, quel que soit l'ordre d'arrivee :
		// un client qui dessine le salon n'a pas a trier.
		for i, ins := range liste {
			if int(ins.place) != i {
				t.Fatalf("tableau a trois, entree %d : place %d", i, ins.place)
			}
		}
		_ = p.attendre(fStart)
	}

	// Et il retrecit.
	_ = un.c.Close()
	for _, p := range []*pair{zero, deux} {
		partie := p.attendre(fBye)
		if len(partie) != 1 || partie[0] != 1 {
			t.Fatalf("BYE d'arene : charge %x, attendu 01", partie)
		}
		liste := lireRoster(t, p.attendre(fRoster))
		if len(liste) != 2 || liste[0].place != 0 || liste[1].place != 2 {
			t.Fatalf("tableau apres depart : %+v", liste)
		}
	}
}

/* ========================================================================== */
/* 4. La diffusion : a tous les autres, et pas a soi                          */
/* ========================================================================== */

func TestAreneDiffusionSaufEmetteur(t *testing.T) {
	_, addr := relaisDeTest(t)

	const salon = 0xC3
	pairs := make([]*pair, 4)
	for place := byte(0); place < 4; place++ {
		pairs[place] = rejointArene(t, addr, salon, place, 4, "joueur")
	}
	for _, p := range pairs {
		_ = p.attendre(fStart)
		p.vider()
	}

	charge := []byte("sabotage")
	pairs[2].envoyer(0x20, charge)

	attendu := append([]byte{2}, charge...)
	for _, place := range []byte{0, 1, 3} {
		tr, err := pairs[place].lire(delaiRecu)
		if err != nil {
			t.Fatalf("place %d : rien recu (%v)", place, err)
		}
		if tr.typ != 0x20 {
			t.Fatalf("place %d : type 0x%02x, attendu 0x20", place, tr.typ)
		}
		// LA PLACE DE L'EMETTEUR EST DEVANT. C'est la decision du fichier :
		// personne ne peut signer a la place d'un autre.
		if string(tr.charge) != string(attendu) {
			t.Fatalf("place %d : charge %x, attendu %x", place, tr.charge, attendu)
		}
	}
	pairs[2].silence("l'emetteur ne doit pas recevoir sa propre trame")
}

// Les types en dessous de 0x20 appartiennent au relais. Un joueur qui en envoie
// un ne doit rien declencher : sans cela, n'importe qui fabrique un START, un
// tableau des places, ou le depart d'un autre.
func TestAreneTypesReservesNonRediffuses(t *testing.T) {
	_, addr := relaisDeTest(t)

	const salon = 0xC4
	zero := rejointArene(t, addr, salon, 0, 2, "zero")
	un := rejointArene(t, addr, salon, 1, 2, "un")
	for _, p := range []*pair{zero, un} {
		_ = p.attendre(fStart)
		p.vider()
	}

	zero.envoyer(fStart, []byte{1, 2, 3, 4, 5, 6, 7, 8})
	zero.envoyer(fRoster, []byte{0})
	zero.envoyer(fJoin, chargeJoin(salon, 1, 2, "usurpateur"))
	zero.envoyer(0x1f, []byte{0})
	un.silence("un type reserve au relais ne doit pas etre rediffuse")

	// La liaison n'est pas rompue pour autant : une trame libre passe toujours.
	zero.envoyer(0x21, []byte{9})
	tr, err := un.lire(delaiRecu)
	if err != nil {
		t.Fatalf("la trame libre n'a pas suivi : %v", err)
	}
	if tr.typ != 0x21 || string(tr.charge) != string([]byte{0, 9}) {
		t.Fatalf("trame libre : type 0x%02x, charge %x", tr.typ, tr.charge)
	}
}

/* ========================================================================== */
/* 5. Les entrees refusees                                                    */
/* ========================================================================== */

func TestAreneEntreesRefusees(t *testing.T) {
	_, addr := relaisDeTest(t)

	t.Run("place deja prise", func(t *testing.T) {
		const salon = 0xD1
		_ = rejointArene(t, addr, salon, 1, 4, "premier")
		intrus := rejointArene(t, addr, salon, 1, 4, "second")
		intrus.mort("place deja prise")
	})

	t.Run("place hors du salon", func(t *testing.T) {
		const salon = 0xD2
		p := rejointArene(t, addr, salon, 4, 4, "trop loin")
		p.mort("place egale au nombre de places")
	})

	t.Run("places attendues differentes", func(t *testing.T) {
		const salon = 0xD3
		premier := rejointArene(t, addr, salon, 0, 4, "quatre")
		_ = premier.attendre(fRoster)
		autre := rejointArene(t, addr, salon, 1, 6, "six")
		autre.mort("nombre de places different de la premiere annonce")
		// Le salon d'origine n'a pas bouge.
		premier.silence("un refus ne doit rien envoyer aux places en place")
	})

	t.Run("bornes du nombre de places", func(t *testing.T) {
		for _, attendues := range []byte{0, 1, 9, 255} {
			p := composer(t, addr)
			p.envoyer(fJoin, chargeJoin(0xD400+uint64(attendues), 0, attendues, "hors bornes"))
			p.mort("nombre de places hors bornes")
		}
	})

	t.Run("JOIN trop court", func(t *testing.T) {
		p := composer(t, addr)
		p.envoyer(fJoin, chargeJoin(0xD5, 0, 2, "court")[:joinMin-1])
		p.mort("JOIN trop court")
	})

	t.Run("un salon ne prend pas l'identifiant d'un duel", func(t *testing.T) {
		const id = 0xD6
		duelliste := composer(t, addr)
		duelliste.envoyer(fHello, chargeHello(id, 0))
		arenier := rejointArene(t, addr, id, 1, 4, "melange")
		arenier.mort("identifiant deja pris par un duel")
	})

	t.Run("un salon lance n'accepte plus personne", func(t *testing.T) {
		// TROIS places et non deux : a deux, le depart du second fermerait le
		// salon, et l'arrivant tardif en ouvrirait simplement un neuf. Ce qu'on
		// veut eprouver est une place LIBRE dans un salon VIVANT et deja lance.
		const salon = 0xD7
		pairs := make([]*pair, 3)
		for place := byte(0); place < 3; place++ {
			pairs[place] = rejointArene(t, addr, salon, place, 3, "joueur")
		}
		for _, p := range pairs {
			_ = p.attendre(fStart)
		}

		_ = pairs[2].c.Close()
		_ = pairs[0].attendre(fBye)

		// La place 2 est libre, le salon tourne. Un arrivant recevrait la
		// graine d'une partie commencee il y a longtemps : il serait au pas 0
		// pendant que les autres sont au pas 40 000, et le relais n'a rien pour
		// le rattraper. On refuse plutot que de laisser croire.
		tardif := rejointArene(t, addr, salon, 2, 3, "tardif")
		tardif.mort("entree dans un salon deja lance")
	})
}

/* ========================================================================== */
/* 6. Un depart en cours de partie ne ferme pas le salon                      */
/* ========================================================================== */

func TestAreneDepartEnCoursDePartie(t *testing.T) {
	r, addr := relaisDeTest(t)

	const salon = 0xE1
	pairs := make([]*pair, 3)
	for place := byte(0); place < 3; place++ {
		pairs[place] = rejointArene(t, addr, salon, place, 3, "joueur")
	}
	for _, p := range pairs {
		_ = p.attendre(fStart)
		p.vider()
	}

	_ = pairs[1].c.Close()

	for _, place := range []byte{0, 2} {
		partie := pairs[place].attendre(fBye)
		if len(partie) != 1 || partie[0] != 1 {
			t.Fatalf("place %d : BYE de charge %x, attendu 01", place, partie)
		}
		liste := lireRoster(t, pairs[place].attendre(fRoster))
		if len(liste) != 2 || liste[0].place != 0 || liste[1].place != 2 {
			t.Fatalf("place %d : tableau %+v", place, liste)
		}
	}

	// LE SALON VIT : les deux restants se parlent toujours, et il est toujours
	// dans la table.
	pairs[0].envoyer(0x20, []byte("toujours la"))
	tr, err := pairs[2].lire(delaiRecu)
	if err != nil {
		t.Fatalf("le salon s'est tu apres le depart : %v", err)
	}
	if tr.typ != 0x20 || tr.charge[0] != 0 {
		t.Fatalf("trame recue : type 0x%02x, charge %x", tr.typ, tr.charge)
	}
	if sessions, places := sessionsOuvertes(r); sessions != 1 || places != 2 {
		t.Fatalf("table : %d session(s), %d place(s), attendu 1 et 2", sessions, places)
	}

	// Le deuxieme depart, lui, ferme le salon : une place seule n'est pas une
	// arene. Le dernier garde sa socket — c'est a lui de raccrocher.
	_ = pairs[2].c.Close()
	_ = pairs[0].attendre(fBye)
	fin := time.Now().Add(delaiRecu)
	for time.Now().Before(fin) {
		if sessions, _ := sessionsOuvertes(r); sessions == 0 {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatal("le salon a une place est reste dans la table")
}

/* ========================================================================== */
/* 7. Deux departs SIMULTANES — le cas qui a deja emporte le processus        */
/* ========================================================================== */

// Le champ `closed` existe pour ca : sans lui, deux departs en meme temps
// finissent par un envoi sur un canal ferme, ce qui panique une routine et
// emporte tout le processus. A lancer avec `-race`.
func TestDepartsSimultanes(t *testing.T) {
	_, addr := relaisDeTest(t)

	const salons = 12

	var demarrage sync.WaitGroup
	var raccroche sync.WaitGroup
	depart := make(chan struct{})

	fermerEnsemble := func(p *pair) {
		raccroche.Add(1)
		go func() {
			defer raccroche.Done()
			<-depart
			_ = p.c.Close()
		}()
	}

	demarrage.Add(salons * 2)
	for salon := uint64(1); salon <= salons; salon++ {
		// Un duel et un salon de huit par tour : les deux protocoles ferment
		// par le meme chemin, et c'est ce chemin-la qu'on secoue.
		go func(id uint64) {
			defer demarrage.Done()
			un := composer(t, addr)
			deux := composer(t, addr)
			un.envoyer(fHello, chargeHello(id, 0))
			deux.envoyer(fHello, chargeHello(id, 1))
			fermerEnsemble(un)
			fermerEnsemble(deux)
		}(salon)

		go func(id uint64) {
			defer demarrage.Done()
			for place := byte(0); place < placesMax; place++ {
				fermerEnsemble(rejointArene(t, addr, id, place, placesMax, "adieu"))
			}
		}(salon + 1000)
	}
	demarrage.Wait()

	// Tout le monde raccroche au meme instant.
	close(depart)
	raccroche.Wait()
}

/* ========================================================================== */
/* 8. Une trame trop longue tombe sans allouer                                */
/* ========================================================================== */

func TestTrameTropLongue(t *testing.T) {
	_, addr := relaisDeTest(t)

	t.Run("des la premiere trame", func(t *testing.T) {
		p := composer(t, addr)
		// L'EN-TETE SEUL, sans un octet de charge. Si le relais allouait puis
		// lisait les 513 octets annonces, il resterait bloque jusqu'au delai
		// d'inactivite ; `mort` verifie qu'il ferme bien avant.
		var tete [3]byte
		binary.LittleEndian.PutUint16(tete[:2], frameMax+1)
		tete[2] = fJoin
		p.brut(tete[:])
		p.mort("longueur superieure a frameMax")
	})

	t.Run("en pleine arene", func(t *testing.T) {
		const salon = 0xF1
		zero := rejointArene(t, addr, salon, 0, 2, "zero")
		un := rejointArene(t, addr, salon, 1, 2, "un")
		_ = zero.attendre(fStart)
		_ = un.attendre(fStart)
		un.vider()

		var tete [3]byte
		binary.LittleEndian.PutUint16(tete[:2], 0xFFFF)
		tete[2] = 0x20
		zero.brut(tete[:])
		zero.mort("longueur superieure a frameMax en cours de salon")
	})

	// La charge d'une trame libre s'arrete un octet plus tot que `frameMax` :
	// la place de l'emetteur doit tenir devant. Rogner en silence donnerait au
	// destinataire une charge qui veut dire autre chose, donc on ferme.
	t.Run("charge de frameMax dans une trame libre", func(t *testing.T) {
		const salon = 0xF2
		zero := rejointArene(t, addr, salon, 0, 2, "zero")
		un := rejointArene(t, addr, salon, 1, 2, "un")
		_ = zero.attendre(fStart)
		_ = un.attendre(fStart)
		un.vider()

		zero.envoyer(0x20, make([]byte, frameMax))
		zero.mort("charge de 512 octets dans une trame diffusee")
		if partie := un.attendre(fBye); len(partie) != 1 || partie[0] != 0 {
			t.Fatalf("BYE : charge %x, attendu 00", partie)
		}
	})

	t.Run("charge maximale acceptee", func(t *testing.T) {
		const salon = 0xF3
		zero := rejointArene(t, addr, salon, 0, 2, "zero")
		un := rejointArene(t, addr, salon, 1, 2, "un")
		_ = zero.attendre(fStart)
		_ = un.attendre(fStart)
		un.vider()

		zero.envoyer(0x20, make([]byte, areneChargeMax))
		tr, err := un.lire(delaiRecu)
		if err != nil {
			t.Fatalf("une charge de %d octets aurait du passer : %v", areneChargeMax, err)
		}
		if len(tr.charge) != frameMax {
			t.Fatalf("charge de %d octets recue, attendu %d", len(tr.charge), frameMax)
		}
	})
}

/* ========================================================================== */
/* Le pseudo, et la table qui se vide                                         */
/* ========================================================================== */

func TestPseudoPropre(t *testing.T) {
	cas := []struct {
		nom     string
		brut    []byte
		attendu string
	}{
		{"coupe au premier zero", []byte("Nine\x00poubelle"), "Nine"},
		{"les octets de commande sautent", []byte("Ni\nne\x1b[2J"), "Nine[2J"},
		{"les accents passent", []byte("Frédérique"), "Frédérique"},
		{"vingt-quatre octets sont rognes a vingt-trois", []byte("abcdefghijklmnopqrstuvwx"),
			"abcdefghijklmnopqrstuvw"},
		{"une sequence UTF-8 n'est pas coupee en deux",
			[]byte("abcdefghijklmnopqrstuvé"), "abcdefghijklmnopqrstuv"},
	}
	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			if got := pseudoPropre(c.brut); got != c.attendu {
				t.Fatalf("pseudoPropre(%q) = %q, attendu %q", c.brut, got, c.attendu)
			}
		})
	}
}

func TestTableSeVide(t *testing.T) {
	r, addr := relaisDeTest(t)

	un := composer(t, addr)
	deux := composer(t, addr)
	un.envoyer(fHello, chargeHello(1, 0))
	deux.envoyer(fHello, chargeHello(1, 1))
	_ = un.attendre(fStart)

	pairs := make([]*pair, placesMax)
	for place := byte(0); place < placesMax; place++ {
		pairs[place] = rejointArene(t, addr, 2, place, placesMax, "joueur")
	}
	_ = pairs[0].attendre(fStart)

	if sessions, places := sessionsOuvertes(r); sessions != 2 || places != 2+placesMax {
		t.Fatalf("table : %d session(s), %d place(s), attendu 2 et %d", sessions, places, 2+placesMax)
	}

	_ = un.c.Close()
	_ = deux.c.Close()
	for _, p := range pairs {
		_ = p.c.Close()
	}
	attendreTableVide(t, r)
}
