// Package runs — validation des parties, côté serveur.
//
// C'est la correction de fond du problème que l'audit résume ainsi : le serveur
// croyait un score calculé par le client. Aucune protection côté client ne peut
// corriger cela — pas même celle, ingénieuse, que la V1 avait mise en place
// (include/hashage.c : le score gardé en clair et sous forme hachée, avec des
// clés régénérées à chaque écriture). Un attaquant qui contrôle le processus
// contrôle les deux valeurs et les clés, et peut de toute façon supprimer le
// test lui-même.
//
// Le modèle retenu inverse la charge de la preuve :
//
//  1. le serveur ouvre la partie, tire la graine du générateur et un secret ;
//  2. le client joue et enregistre son journal d'événements ;
//  3. il renvoie le journal, scellé par HMAC-SHA256 avec le secret de la partie ;
//  4. le serveur vérifie le sceau, **recalcule** le score depuis les événements,
//     et rejette ce qui est physiquement implausible.
//
// Le HMAC n'empêche pas un joueur déterminé de fabriquer un journal cohérent :
// le secret transite jusqu'à lui. Il empêche la modification a posteriori, le
// rejeu, et le score inventé à la main. Ce qui écarte le journal fabriqué, c'est
// la validation d'invariants : un événement ne peut pas arriver avant le
// précédent, une partie ne peut pas durer trois secondes pour dix mille points,
// et le score doit découler des événements, pas les accompagner.
package runs

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"fmt"
	"math"
	"sort"
	"strings"
	"time"
)

// Event — un fait de jeu horodaté depuis le début de la partie.
type Event struct {
	// Millisecondes écoulées depuis le début de la partie, mesurées par le
	// client mais recoupées avec la durée réelle observée par le serveur.
	At   int64  `json:"t"`
	Kind string `json:"k"`
	// Valeur associée : nombre de lignes complétées, taille de l'astéroïde
	// détruit, etc. Son sens dépend de Kind et du jeu.
	Value int64 `json:"v"`
}

// Submission — ce que le client renvoie en fin de partie.
type Submission struct {
	Events       []Event `json:"events"`
	ClaimedScore int64   `json:"claimedScore"`
	DurationMs   int64   `json:"durationMs"`
	// Sceau HMAC-SHA256 encodé en base64, calculé sur la sérialisation
	// canonique des champs ci-dessus.
	Seal string `json:"seal"`
}

// Context — ce que le serveur sait de la partie, et que le client ne peut pas
// choisir.
type Context struct {
	Secret            []byte
	Seed              int64
	StartedAt         time.Time
	Now               time.Time
	GameSlug          string
	MaxPlausibleScore int64
}

type Verdict struct {
	Accepted bool
	Score    int64
	Reason   string
}

// CanonicalPayload construit la chaîne scellée par le HMAC.
//
// Le format est fixé ici et dupliqué à l'identique côté client (engine/net).
// Il est volontairement textuel et sans espaces : une sérialisation JSON serait
// ambiguë (ordre des clés, formatage des nombres), et une divergence d'un octet
// entre les deux côtés invaliderait toutes les parties.
func CanonicalPayload(seed int64, durationMs int64, claimed int64, events []Event) string {
	var b strings.Builder
	fmt.Fprintf(&b, "v1|%d|%d|%d|%d", seed, durationMs, claimed, len(events))
	for _, e := range events {
		fmt.Fprintf(&b, "|%d:%s:%d", e.At, e.Kind, e.Value)
	}
	return b.String()
}

// Seal calcule le sceau attendu.
func Seal(secret []byte, payload string) string {
	mac := hmac.New(sha256.New, secret)
	mac.Write([]byte(payload))
	return base64.RawStdEncoding.EncodeToString(mac.Sum(nil))
}

