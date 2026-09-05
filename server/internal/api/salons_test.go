// salons_test.go — le CONTRAT du service de salons, verifie sans base.
//
// CE QUE CE FICHIER PEUT ET NE PEUT PAS FAIRE, dit franchement.
//
// Les gestionnaires de salon parlent a PostgreSQL des qu'ils ont quelque chose a
// dire, et il n'y a pas de base dans cet environnement. Ce qui est verifie ici
// est donc tout ce qui se decide AVANT la premiere requete, plus les fonctions
// de rendu, qui sont pures :
//
//   - la forme exacte du JSON rendu, nom de champ par nom de champ, parce que
//     c'est le contrat que le client C lit et qu'une faute de frappe y est
//     invisible des deux cotes ;
//   - le fait que le classement live, qui est PUBLIC, ne laisse pas fuir
//     l'identifiant de relais, qui est la capacite ;
//   - les refus qui ne coutent pas une lecture : service sans relais annonce,
//     requete sans session, methode qui n'existe pas, code mal forme.
//
// Ce qui n'est PAS couvert ici et qu'il faut donc lire comme non teste : le
// chemin SQL lui-meme — transactions, verrous, cascade — et donc l'attribution
// de place en CONCURRENCE. Les regles qu'il execute, elles, sont couvertes par
// internal/salons, qui n'a besoin de rien pour tourner.
package api

import (
	"encoding/json"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"sort"
	"strconv"
	"strings"
	"testing"
	"time"

	"nineteen/internal/salons"
)

var tzero = time.Date(2026, 8, 31, 12, 0, 0, 0, time.UTC)

// relaisDEssai — une adresse annoncee, pour que le service ne reponde pas 503.
var relaisDEssai = RelaisPublic{Hote: "arcade.example", Port: 8099}

// serveurDEssai monte le routage sans magasin.
//
// `Store` reste nil, et c'est assume : aucun chemin exerce ici ne l'atteint. Un
// gestionnaire qui commencerait un jour par une requete le ferait savoir tres
// bruyamment, ce qui est exactement le signal qu'on veut.
func serveurDEssai(relais RelaisPublic) *Server {
	return New(Config{
		Logger: slog.New(slog.NewTextHandler(io.Discard, nil)),
		Relais: relais,
	})
}

func appel(t *testing.T, s *Server, methode, chemin string) *httptest.ResponseRecorder {
	t.Helper()
	rec := httptest.NewRecorder()
	s.ServeHTTP(rec, httptest.NewRequest(methode, chemin, nil))
	return rec
}

// salonDEssai — un salon de quatre places, deux joueurs assis, l'un muet.
func salonDEssai() *salons.Salon {
	return &salons.Salon{
		Code: "K7M3QP", Nom: "Chez Nine",
		Places: 4, Camps: 2, Prive: true, Etat: salons.Manche,
		Relais:       1234567890123,
		Proprietaire: 1, ProprietairePseudo: "Nine",
		CreeA: tzero, ChangeA: tzero.Add(10 * time.Second),
		Occupants: []salons.Occupant{
			{Place: 0, PlayerID: 1, Pseudo: "Nine", Camp: 0, Points: 1200,
				Fusibles: 3, Vivante: true, Borne: "snake-difficile",
				EntreA: tzero, BattuA: tzero.Add(time.Minute)},
			{Place: 2, PlayerID: 2, Pseudo: "Autre", Camp: 1, Points: 800,
				Fusibles: 1, Vivante: false, Borne: "aplomb-facile",
				EntreA: tzero, BattuA: tzero}, // muet depuis une minute
		},
	}
}

/* ========================================================================== */
/* La forme rendue                                                            */
/* ========================================================================== */

// clesDe rend les cles de premier niveau d'un objet JSON, triees.
func clesDe(t *testing.T, v any) []string {
	t.Helper()
	brut, err := json.Marshal(v)
	if err != nil {
		t.Fatalf("serialisation : %v", err)
	}
	var objet map[string]json.RawMessage
	if err := json.Unmarshal(brut, &objet); err != nil {
		t.Fatalf("relecture : %v", err)
	}
	cles := make([]string, 0, len(objet))
	for k := range objet {
		cles = append(cles, k)
	}
	sort.Strings(cles)
	return cles
}

