/*
 * salon.js — le classement d'une manche, pendant qu'elle se joue.
 *
 * DEUX SOURCES, JAMAIS MELANGEES. Le classement general de la page d'accueil
 * ne contient que des parties RECALCULEES par le serveur a partir de leur
 * journal ; un score invente n'y entre pas. Ici, ce sont les joueurs qui
 * publient leur ligne pendant la manche, et le serveur la relaie. C'est
 * suffisant pour regarder une manche, et ce serait insuffisant pour un
 * palmares — d'ou la phrase en tete de page, qui n'est pas de la decoration.
 *
 * AUCUN innerHTML NULLE PART. Le pseudo et le nom du salon sont ecrits par des
 * joueurs. La regle du site est textContent partout, et l'audit du site
 * d'origine explique pourquoi : c'est exactement la ou il tombait.
 */

"use strict";

/*
 * Deux secondes, et pas les 250 ms du jeu.
 *
 * Le client bat a 4 Hz parce qu'il en a besoin pour jouer. Un spectateur n'en
 * a pas besoin : la lame du Couperet tombe toutes les 45 s, et rafraichir vingt
 * fois entre deux lames ne montre rien de plus. Le seul cout d'un intervalle
 * court serait de multiplier par huit les requetes d'une page qu'on laisse
 * ouverte.
 */
const PERIODE_MS = 2000;
const PERIODE_OUVERTS_MS = 10000;

const champCode = document.getElementById("champ-code");
const formeCode = document.getElementById("forme-code");
const statut = document.getElementById("statut-salon");
const titre = document.getElementById("salon-titre");
const tableau = document.getElementById("tableau-manche");
const corps = tableau.querySelector("tbody");

const statutOuverts = document.getElementById("statut-ouverts");
const corpsOuverts = document.getElementById("tableau-ouverts").querySelector("tbody");

let code = "";
let minuteur = null;
let minuteurOuverts = null;

/* Le code voyage en majuscules et sans rien d'autre que des lettres et des
 * chiffres. On le nettoie ICI plutot que de laisser partir une requete dont on
 * sait deja qu'elle sera refusee — et parce que ce code finit dans un chemin
 * d'URL. */
function nettoyer(brut) {
    return String(brut || "").toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, 10);
}

function cellule(ligne, texte, classe) {
    const td = document.createElement("td");
    td.textContent = texte;
    if (classe) td.className = classe;
    ligne.appendChild(td);
    return td;
}

function vider(n) {
    while (n.firstChild) n.removeChild(n.firstChild);
}

/* « il y a 3 min » plutot qu'un horodatage : ce qu'on veut savoir d'une manche
 * en cours, c'est depuis combien de temps elle dure. */
function depuis(ms) {
    const s = Math.max(0, Math.round(Number(ms || 0) / 1000));
    if (s < 60) return s + " s";
    const m = Math.floor(s / 60);
    if (m < 60) return m + " min " + String(s % 60).padStart(2, "0") + " s";
    return Math.floor(m / 60) + " h " + String(m % 60).padStart(2, "0");
}

const ETATS = {
    attente: "en attente de joueurs",
    manche: "manche en cours",
    fini: "manche terminée",
};

function dessiner(salon) {
    const nom = String(salon.nom || "").trim();
    titre.textContent = nom ? nom + " — " + salon.code : "Salon " + salon.code;
    titre.hidden = false;

    const etat = ETATS[salon.etat] || String(salon.etat || "");
    statut.textContent = etat + " · depuis " + depuis(salon.depuisMs);

    const occupants = Array.isArray(salon.occupants) ? salon.occupants.slice() : [];

    /*
     * Le classement d'une manche du Couperet n'est pas l'ordre des points seul.
     * Un joueur SORTI garde ses points mais n'est plus en course : le montrer
     * au-dessus d'un vivant qui le rattrape mentirait sur l'etat de la manche.
     * Les vivants d'abord, puis les points.
     */
    occupants.sort((a, b) => {
        if (!!a.vivante !== !!b.vivante) return a.vivante ? -1 : 1;
        return Number(b.points || 0) - Number(a.points || 0);
    });

    vider(corps);
    occupants.forEach((o, i) => {
        const tr = document.createElement("tr");
        if (!o.vivante) tr.className = "sorti";
        cellule(tr, o.vivante ? String(i + 1) : "—", "c-rank");
        cellule(tr, String(o.pseudo || "—"));
        /* Un seul camp veut dire « chacun pour soi » : afficher « camp 0 »
         * inventerait une equipe qui n'existe pas. */
        cellule(tr, Number(salon.camps || 1) > 1 ? "ABCD"[Number(o.camp) || 0] || "?" : "—");
        cellule(tr, String(o.borne || "—"));
        cellule(tr, String(Number(o.fusibles) || 0), "c-score");
        cellule(tr, String(Number(o.points) || 0), "c-score");
        corps.appendChild(tr);
    });

    if (occupants.length === 0) {
        const tr = document.createElement("tr");
        const td = cellule(tr, "Personne pour l'instant.", "empty");
        td.colSpan = 6;
        corps.appendChild(tr);
    }
    tableau.hidden = false;
}