// Verify applique, dans l'ordre, les contrôles du moins cher au plus cher.
func Verify(ctx Context, sub Submission) Verdict {
	// --- 1. Bornes grossières, avant tout calcul ---
	const maxEvents = 200_000
	if len(sub.Events) > maxEvents {
		return Verdict{Reason: "journal trop volumineux"}
	}
	if sub.DurationMs < 0 {
		return Verdict{Reason: "durée négative"}
	}

	// --- 2. Sceau ---
	// Comparaison à temps constant, même si le contenu n'est pas secret : c'est
	// l'habitude qui compte, et elle ne coûte rien ici.
	expected := Seal(ctx.Secret, CanonicalPayload(ctx.Seed, sub.DurationMs, sub.ClaimedScore, sub.Events))
	if !hmac.Equal([]byte(expected), []byte(sub.Seal)) {
		return Verdict{Reason: "sceau invalide"}
	}

	// --- 3. Cohérence temporelle ---
	// La durée annoncée par le client ne peut pas dépasser le temps réellement
	// écoulé entre l'ouverture de la partie et sa soumission. Une marge couvre
	// la latence réseau et la dérive d'horloge ; en dessous, on refuse.
	realElapsed := ctx.Now.Sub(ctx.StartedAt)
	const clockTolerance = 30 * time.Second
	if time.Duration(sub.DurationMs)*time.Millisecond > realElapsed+clockTolerance {
		return Verdict{Reason: "durée annoncée supérieure au temps écoulé"}
	}
	// Une partie ouverte il y a trois heures et soumise maintenant est
	// suspecte : soit le client a été suspendu, soit la partie a été préparée.
	if realElapsed > 6*time.Hour {
		return Verdict{Reason: "partie trop ancienne"}
	}

	// Les événements doivent être ordonnés et tenir dans la durée annoncée.
	if !sort.SliceIsSorted(sub.Events, func(i, j int) bool {
		return sub.Events[i].At < sub.Events[j].At
	}) {
		return Verdict{Reason: "événements désordonnés"}
	}
	if n := len(sub.Events); n > 0 {
		if sub.Events[0].At < 0 {
			return Verdict{Reason: "horodatage négatif"}
		}
		if sub.Events[n-1].At > sub.DurationMs+1000 {
			return Verdict{Reason: "événement postérieur à la fin de partie"}
		}
	}

	// --- 4. Débit d'événements ---
	// Un humain ne produit pas mille actions par seconde. Le seuil est large :
	// il ne s'agit pas d'attraper un joueur rapide, mais un script.
	if sub.DurationMs > 0 {
		rate := float64(len(sub.Events)) / (float64(sub.DurationMs) / 1000.0)
		if rate > 200 {
			return Verdict{Reason: fmt.Sprintf("débit d'événements irréaliste (%.0f/s)", rate)}
		}
	} else if len(sub.Events) > 10 {
		return Verdict{Reason: "événements sans durée"}
	}

	// --- 5. Recalcul du score ---
	rules, ok := rulesFor(ctx.GameSlug)
	if !ok {
		return Verdict{Reason: "règles de jeu inconnues"}
	}

	score, err := rules.compute(sub, ctx)
	if err != nil {
		return Verdict{Reason: err.Error()}
	}

	// Le score annoncé doit correspondre à ce que le serveur recalcule. Un écart
	// n'est pas forcément une triche — cela peut être un désaccord de version —
	// mais dans les deux cas c'est le calcul du serveur qui fait foi.
	if score != sub.ClaimedScore {
		return Verdict{
			Reason: fmt.Sprintf("score annoncé %d, recalculé %d", sub.ClaimedScore, score),
		}
	}

	// --- 6. Plafond de plausibilité ---
	if ctx.MaxPlausibleScore > 0 && score > ctx.MaxPlausibleScore {
		return Verdict{Score: score, Reason: "score au-delà du plafond du jeu"}
	}

	return Verdict{Accepted: true, Score: score, Reason: "ok"}
}

/* ========================================================================== */
/* Règles par jeu                                                             */
/* ========================================================================== */