func memesCles(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// TestLesChampsRendusSontCeuxDuContrat — le client C lit ces noms-la.
//
// Une faute de frappe dans une etiquette JSON est INVISIBLE des deux cotes : le
// serveur produit du JSON valide, le client lit ce qu'on lui a dit de lire, et
// le champ manquant prend simplement son repli — zero point, aucune place, un
// etat vide. C'est exactement le defaut que le journal d'entrees a deja
// rencontre (voir `inputsContentType`), et la reponse est la meme : clouer le
// contrat.
func TestLesChampsRendusSontCeuxDuContrat(t *testing.T) {
	sal := salonDEssai()
	maintenant := tzero.Add(time.Minute)

	cas := []struct {
		nom  string
		vue  any
		veut []string
	}{
		{
			nom:  "creation et entree",
			vue:  vueSalon(sal, 0, relaisDEssai, maintenant),
			veut: []string{"camps", "code", "etat", "nom", "occupants", "ok", "place", "places", "prive", "proprietaire", "relais"},
		},
		{
			nom:  "resume de liste",
			vue:  vueResume(sal, maintenant),
			veut: []string{"camps", "code", "depuisMs", "etat", "nom", "occupes", "places", "proprietaire"},
		},
		{
			nom: "classement live",
			vue: vueLive(sal, maintenant),
			// `camps` et `places` ont ete ajoutes : sans eux, la page web ne
			// peut pas lire la colonne du camp — il vaut la place en
			// individuel et 0 ou 1 en equipes, et elle affichait donc un tiret
			// pour tout le monde, y compris sur une manche a deux camps.
			veut: []string{"camps", "code", "depuisMs", "etat", "nom", "occupants", "ok", "places"},
		},
		{
			nom:  "battement",
			vue:  vueBattement(sal, maintenant),
			veut: []string{"etat", "occupants", "ok", "places"},
		},
		{
			nom:  "un occupant",
			vue:  salonOccupantJSON{},
			veut: []string{"borne", "camp", "fusibles", "place", "points", "pseudo", "vivante"},
		},
		{
			nom:  "l'adresse du relais",
			vue:  salonRelaisJSON{},
			veut: []string{"hote", "port", "salon"},
		},
	}

	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			got := clesDe(t, c.vue)
			if !memesCles(got, c.veut) {
				t.Errorf("champs rendus %v, attendu %v", got, c.veut)
			}
		})
	}
}

// TestLeClassementLiveNeLivrePasLaCapacite — LA propriete de securite du
// module, verifiee sur les octets rendus.
//
// La route `/live` est PUBLIQUE et repond meme pour un salon prive : connaitre
// le code suffit pour regarder. Ce qui rend cela sans consequence, c'est que
// l'identifiant de session sur le relais — celui qui donne reellement l'entree,
// puisque le relais n'a aucune notion de compte — n'est PAS dans cette reponse.
// Le jour ou quelqu'un ajoute un champ « pour deboguer », ce test le dit.
func TestLeClassementLiveNeLivrePasLaCapacite(t *testing.T) {
	sal := salonDEssai()
	maintenant := tzero.Add(time.Minute)

	brut, err := json.Marshal(vueLive(sal, maintenant))
	if err != nil {
		t.Fatal(err)
	}
	rendu := string(brut)
	if strings.Contains(rendu, strconv.FormatInt(sal.Relais, 10)) {
		t.Errorf("le classement live publie l'identifiant de relais :\n%s", rendu)
	}
	if strings.Contains(strings.ToLower(rendu), "relais") {
		t.Errorf("le classement live porte un champ « relais » :\n%s", rendu)
	}

	// Le resume de liste non plus, et pour la meme raison — en plus fort : la
	// liste est anonyme et parcourable.
	brut, err = json.Marshal(vueResume(sal, maintenant))
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(brut), strconv.FormatInt(sal.Relais, 10)) {
		t.Errorf("la liste publie l'identifiant de relais :\n%s", brut)
	}

	// La creation, elle, DOIT le livrer : c'est ce qui rend le salon jouable.
	// Sans ce controle, la propriete ci-dessus se satisferait d'un service qui
	// ne le livre a personne.
	brut, err = json.Marshal(vueSalon(sal, 0, relaisDEssai, maintenant))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(brut), strconv.FormatInt(sal.Relais, 10)) {
		t.Errorf("l'entree dans un salon ne livre pas de quoi joindre le relais :\n%s", brut)
	}
}

