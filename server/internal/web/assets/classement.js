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

const { api, elt, vider, etat, tableau, nombre, duree } = window.NS;

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