type gameRules struct {
	// Points attribués par type d'événement. Un type absent de la table est
	// refusé : un client ne peut pas inventer un événement qui rapporte.
	points map[string]int64
	// Points proportionnels à la valeur de l'événement, pour les jeux dont le
	// gain dépend d'une quantité (lignes simultanées, taille d'astéroïde).
	scaled map[string]int64
	// Événements qui ne rapportent RIEN mais qu'on accepte : les gestes du
	// joueur et la mort.
	//
	// Ils manquaient, et la table se contredisait : `flap` était limité en
	// fréquence — donc attendu — alors qu'un événement absent de `points` et de
	// `scaled` provoque un refus sec. Résultat, TOUTE partie de Flappy soumise
	// par le client était rejetée avec « événement inconnu « flap » », et rien
	// côté client ne pouvait le prévoir : il ne voyait qu'un envoi refusé.
	//
	// Ils comptent pour l'anti-triche : c'est sur eux que portent les limites de
	// fréquence, et c'est ce qui distingue une partie jouée d'un score inventé.
	silent map[string]bool
	// Nombre maximal d'événements de ce type par seconde de jeu.
	maxRatePerSecond map[string]float64
	minDurationMs    int64
	// Bornes de la valeur d'un événement proportionnel. Zéro des deux côtés
	// signifie « les valeurs par défaut », soit 0 à 10 000.
	//
	// Elles étaient écrites en dur, et Snake n'y rentre pas : un fruit géant
	// vaut dix fois sa valeur — jusqu'à 100 000 pour le muffin rose — et en
	// hardcore manger rapporte NÉGATIF (RATIO_GET_FRUIT_HARDCORE vaut −5, le
	// score venant au contraire des fruits qu'on laisse expirer). Le mode
	// hardcore de Snake était donc insoumettable avant d'exister.
	valueMin, valueMax int64
}

// Bornes effectives d'un événement proportionnel.
func (g gameRules) bounds() (int64, int64) {
	if g.valueMin == 0 && g.valueMax == 0 {
		return 0, 10_000
	}
	return g.valueMin, g.valueMax
}

func (g gameRules) compute(sub Submission, ctx Context) (int64, error) {
	if sub.DurationMs < g.minDurationMs {
		return 0, fmt.Errorf("partie trop courte (%d ms)", sub.DurationMs)
	}

	var total int64
	counts := map[string]int{}

	for i, e := range sub.Events {
		counts[e.Kind]++

		flat, hasFlat := g.points[e.Kind]
		scale, hasScale := g.scaled[e.Kind]
		if !hasFlat && !hasScale {
			if g.silent[e.Kind] {
				continue // compté pour la fréquence, sans valeur
			}
			return 0, fmt.Errorf("événement inconnu « %s » au rang %d", sanitizeKind(e.Kind), i)
		}
		if hasScale {
			// Borne sur la valeur : sans elle, un seul événement suffirait à
			// faire déborder l'entier.
			lo, hi := g.bounds()
			if e.Value < lo || e.Value > hi {
				return 0, fmt.Errorf("valeur hors bornes pour « %s »", sanitizeKind(e.Kind))
			}
			total += scale * e.Value
		}
		if hasFlat {
			total += flat
		}
		// Le débordement est vérifié À CHAQUE PAS — c'est lui qui protège. Le
		// total NÉGATIF, lui, ne l'est plus : il est légitime et transitoire
		// dans un jeu où certains gains sont des pertes, et le refuser en cours
		// de route rendait le hardcore de Snake impossible à soumettre.
		if total > math.MaxInt32 || total < math.MinInt32 {
			return 0, fmt.Errorf("score en débordement")
		}
	}

	seconds := float64(sub.DurationMs) / 1000.0
	if seconds > 0 {
		for kind, limit := range g.maxRatePerSecond {
			if rate := float64(counts[kind]) / seconds; rate > limit {
				return 0, fmt.Errorf("trop d'événements « %s » (%.1f/s, maximum %.1f)",
					sanitizeKind(kind), rate, limit)
			}
		}
	}

	// Un score final négatif vaut zéro, comme l'affichage de 2020 : on ne doit
	// rien à la salle en sortant.
	if total < 0 {
		total = 0
	}
	return total, nil
}