// TestLesOccupantsRendusSontLesVivants — un joueur qui s'est tu ne reste pas au
// tableau.
func TestLesOccupantsRendusSontLesVivants(t *testing.T) {
	sal := salonDEssai()

	// A l'instant zero, les deux battent : les deux paraissent.
	if n := len(vueOccupants(sal, tzero)); n != 2 {
		t.Errorf("%d occupants rendus a l'instant zero, attendu 2", n)
	}

	// Une minute plus tard, celui de la place 2 s'est tu depuis plus que
	// `TTLOccupant` : il disparait, et l'autre reste.
	vus := vueOccupants(sal, tzero.Add(time.Minute))
	if len(vus) != 1 {
		t.Fatalf("%d occupants rendus apres une minute de silence, attendu 1", len(vus))
	}
	if vus[0].Place != 0 || vus[0].Pseudo != "Nine" {
		t.Errorf("occupant rendu %+v, attendu celui de la place 0", vus[0])
	}
	if vus[0].Points != 1200 || vus[0].Fusibles != 3 || !vus[0].Vivante ||
		vus[0].Borne != "snake-difficile" || vus[0].Camp != 0 {
		t.Errorf("les valeurs de l'occupant ne sont pas recopiees telles quelles : %+v", vus[0])
	}
}

// TestDepuisMsCompteLEtat — l'age de l'ETAT COURANT, pas celui du salon.
func TestDepuisMsCompteLEtat(t *testing.T) {
	sal := salonDEssai() // cree a tzero, passe en manche a tzero + 10 s

	if got := depuisMs(sal, tzero.Add(30*time.Second)); got != 20_000 {
		t.Errorf("depuisMs = %d, attendu 20000 (la manche court depuis 20 s, "+
			"le salon existe depuis 30)", got)
	}

	// Une horloge de base legerement en avance ne doit pas afficher un compteur
	// qui remonte.
	if got := depuisMs(sal, tzero); got != 0 {
		t.Errorf("depuisMs = %d pour une date anterieure a la transition, attendu 0", got)
	}
}

/* ========================================================================== */
/* Les refus qui ne coutent pas une lecture                                   */
/* ========================================================================== */

// lesRoutes — les sept points d'entree, avec leur methode.
var lesRoutes = []struct {
	methode, chemin string
	authentifiee    bool
}{
	{http.MethodPost, "/api/v1/salons", true},
	{http.MethodGet, "/api/v1/salons", false},
	{http.MethodPost, "/api/v1/salons/K7M3QP/join", true},
	{http.MethodPost, "/api/v1/salons/K7M3QP/leave", true},
	{http.MethodDelete, "/api/v1/salons/K7M3QP", true},
	{http.MethodPost, "/api/v1/salons/K7M3QP/beat", true},
	{http.MethodGet, "/api/v1/salons/K7M3QP/live", false},
}

// TestSansRelaisAnnonceToutRepond503 — un salon dont on ne peut pas donner le
// relais est un salon qu'on ne peut pas jouer.
//
// Le refus vaut pour les SEPT routes, liste et classement live compris : un
// service qui n'ouvre pas de salons n'a pas de salons a montrer. Et il passe
// AVANT l'authentification, parce que c'est un fait de configuration identique
// pour tout le monde — demander des identifiants pour repondre ensuite « ce
// service n'existe pas ici » ferait chercher la faute du mauvais cote.
func TestSansRelaisAnnonceToutRepond503(t *testing.T) {
	s := serveurDEssai(RelaisPublic{})
	for _, r := range lesRoutes {
		t.Run(r.methode+" "+r.chemin, func(t *testing.T) {
			rec := appel(t, s, r.methode, r.chemin)
			if rec.Code != http.StatusServiceUnavailable {
				t.Errorf("code %d, attendu 503", rec.Code)
			}
			if !strings.Contains(rec.Body.String(), "relais") {
				t.Errorf("le refus ne dit pas ce qui manque : %s", rec.Body.String())
			}
		})
	}
}

// TestRelaisPublicAnnonce — la condition, telle que le service la lit.
func TestRelaisPublicAnnonce(t *testing.T) {
	cas := []struct {
		nom    string
		relais RelaisPublic
		veut   bool
	}{
		{"rien", RelaisPublic{}, false},
		{"un hote sans port", RelaisPublic{Hote: "arcade.example"}, false},
		{"un port sans hote", RelaisPublic{Port: 8099}, false},
		{"un port nul", RelaisPublic{Hote: "arcade.example", Port: 0}, false},
		{"les deux", RelaisPublic{Hote: "arcade.example", Port: 8099}, true},
	}
	for _, c := range cas {
		t.Run(c.nom, func(t *testing.T) {
			if got := c.relais.Annonce(); got != c.veut {
				t.Errorf("Annonce() = %v, attendu %v", got, c.veut)
			}
		})
	}
}

