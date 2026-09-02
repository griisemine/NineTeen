// saison.go — les routes du classement CLASSE.
//
// Trois routes, et aucune ne calcule quoi que ce soit : tout le bareme vit dans
// `internal/saison`, qui ne touche ni base ni reseau et se teste pour cette
// raison. Ici on lit, on assemble, on rend.
//
// AUCUNE DE CES ROUTES NE DEMANDE DE COMPTE. Un classement qu'il faut se
// connecter pour lire est un classement que personne ne regarde, et c'est
// exactement ce qui fait venir : voir le podium avant d'y etre. La session,
// quand elle est la, ajoute seulement le bloc « moi ».
package api

import (
	"errors"
	"net/http"
	"strings"
	"time"

	"nineteen/internal/saison"
	"nineteen/internal/store"
)

// Combien de lignes le classement rend au plus.
//
// Cinquante, parce que c'est ce qu'une page affiche sans devenir un annuaire,
// et parce que le calcul, lui, porte sur TOUT le monde : le rang d'un joueur
// non affiche reste juste, et sa fiche le montre.
const lignesClassement = 50

// creneauPublic — une borne du serveur, vue depuis la saison.
type creneauPublic struct {
	Jeu            string  `json:"jeu"`
	JeuNom         string  `json:"jeuNom"`
	Difficulte     string  `json:"difficulte"`
	Multiplicateur float64 `json:"multiplicateur"`
	Joueurs        int     `json:"joueurs"`
	Meneur         string  `json:"meneur,omitempty"`
	Score          int64   `json:"score,omitempty"`
}

// lireSaison fait le travail commun aux trois routes : borner la fenetre, lire
// les meilleurs, classer.
func (s *Server) lireSaison(r *http.Request, cle string) ([]saison.Ligne, []saison.Meilleur, []saison.CreneauServeur, error) {
	debut, fin, ok := saison.Bornes(cle)
	if !ok {
		return nil, nil, nil, errors.New("saison inconnue")
	}
	meilleurs, err := s.store.SaisonMeilleurs(r.Context(), debut, fin)
	if err != nil {
		return nil, nil, nil, err
	}
	jeux, err := s.store.Games(r.Context())
	if err != nil {
		return nil, nil, nil, err
	}
	tous := make([]saison.CreneauServeur, 0, len(jeux))
	for _, g := range jeux {
		tous = append(tous, saison.CreneauServeur{
			Jeu: g.Slug, JeuNom: g.Name, Difficulte: g.Difficulty,
			Multiplicateur: g.Multiplier,
		})
	}
	return saison.Classer(meilleurs), meilleurs, tous, nil
}

// creneauxDeLaSaison rend l'etat de chaque borne : qui la tient, avec quel
// score, et combien s'y sont mesures.
//
// Les bornes DESERTES y figurent, avec zero joueur, et c'est le point : elles
// sont le raccourci le plus court vers le haut du classement, et une liste qui
// ne montrerait que les bornes jouees les cacherait precisement a ceux qui en
// ont le plus besoin.
func creneauxDeLaSaison(meilleurs []saison.Meilleur, tous []saison.CreneauServeur) []creneauPublic {
	type tete struct {
		pseudo string
		score  int64
		quand  time.Time
		nb     int
	}
	etat := map[string]*tete{}
	for _, m := range meilleurs {
		t := etat[m.Jeu]
		if t == nil {
			t = &tete{}
			etat[m.Jeu] = t
		}
		t.nb++
		if m.Score > t.score || (m.Score == t.score && m.Quand.Before(t.quand)) {
			t.pseudo, t.score, t.quand = m.Pseudo, m.Score, m.Quand
		}
	}

	out := make([]creneauPublic, 0, len(tous))
	for _, c := range tous {
		p := creneauPublic{
			Jeu: c.Jeu, JeuNom: c.JeuNom, Difficulte: c.Difficulte,
			Multiplicateur: c.Multiplicateur,
		}
		if t := etat[c.Jeu]; t != nil {
			p.Joueurs, p.Meneur, p.Score = t.nb, t.pseudo, t.score
		}
		out = append(out, p)
	}
	return out
}

// allegee rend une ligne sans son detail : le classement affiche cinquante
// lignes, et y joindre les quatorze creneaux de chacune multiplierait la
// reponse par quinze pour une information que personne ne lit a ce niveau.
func allegee(l saison.Ligne) saison.Ligne {
	l.Creneaux = nil
	l.Prochain = nil
	return l
}

