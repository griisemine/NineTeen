/*
 * classement.js, les trois tableaux de la page de classement.
 *
 * POURQUOI CETTE PAGE A ETE REECRITE. Le classement general vivait sur la page
 * d'accueil, avec son entete de tableau ecrit en HTML et ses lignes ecrites
 * ici ; le direct vivait sur une seconde page, avec ses propres colonnes. Deux
 * endroits decidaient des memes choses, et personne ne savait lequel regarder.
 *
 * Maintenant : une page, aucune ligne dans le HTML, et un seul constructeur de
 * tableau (NS.tableau) nourri par trois descriptions de colonnes. Une colonne
 * se change a un seul endroit, et son entete suit toute seule.
 *
 * DEUX SOURCES, JAMAIS MELANGEES. Le classement general ne contient que des
 * parties RECALCULEES par le serveur a partir de leur journal, donc un score
 * invente n'y entre pas. Le direct, lui, est publie par les joueurs pendant la
 * manche et relaye par le serveur. C'est suffisant pour regarder une manche, et
 * ce serait insuffisant pour un palmares. La page le dit, et ce n'est pas de la
 * decoration.
 */

"use strict";

(function () {

const { api, elt, vider, etat, tableau, nombre, duree,
        quand, lienJoueur, medaille, podium, echelle, jauge, surCompte } = window.NS;

/*
 * Deux secondes pour une manche, dix pour la liste des salons.
 *
 * Le jeu bat a 4 Hz parce qu'il en a besoin pour jouer. Un spectateur n'en a
 * pas besoin : la lame tombe toutes les 45 s, et rafraichir vingt fois entre
 * deux lames ne montre rien de plus. Le seul cout d'un intervalle court serait
 * de multiplier les requetes d'une page qu'on laisse ouverte.
 */
const PERIODE_MANCHE_MS = 2000;
const PERIODE_OUVERTS_MS = 10000;

/* ==========================================================================
   La saison classee
   ==========================================================================
   Tout le bareme est calcule par le serveur, dans `internal/saison`, qui se
   teste sans base. Ici on ne fait que peindre : aucune ligne de ce fichier ne
   decide combien vaut une place.
   ========================================================================== */

const teteSaison = document.getElementById("saison-tete");
const zonePodium = document.getElementById("podium");
const zoneMoi = document.getElementById("moi");
const etatSaison = document.getElementById("etat-saison");
const ecranSaison = document.getElementById("ecran-saison");
const zoneEchelle = document.getElementById("echelle");
const ecranCreneaux = document.getElementById("ecran-creneaux");

/* Le pseudo connecte, pour surligner sa ligne dans le classement. Il arrive
 * apres coup : on redessine quand il tombe plutot que d'attendre, sinon la
 * page resterait vide pour un visiteur anonyme. */
let moiPseudo = "";
let derniereSaison = null;

const COLONNES_SAISON = [
    { titre: "Rang",   classe: "c-rang",  valeur: (l) => String(l.rang) },
    { titre: "Joueur", classe: "c-nom",   noeud:  (l) => lienJoueur(l.pseudo) },
    { titre: "Palier", classe: "c-nom",   noeud:  (l) => medaille(l.palier) },
    { titre: "Points", classe: "c-score", valeur: (l) => nombre(l.points) },
    /* L'ECART EST UNE COLONNE, et c'est le nombre le plus motivant du tableau.
       Le premier n'a personne au-dessus : le serveur lui rend son AVANCE, et
       on l'ecrit avec un plus pour qu'on ne les confonde pas. */
    { titre: "Ecart",  classe: "c-score",
      valeur: (l) => l.rang === 1 ? "+" + nombre(l.ecart) : nombre(l.ecart) },
    { titre: "1res places", classe: "c-score", valeur: (l) => nombre(l.ors) },
    { titre: "Parties", classe: "c-score", valeur: (l) => nombre(l.parties) },
];

const COLONNES_CRENEAUX = [
    { titre: "Borne",  classe: "c-nom", valeur: (c) => c.jeuNom },
    { titre: "Regime", classe: "c-nom", valeur: (c) => regime(c.difficulte) },
    { titre: "Valeur", classe: "c-score",
      valeur: (c) => "x " + String(c.multiplicateur).replace(".", ",") },
    { titre: "Tenue par", classe: "c-nom",
      noeud: (c) => c.meneur ? lienJoueur(c.meneur) : elt("span", "vide-mini", "personne") },
    { titre: "Score", classe: "c-score", valeur: (c) => c.score ? nombre(c.score) : "" },
    { titre: "Joueurs", classe: "c-score", valeur: (c) => nombre(c.joueurs) },
];

function peindreTete(data) {
    vider(teteSaison);
    const s = data.saison || {};

    teteSaison.appendChild(elt("span", "nom", s.libelle || s.cle || ""));
    teteSaison.appendChild(elt("span", "compte",
        nombre(data.joueurs || 0) + " joueurs classes"));

    const fin = elt("span", "fin");
    if (s.encours) {
        /* L'URGENCE. Un classement sans fin ne se dispute pas : c'est la date
           limite qui fait revenir le 29. */
        const j = Number(s.joursRestants || 0);
        fin.appendChild(elt("strong", "reste",
            j <= 0 ? "dernier jour" : j + (j > 1 ? " jours restants" : " jour restant")));
    } else {
        fin.appendChild(elt("strong", "reste", "saison close"));
    }
    teteSaison.appendChild(fin);
}

/*
 * LE BLOC PERSONNEL. C'est la moitie qui transforme un tableau en objectif :
 * ou j'en suis, ce qu'il me manque pour le palier suivant, et LA borne ou
 * jouer maintenant.
 */
function peindreMoi(data) {
    vider(zoneMoi);
    const moi = data.moi;
    if (!moi) return;

    const bloc = elt("div", "fiche");
    const ligne = moi.ligne;

    if (!ligne) {
        bloc.appendChild(elt("h2", null, "Votre saison n'a pas commence"));
        bloc.appendChild(elt("p", "depuis",
            "Trois parties valides suffisent a entrer au classement."));
    } else {
        bloc.appendChild(elt("h2", null, "Votre saison"));
        const stats = elt("ul", "stats");
        stats.appendChild(stat(nombre(ligne.points), "points"));
        stats.appendChild(stat(String(ligne.rang), "rang"));
        stats.appendChild(stat(nombre(ligne.ors), "premieres places"));
        stats.appendChild(stat(nombre(ligne.parties), "parties"));
        bloc.appendChild(stats);

        const p = elt("p", "depuis");
        p.appendChild(medaille(ligne.palier));
        if (ligne.palier && ligne.palier.niveau === 0) {
            p.appendChild(elt("span", null,
                "  il vous manque " + (data.placementRequis - ligne.parties) +
                " partie(s) pour etre classe"));
        }
        bloc.appendChild(p);

        const j = jauge(data.paliers, ligne.points);
        if (j) {
            bloc.appendChild(j.barre);
            bloc.appendChild(elt("p", "note",
                nombre(j.manque) + " points avant " + j.suivant.nom));
        }

        if (ligne.rang > 1) {
            bloc.appendChild(elt("p", "note",
                nombre(ligne.ecart) + " points vous separent du rang au-dessus."));
        }
    }

    /* LE CONSEIL, et il est nomme. « Joue plus » ne donne aucune prise, « bats
       420 sur Piano, la place vaut 15 points » en donne une. */
    const conseil = meilleurConseil(moi);
    if (conseil) bloc.appendChild(conseil);

    zoneMoi.appendChild(bloc);
}

function stat(valeur, quoi) {
    const li = elt("li");
    li.appendChild(elt("strong", "valeur", valeur));
    li.appendChild(elt("span", "quoi", quoi));
    return li;
}

/*
 * Entre grimper d'une place la ou l'on joue deja et prendre une borne que
 * personne ne tient, on propose CE QUI RAPPORTE LE PLUS. Le serveur rend les
 * deux chiffres, la comparaison se fait ici parce qu'elle ne concerne que
 * l'affichage.
 */
function meilleurConseil(moi) {
    const pas = moi.ligne && moi.ligne.prochain ? moi.ligne.prochain : null;
    const vierges = moi.vierges || [];
    const libre = vierges.length ? vierges[0] : null;

    const gainPas = pas ? pas.gain : 0;
    const gainLibre = libre ? libre.gain : 0;
    if (gainPas <= 0 && gainLibre <= 0) return null;

    const n = elt("p", "conseil");
    if (gainLibre >= gainPas && libre) {
        n.appendChild(elt("strong", null, "A prendre : " + libre.jeuNom + ". "));
        n.appendChild(elt("span", null,
            libre.joueurs === 0
                ? "Personne n'y a marque cette saison, la premiere place vaut " +
                  nombre(libre.gain) + " points."
                : nombre(libre.joueurs) + " joueurs y sont deja, y entrer vaut " +
                  nombre(libre.gain) + " points."));
    } else {
        n.appendChild(elt("strong", null, "A reprendre : " + pas.jeuNom + ". "));
        n.appendChild(elt("span", null,
            "Marquez " + nombre(pas.scoreVise) + " pour prendre la place " +
            pas.rangVise + ", qui vaut " + nombre(pas.gain) + " points de plus."));
    }
    return n;
}

function peindreSaison(data) {
    derniereSaison = data;
    peindreTete(data);
    podium(zonePodium, data.podium || []);
    peindreMoi(data);
    echelle(zoneEchelle, data.paliers, data.moi && data.moi.ligne ? data.moi.ligne.points : 0);

    vider(ecranSaison);
    ecranSaison.appendChild(tableau(COLONNES_SAISON, data.classement, {
        legende: "Classement de la saison",
        vide: "Aucune partie classee cette saison. La premiere place est libre.",
        marquer: (tr, l) => {
            if (moiPseudo && l.pseudo.toLowerCase() === moiPseudo.toLowerCase()) {
                tr.dataset.moi = "oui";
            }
        },
    }));

    vider(ecranCreneaux);
    ecranCreneaux.appendChild(tableau(COLONNES_CRENEAUX, data.creneaux, {
        legende: "Les bornes de la saison",
        vide: "Le serveur n'annonce aucune borne.",
    }));

    etat(etatSaison, data.saison && data.saison.encours
        ? "Saison en cours" : "Saison close",
        data.saison && data.saison.encours ? "direct" : null);
}

async function chargerSaison() {
    try {
        peindreSaison(await api("/api/v1/saison"));
    } catch (err) {
        console.error("saison", err);
        etat(etatSaison, "Classement de saison indisponible", "erreur");
    }
}

/* Le pseudo arrive apres la premiere peinture : on redessine pour surligner sa
 * ligne et afficher son bloc, plutot que de retarder toute la page. */
surCompte((compte) => {
    moiPseudo = compte && compte.username ? compte.username : "";
    if (derniereSaison) chargerSaison();
});

/* ==========================================================================
   Le classement general
   ========================================================================== */

const choixJeu = document.getElementById("choix-jeu");
const etatGeneral = document.getElementById("etat-general");
const ecranGeneral = document.getElementById("ecran-general");

/* Le regime d'une borne est ecrit en anglais dans la base, parce que c'est un
 * identifiant et pas une etiquette. La page, elle, est en francais. La
 * traduction est ici, au bord, et la base ne bouge pas : renommer une valeur
 * qui sert de cle pour faire joli sur un ecran est le genre d'echange qu'on
 * regrette a la migration suivante. */
function regime(d) {
    if (d === "hard") return "difficile";
    if (d === "easy") return "facile";
    return d;
}

const COLONNES_GENERAL = [
    { titre: "Rang",   classe: "c-rang",  valeur: (e) => String(e.rank) },
    { titre: "Joueur", classe: "c-nom",   valeur: (e) => e.username },
    { titre: "Score",  classe: "c-score", valeur: (e) => nombre(e.score) },
];

function peindreGeneral(entrees) {
    vider(ecranGeneral);
    ecranGeneral.appendChild(tableau(COLONNES_GENERAL, entrees, {
        legende: "Meilleurs scores",
        vide: "Aucun score enregistre pour l'instant. La premiere place est libre.",
    }));
}

async function chargerGeneral(idJeu) {
    etat(etatGeneral, "Chargement");
    try {
        const data = await api("/api/v1/leaderboard?game=" +
            encodeURIComponent(idJeu) + "&limit=20");
        peindreGeneral(data.entries);
        etat(etatGeneral, "A jour", "direct");
    } catch (err) {
        console.error("classement", err);
        peindreGeneral([]);
        etat(etatGeneral, "Indisponible", "erreur");
    }
}

async function chargerJeux() {
    if (!choixJeu) return;
    try {
        const data = await api("/api/v1/games");
        if (!data.games) return;
        for (const jeu of data.games) {
            const option = elt("option");
            option.value = String(jeu.id);
            option.textContent = jeu.difficulty === "normal"
                ? jeu.name
                : jeu.name + ", " + regime(jeu.difficulty);
            choixJeu.appendChild(option);
        }
    } catch (err) {
        console.error("liste des jeux", err);
        /* Le selecteur garde sa seule entree, « Classement general », qui est
           celle qui compte. */
    }
}

/* ==========================================================================
   Une manche, en direct
   ========================================================================== */

const formeCode = document.getElementById("forme-code");
const champCode = document.getElementById("champ-code");
const etatManche = document.getElementById("etat-manche");
const ecranManche = document.getElementById("ecran-manche");

let codeSuivi = "";
let minuteurManche = null;

/* Le code voyage en majuscules et sans rien d'autre que des lettres et des
 * chiffres. On le nettoie ICI plutot que de laisser partir une requete dont on
 * sait deja qu'elle sera refusee, et parce que ce code finit dans un chemin
 * d'URL. */
function nettoyerCode(brut) {
    return String(brut || "").toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, 10);
}

function motEtat(e) {
    if (e === "attente") return "en attente";
    if (e === "manche") return "manche en cours";
    if (e === "fini") return "terminee";
    return e;
}

function colonnesManche(camps) {
    const colonnes = [
        { titre: "Rang",   classe: "c-rang", valeur: (o, i) => String(i + 1) },
        { titre: "Joueur", classe: "c-nom",  valeur: (o) => o.pseudo || "place libre" },
    ];
    /* La colonne des camps n'apparait qu'en equipes. En individuel le camp vaut
       la place, donc l'afficher n'ajouterait qu'une colonne de nombres qui
       repete la premiere. */
    if (camps > 1) {
        colonnes.push({ titre: "Camp", valeur: (o) => "camp " + (o.camp + 1) });
    }
    colonnes.push(
        { titre: "Borne",    valeur: (o) => o.borne || "aucune" },
        { titre: "Fusibles", classe: "c-score", valeur: (o) => String(o.fusibles) },
        { titre: "Points",   classe: "c-score", valeur: (o) => nombre(o.points) },
        { titre: "Etat",     valeur: (o) => (o.vivante ? "en jeu" : "spectre") });
    return colonnes;
}

function peindreManche(vue) {
    /* Les vivants d'abord, puis les points. Un tableau trie par place ferait
       chercher le premier, et c'est la seule chose qu'on regarde. */
    const occupants = (vue.occupants || []).slice().sort((a, b) => {
        if (a.vivante !== b.vivante) return a.vivante ? -1 : 1;
        return b.points - a.points;
    });

    const colonnes = colonnesManche(vue.camps).map((c) => ({
        titre: c.titre,
        classe: c.classe,
        valeur: (o) => c.valeur(o, occupants.indexOf(o)),
    }));

    vider(ecranManche);
    ecranManche.appendChild(tableau(colonnes, occupants, {
        legende: "Classement de la manche " + vue.code,
        vide: "Personne dans ce salon pour l'instant.",
        marquer: (tr, o) => {
            if (!o.vivante) tr.dataset.sorti = "oui";
        },
    }));
}

async function battreManche() {
    if (!codeSuivi) return;
    try {
        const vue = await api("/api/v1/salons/" + encodeURIComponent(codeSuivi) + "/live");
        peindreManche(vue);
        etat(etatManche,
            vue.nom + ", " + motEtat(vue.etat) + " depuis " + duree(vue.depuisMs),
            vue.etat === "fini" ? null : "direct");

        /* Une manche finie ne bouge plus. On arrete de la redemander plutot
           que d'interroger indefiniment un tableau immobile. */
        if (vue.etat === "fini") arreterManche();
    } catch (err) {
        if (err.statut === 404) {
            etat(etatManche, "Aucun salon ne porte ce code", "erreur");
        } else if (err.statut === 503) {
            etat(etatManche, "Ce serveur n'ouvre pas de salons", "erreur");
        } else {
            console.error("manche", err);
            etat(etatManche, "Le direct ne repond pas", "erreur");
        }
        arreterManche();
    }
}

function arreterManche() {
    if (minuteurManche !== null) {
        window.clearInterval(minuteurManche);
        minuteurManche = null;
    }
}

function suivre(code) {
    codeSuivi = nettoyerCode(code);
    arreterManche();
    if (!codeSuivi) {
        etat(etatManche, "Entrez un code, ou choisissez une manche ci-dessous");
        vider(ecranManche);
        return;
    }
    if (champCode) champCode.value = codeSuivi;
    /* Le code passe dans le fragment d'URL, pas dans la requete : la page se
       partage et se recharge sur la meme manche, et un code de salon n'a rien
       a faire dans un journal de serveur. */
    history.replaceState(null, "", "#salon-" + codeSuivi);
    etat(etatManche, "Connexion");
    battreManche();
    minuteurManche = window.setInterval(battreManche, PERIODE_MANCHE_MS);
}

if (formeCode) {
    formeCode.addEventListener("submit", (e) => {
        e.preventDefault();
        suivre(champCode.value);
    });
}

/* ==========================================================================
   Les salons ouverts
   ========================================================================== */

const ecranOuverts = document.getElementById("ecran-ouverts");
let minuteurOuverts = null;

function boutonSuivre(salon) {
    const b = elt("button", "btn btn-small", "Suivre");
    b.type = "button";
    b.addEventListener("click", () => {
        suivre(salon.code);
        document.getElementById("direct").scrollIntoView({ block: "start" });
    });
    return b;
}

const COLONNES_OUVERTS = [
    { titre: "Salon",  classe: "c-nom", valeur: (s) => s.nom },
    { titre: "Code",   valeur: (s) => s.code },
    { titre: "Places", classe: "c-score", valeur: (s) => s.occupes + " / " + s.places },
    { titre: "Camps",  valeur: (s) => (s.camps > 1 ? s.camps + " equipes" : "chacun pour soi") },
    { titre: "Hote",   valeur: (s) => s.proprietaire },
    { titre: "Depuis", valeur: (s) => duree(s.depuisMs) },
    { titre: "",       noeud: boutonSuivre },
];

async function chargerOuverts() {
    if (!ecranOuverts) return;
    try {
        const data = await api("/api/v1/salons");
        vider(ecranOuverts);
        ecranOuverts.appendChild(tableau(COLONNES_OUVERTS, data.salons, {
            legende: "Salons publics ouverts",
            vide: "Aucun salon public ouvert. Creez-en un depuis le jeu, avec F1.",
        }));
    } catch (err) {
        vider(ecranOuverts);
        const message = err.statut === 503
            ? "Ce serveur n'ouvre pas de salons."
            : "La liste des salons ne repond pas.";
        if (err.statut !== 503) console.error("salons", err);
        ecranOuverts.appendChild(elt("p", "note", message));
    }
}

/* ==========================================================================
   Vie de la page
   ========================================================================== */

/*
 * ON N'INTERROGE PAS UN ONGLET QUE PERSONNE NE REGARDE.
 *
 * Sans cela, un onglet laisse ouvert une nuit envoie trente mille requetes
 * pour un tableau que personne ne lit. Le retour a l'onglet redemande tout de
 * suite, donc la reprise est immediate et le visiteur ne voit pas la coupure.
 */
function reglerRythme() {
    if (document.hidden) {
        arreterManche();
        if (minuteurOuverts !== null) {
            window.clearInterval(minuteurOuverts);
            minuteurOuverts = null;
        }
        return;
    }
    chargerOuverts();
    if (minuteurOuverts === null) {
        minuteurOuverts = window.setInterval(chargerOuverts, PERIODE_OUVERTS_MS);
    }
    if (codeSuivi && minuteurManche === null) suivre(codeSuivi);
}

document.addEventListener("visibilitychange", reglerRythme);

if (choixJeu) {
    choixJeu.addEventListener("change", () => chargerGeneral(choixJeu.value));
}

chargerSaison();
chargerJeux().then(() => chargerGeneral(0));
reglerRythme();

/*
 * UN LIEN PARTAGE ARRIVE AVEC LE CODE DANS LE FRAGMENT.
 *
 * Et il faut ecouter `hashchange`, pas seulement lire le fragment au
 * chargement : changer le fragment d'une page deja ouverte est une navigation
 * DANS LE MEME DOCUMENT, donc le script ne se rejoue pas. Sans cet ecouteur,
 * cliquer sur le lien qu'un ami vient d'envoyer, depuis cette page-ci, ne
 * faisait rien du tout. Constate en essayant exactement cela.
 */
function suivreLeFragment() {
    const fragment = window.location.hash.replace(/^#salon-/, "");
    if (fragment && fragment !== window.location.hash) suivre(fragment);
}

window.addEventListener("hashchange", suivreLeFragment);
suivreLeFragment();

})();