// sanitizeKind empêche qu'une chaîne venue du client se retrouve telle quelle
// dans un message d'erreur renvoyé au navigateur.
func sanitizeKind(k string) string {
	if len(k) > 24 {
		k = k[:24]
	}
	var b strings.Builder
	for _, r := range k {
		if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') || r == '_' {
			b.WriteRune(r)
		} else {
			b.WriteRune('?')
		}
	}
	return b.String()
}

// Table des règles. Les barèmes reprennent ceux du code d'origine, de sorte que
// les scores de la V15 restent comparables à ceux de la V1.
var rulesTable = map[string]gameRules{
	"flappy": {
		points:           map[string]int64{"pipe": 1},
		silent:           map[string]bool{"flap": true, "death": true},
		maxRatePerSecond: map[string]float64{"pipe": 3, "flap": 12},
		minDurationMs:    500,
	},
	"snake": {
		// Les fruits de 2020 n'ont pas tous la même valeur — de la fraise à 20
		// au muffin rose à 10 000, et dix fois plus en géant — donc le barème
		// est PROPORTIONNEL et la valeur voyage avec l'événement.
		//
		// La borne basse est négative, et ce n'est pas une facilité : en
		// hardcore, manger un fruit COÛTE cinq fois sa valeur et le score vient
		// des fruits qu'on laisse expirer. C'est la règle la plus surprenante de
		// 2020, et elle est délibérée.
		scaled:           map[string]int64{"fruit": 1},
		points:           map[string]int64{"bonus": 50},
		silent:           map[string]bool{"turn": true, "death": true},
		maxRatePerSecond: map[string]float64{"fruit": 6, "bonus": 1, "turn": 40},
		minDurationMs:    1000,
		valueMin:         -100_000,
		valueMax:         100_000,
	},
	"tetris": {
		silent: map[string]bool{"death": true},
		// Barème classique : quatre lignes d'un coup valent bien plus que
		// quatre lignes séparées.
		scaled:           map[string]int64{"lines": 100},
		points:           map[string]int64{"tetris": 400, "drop": 1},
		maxRatePerSecond: map[string]float64{"lines": 4, "tetris": 1, "drop": 20},
		minDurationMs:    2000,
	},
	"asteroid": {
		silent:           map[string]bool{"death": true},
		scaled:           map[string]int64{"rock": 20},
		points:           map[string]int64{"bonus": 100, "wave": 250},
		maxRatePerSecond: map[string]float64{"rock": 15, "bonus": 2, "wave": 0.5},
		minDurationMs:    2000,
	},
	"shooter": {
		silent:           map[string]bool{"death": true},
		scaled:           map[string]int64{"enemy": 15},
		points:           map[string]int64{"boss": 2000, "wave": 300},
		maxRatePerSecond: map[string]float64{"enemy": 20, "boss": 0.2, "wave": 0.5},
		minDurationMs:    2000,
	},
	"demineur": {
		silent:           map[string]bool{"death": true},
		points:           map[string]int64{"cell": 5, "flag": 2, "win": 500},
		maxRatePerSecond: map[string]float64{"cell": 15, "flag": 8, "win": 0.2},
		minDurationMs:    1000,
	},
	"pacman": {
		silent:           map[string]bool{"death": true},
		points:           map[string]int64{"pellet": 10, "power": 50, "ghost": 200, "level": 1000},
		maxRatePerSecond: map[string]float64{"pellet": 10, "power": 1, "ghost": 2, "level": 0.1},
		minDurationMs:    2000,
	},
	"piano": {
		silent:           map[string]bool{"death": true},
		points:           map[string]int64{"note": 5, "combo": 25},
		maxRatePerSecond: map[string]float64{"note": 14, "combo": 4},
		minDurationMs:    1000,
	},
}

// rulesFor accepte les identifiants de jeu suffixés par la difficulté
// ("flappy-hard" utilise les règles de "flappy").
func rulesFor(slug string) (gameRules, bool) {
	if r, ok := rulesTable[slug]; ok {
		return r, true
	}
	if base, _, found := strings.Cut(slug, "-"); found {
		r, ok := rulesTable[base]
		return r, ok
	}
	return gameRules{}, false
}