// handleSaison — le classement d'une saison, la courante par defaut.
func (s *Server) handleSaison(w http.ResponseWriter, r *http.Request) {
	maintenant := time.Now().UTC()
	cle := r.PathValue("cle")
	if cle == "" {
		cle = r.URL.Query().Get("cle")
	}
	if cle == "" {
		cle = saison.Cle(maintenant)
	}
	if !saison.CleValide(cle) {
		s.fail(w, r, http.StatusBadRequest, "saison inconnue", nil)
		return
	}

	lignes, meilleurs, tous, err := s.lireSaison(r, cle)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "classement indisponible", err)
		return
	}

	// LE PODIUM GARDE SON DETAIL. Trois lignes, et ce sont les seules que
	// quelqu'un regarde vraiment : savoir QUE Mine est premier n'apprend rien,
	// savoir qu'il l'est grace a trois premieres places sur les bornes dures
	// apprend comment y arriver.
	podium := []saison.Ligne{}
	for i := 0; i < len(lignes) && i < 3; i++ {
		podium = append(podium, lignes[i])
	}

	classement := make([]saison.Ligne, 0, lignesClassement)
	for i := 0; i < len(lignes) && i < lignesClassement; i++ {
		classement = append(classement, allegee(lignes[i]))
	}

	reponse := map[string]any{
		"ok": true,
		"saison": map[string]any{
			"cle":           cle,
			"libelle":       saison.Libelle(cle),
			"precedente":    saison.Precedente(cle),
			"encours":       cle == saison.Cle(maintenant),
			"joursRestants": saison.JoursRestants(cle, maintenant),
		},
		"placementRequis": saison.PlacementRequis,
		"paliers":         saison.Paliers(),
		"joueurs":         len(lignes),
		"podium":          podium,
		"classement":      classement,
		"creneaux":        creneauxDeLaSaison(meilleurs, tous),
	}

	// LE BLOC « MOI », seulement si une session accompagne la requete. Le
	// classement se lit sans compte ; ce bloc est ce qui le rend personnel.
	if sess, err := s.authenticate(r); err == nil {
		if moi := saison.Trouver(lignes, sess.Username); moi != nil {
			reponse["moi"] = map[string]any{
				"ligne":   moi,
				"vierges": saison.Vierges(sess.Username, meilleurs, tous),
			}
		} else {
			// Connecte mais rien joue cette saison : on le dit, avec les bornes
			// ou commencer. C'est le seul moment ou un classement vide est utile.
			reponse["moi"] = map[string]any{
				"ligne":   nil,
				"vierges": saison.Vierges(sess.Username, meilleurs, tous),
			}
		}
	}

	writeJSON(w, http.StatusOK, reponse)
}

// handleJoueur — la fiche d'un joueur : son palmares de toujours, et sa saison.
func (s *Server) handleJoueur(w http.ResponseWriter, r *http.Request) {
	pseudo := strings.TrimSpace(r.PathValue("pseudo"))
	if pseudo == "" || len(pseudo) > 24 {
		s.fail(w, r, http.StatusBadRequest, "pseudo invalide", nil)
		return
	}

	fiche, err := s.store.Joueur(r.Context(), pseudo)
	if errors.Is(err, store.ErrNotFound) {
		s.fail(w, r, http.StatusNotFound, "joueur inconnu", nil)
		return
	}
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "fiche indisponible", err)
		return
	}

	maintenant := time.Now().UTC()
	cle := saison.Cle(maintenant)
	// Les paliers voyagent avec la fiche : sans eux la page ne saurait pas
	// dessiner la jauge vers le palier suivant, et devrait faire une seconde
	// requete pour une liste de sept elements qui ne changent jamais.
	reponse := map[string]any{
		"ok":              true,
		"fiche":           fiche,
		"paliers":         saison.Paliers(),
		"placementRequis": saison.PlacementRequis,
	}

	// LA SAISON EN COURS ET CELLE D'AVANT, et pas davantage.
	//
	// Deux, parce que ce sont les deux seules qu'on compare : celle qu'on joue
	// et celle qu'on vient de finir. Remonter plus loin demanderait de classer
	// autant de mois que le serveur en a vus, a chaque affichage d'une fiche.
	for nom, c := range map[string]string{"saison": cle, "precedente": saison.Precedente(cle)} {
		lignes, meilleurs, tous, err := s.lireSaison(r, c)
		if err != nil {
			continue
		}
		bloc := map[string]any{
			"cle":     c,
			"libelle": saison.Libelle(c),
			"joueurs": len(lignes),
			"ligne":   saison.Trouver(lignes, fiche.Pseudo),
		}
		if nom == "saison" {
			bloc["vierges"] = saison.Vierges(fiche.Pseudo, meilleurs, tous)
			bloc["joursRestants"] = saison.JoursRestants(c, maintenant)
		}
		reponse[nom] = bloc
	}

	writeJSON(w, http.StatusOK, reponse)
}