async function relever() {
    if (!code) return;
    try {
        const r = await fetch("/api/v1/salons/" + encodeURIComponent(code) + "/live", {
            headers: { Accept: "application/json" },
        });
        if (r.status === 404) {
            statut.textContent = "Aucun salon avec ce code. Il a peut-être été fermé.";
            titre.hidden = true;
            tableau.hidden = true;
            arreter();
            return;
        }
        if (!r.ok) throw new Error("HTTP " + r.status);
        const salon = await r.json();
        dessiner(salon);

        /* Une manche finie ne bouge plus. Continuer a la relever serait
         * demander vingt fois par minute une reponse qu'on connait deja. */
        if (salon.etat === "fini") arreter();
    } catch (err) {
        statut.textContent = "Serveur injoignable. Nouvel essai dans quelques secondes.";
        console.error("salon en direct", err);
    }
}

function arreter() {
    if (minuteur !== null) {
        clearInterval(minuteur);
        minuteur = null;
    }
}

function suivre(nouveau) {
    code = nettoyer(nouveau);
    arreter();
    if (!code) {
        statut.textContent = "Entrez un code, ou choisissez un salon ouvert ci-dessous.";
        titre.hidden = true;
        tableau.hidden = true;
        return;
    }
    champCode.value = code;

    /* L'adresse porte le code : c'est ce lien qu'on envoie a quelqu'un. Sans
     * ca, partager la page ne partagerait rien. `replaceState` et non `push`,
     * pour ne pas remplir l'historique a chaque frappe. */
    const url = new URL(window.location.href);
    url.searchParams.set("c", code);
    window.history.replaceState(null, "", url);

    statut.textContent = "Chargement…";
    relever();
    minuteur = setInterval(relever, PERIODE_MS);
}

formeCode?.addEventListener("submit", (e) => {
    e.preventDefault();
    suivre(champCode.value);
});

/* --- les salons ouverts -------------------------------------------------- */

async function releverOuverts() {
    try {
        const r = await fetch("/api/v1/salons", { headers: { Accept: "application/json" } });
        if (!r.ok) throw new Error("HTTP " + r.status);
        const data = await r.json();
        const salons = Array.isArray(data.salons) ? data.salons : [];

        vider(corpsOuverts);
        if (salons.length === 0) {
            const tr = document.createElement("tr");
            const td = cellule(tr, "Aucun salon public ouvert pour l'instant.", "empty");
            td.colSpan = 4;
            corpsOuverts.appendChild(tr);
            statutOuverts.textContent = "Aucun salon ouvert.";
            return;
        }

        salons.forEach((s) => {
            const tr = document.createElement("tr");
            cellule(tr, String(s.nom || "sans nom"));
            cellule(tr, String(s.proprietaire || "—"));
            cellule(tr, (Number(s.occupes) || 0) + " / " + (Number(s.places) || 0), "c-score");

            /* Le code est un BOUTON : on le suit d'un clic. Le recopier a la
             * main quand il est deja a l'ecran est une occasion de se tromper
             * pour rien. */
            const td = document.createElement("td");
            const b = document.createElement("button");
            b.type = "button";
            b.className = "btn btn-ghost";
            b.textContent = String(s.code || "");
            b.addEventListener("click", () => suivre(s.code));
            td.appendChild(b);
            tr.appendChild(td);
            corpsOuverts.appendChild(tr);
        });
        statutOuverts.textContent = salons.length + (salons.length > 1 ? " salons ouverts" : " salon ouvert");
    } catch (err) {
        statutOuverts.textContent = "Serveur injoignable.";
        console.error("salons ouverts", err);
    }
}

/*
 * Un onglet cache ne raffraichit rien. C'est la seule facon honnete de ne pas
 * interroger le serveur toutes les deux secondes pendant des heures pour une
 * page que personne ne regarde.
 */
document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
        arreter();
        if (minuteurOuverts !== null) { clearInterval(minuteurOuverts); minuteurOuverts = null; }
    } else {
        if (code && minuteur === null) { relever(); minuteur = setInterval(relever, PERIODE_MS); }
        if (minuteurOuverts === null) {
            releverOuverts();
            minuteurOuverts = setInterval(releverOuverts, PERIODE_OUVERTS_MS);
        }
    }
});

releverOuverts();
minuteurOuverts = setInterval(releverOuverts, PERIODE_OUVERTS_MS);

const initial = new URL(window.location.href).searchParams.get("c");
if (initial) suivre(initial);