// TestUnePlaceExigeUnCompte — se montrer dans l'allee n'exige aucun compte ;
// tenir une place dans une manche, si.
//
// La difference est celle-la et pas une autre : une presence n'enleve rien a
// personne, une place se tient CONTRE les sept autres. C'est aussi ce qui permet
// au serveur de trancher le pseudo affiche au tableau, comme il le fait deja
// pour un pair authentifie.
func TestUnePlaceExigeUnCompte(t *testing.T) {
	s := serveurDEssai(relaisDEssai)
	for _, r := range lesRoutes {
		if !r.authentifiee {
			continue
		}
		t.Run(r.methode+" "+r.chemin, func(t *testing.T) {
			rec := appel(t, s, r.methode, r.chemin)
			if rec.Code != http.StatusUnauthorized {
				t.Errorf("code %d, attendu 401", rec.Code)
			}
		})
	}
}

// TestUnCodeMalFormeNeTouchePasLaBase — 404 sans requete, et sans jeton.
//
// Le magasin est nil dans ce test : si ce chemin l'atteignait, le test
// paniquerait au lieu de passer. C'est ce qui rend la verification credible.
//
// 404 et non 400 : du point de vue de qui tape, un code mal forme et un code
// inexistant sont la meme chose — un salon qu'on ne trouve pas.
func TestUnCodeMalFormeNeTouchePasLaBase(t *testing.T) {
	s := serveurDEssai(relaisDEssai)
	for _, code := range []string{"trop-long-pour-un-code", "K7M3Q", "K7M3QO", "%20%20%20%20%20%20"} {
		t.Run(code, func(t *testing.T) {
			rec := appel(t, s, http.MethodGet, "/api/v1/salons/"+code+"/live")
			if rec.Code != http.StatusNotFound {
				t.Errorf("code %d pour le code %q, attendu 404", rec.Code, code)
			}
		})
	}
}

// TestLesMethodesSontExplicites — un POST sur une route en lecture seule rend
// 405, pas un 404 qui ferait croire a une faute d'URL.
func TestLesMethodesSontExplicites(t *testing.T) {
	s := serveurDEssai(relaisDEssai)
	cas := []struct{ methode, chemin string }{
		{http.MethodPost, "/api/v1/salons/K7M3QP/live"},
		{http.MethodGet, "/api/v1/salons/K7M3QP/join"},
		{http.MethodGet, "/api/v1/salons/K7M3QP/beat"},
		{http.MethodDelete, "/api/v1/salons"},
	}
	for _, c := range cas {
		t.Run(c.methode+" "+c.chemin, func(t *testing.T) {
			rec := appel(t, s, c.methode, c.chemin)
			if rec.Code != http.StatusMethodNotAllowed {
				t.Errorf("code %d, attendu 405", rec.Code)
			}
		})
	}
}

// TestLeNomDeSalonPasseParLAssainisseurDesPseudos — reutilise, pas recopie.
//
// Ce qu'on verifie n'est pas que le nom est propre, mais qu'il est nettoye par
// LA MEME fonction que les pseudos et les noms de borne. Deux assainissements
// pour une meme sorte de champ finissent par diverger, et la divergence ne se
// voit que le jour ou l'un laisse passer ce que l'autre refusait.
func TestLeNomDeSalonPasseParLAssainisseurDesPseudos(t *testing.T) {
	cas := []struct{ entree, veut string }{
		{"Chez Nine", "Chez Nine"},
		{"  Chez Nine  ", "Chez Nine"},
		// Un retour a la ligne dans un nom est le debut d'une falsification de
		// journal, et le nom part dans les journaux du serveur.
		{"Chez\nNine", "ChezNine"},
		{"Salon \x00 nul", "Salon  nul"},
		{strings.Repeat("é", salons.NomMax+10), strings.Repeat("é", salons.NomMax)},
	}
	for _, c := range cas {
		if got := sanitizeShort(c.entree, salons.NomMax); got != c.veut {
			t.Errorf("sanitizeShort(%q) = %q, attendu %q", c.entree, got, c.veut)
		}
	}
}
