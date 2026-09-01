/*
 * nineteen.js, le socle commun aux cinq pages.
 *
 * Trois regles, et elles viennent de l'audit du site d'origine :
 *
 *  1. RIEN N'EST INSERE EN HTML. Tout passe par textContent, donc un pseudo
 *     contenant du balisage s'affiche comme du texte. L'original concatenait
 *     les valeurs de la base dans ses pages, ce qui donnait un XSS stocke : il
 *     suffisait de choisir le bon pseudo a l'inscription.
 *
 *  2. TOUTE REQUETE MODIFIANTE PORTE LE JETON CSRF. Il est lu dans un cookie
 *     accessible au script et recopie dans un en-tete. Un site tiers peut faire
 *     envoyer le cookie par le navigateur, il ne peut pas en lire la valeur.
 *
 *  3. AUCUNE ERREUR DU SERVEUR N'EST AFFICHEE TELLE QUELLE si elle n'est pas
 *     prevue. On montre un message general et on garde le detail en console.
 *
 * Il expose son outillage sous window.NS. Pas de module ES : les cinq pages
 * chargent deux fichiers avec `defer`, dans l'ordre, et cela suffit. Un
 * `type="module"` ferait payer un aller-retour de plus pour un site qui tient
 * en quatre scripts.
 */

"use strict";

(function () {

/* ------------------------------------------------------------------ reseau */

function lireCookie(nom) {
    const prefixe = nom + "=";
    for (const part of document.cookie.split(";")) {
        const net = part.trim();
        if (net.startsWith(prefixe)) {
            return decodeURIComponent(net.slice(prefixe.length));
        }
    }
    return "";
}

async function api(chemin, options) {
    const o = options || {};
    const methode = o.method || "GET";
    const entetes = {};

    if (o.body !== undefined && o.body !== null) {
        entetes["Content-Type"] = "application/json";
    }
    if (methode !== "GET" && methode !== "HEAD") {
        const csrf = lireCookie("ns_csrf");
        if (csrf) entetes["X-Nineteen-CSRF"] = csrf;
    }

    const reponse = await fetch(chemin, {
        method: methode,
        headers: entetes,
        body: o.body === undefined || o.body === null ? null : JSON.stringify(o.body),
        credentials: "same-origin",
    });

    let charge = null;
    try {
        charge = await reponse.json();
    } catch (e) {
        /* Reponse non JSON : panne d'infrastructure, pas de l'application. */
    }

    if (!reponse.ok) {
        const message = charge && typeof charge.error === "string"
            ? charge.error
            : "Le service ne repond pas correctement.";
        const err = new Error(message);
        err.statut = reponse.status;
        throw err;
    }
    return charge;
}

/* ------------------------------------------------------------------- formes */

const formatNombre = new Intl.NumberFormat("fr-FR").format;

/* Les tailles sont en mebioctets parce que ce sont des paquets : un joueur qui
 * regarde 175 Mio sait ce qu'il va attendre, et « 175 580 032 octets » ne dit
 * rien a personne. Une decimale suffit a distinguer deux paquets voisins. */
function octets(n) {
    const v = Number(n || 0);
    if (v <= 0) return "";
    if (v < 1024) return v + " octets";
    if (v < 1024 * 1024) return Math.round(v / 1024) + " Kio";
    return (v / (1024 * 1024)).toFixed(1).replace(".", ",") + " Mio";
}

/* « il y a 3 min » plutot qu'un horodatage : ce qu'on veut savoir d'une manche
 * en cours, c'est depuis combien de temps elle dure. */
function duree(ms) {
    const s = Math.max(0, Math.round(Number(ms || 0) / 1000));
    if (s < 60) return s + " s";
    const m = Math.floor(s / 60);
    if (m < 60) return m + " min " + String(s % 60).padStart(2, "0") + " s";
    return Math.floor(m / 60) + " h " + String(m % 60).padStart(2, "0");
}

/* --------------------------------------------------------------------- DOM */

/* Un seul constructeur d'element pour tout le site. `texte` passe par
 * textContent, ce qui est la regle 1 ci-dessus rendue difficile a contourner :
 * il n'existe nulle part de fonction qui accepterait du balisage. */
function elt(nom, classe, texte) {
    const n = document.createElement(nom);
    if (classe) n.className = classe;
    if (texte !== undefined && texte !== null) n.textContent = String(texte);
    return n;
}

function vider(n) {
    if (n) n.replaceChildren();
}

/* L'etat d'un panneau, en un mot et une couleur. Le mot passe par textContent,
 * la couleur par un attribut de donnee que le CSS lit. */
function etat(noeud, texte, ton) {
    if (!noeud) return;
    noeud.textContent = texte;
    if (ton) {
        noeud.dataset.etat = ton;
    } else {
        delete noeud.dataset.etat;
    }
}

/*
 * UN SEUL CONSTRUCTEUR DE TABLEAU POUR LES TROIS TABLEAUX DU SITE.
 *
 * C'est le point de la refonte du classement. Avant, l'entete d'un tableau
 * etait en HTML et ses lignes en JavaScript, sur deux pages differentes qui
 * n'avaient pas les memes colonnes. Ajouter une colonne demandait de trouver
 * les deux endroits, et rien ne signalait qu'on en avait oublie un.
 *
 * `colonnes` decrit tout : le titre, la classe, et comment lire la valeur dans
 * une ligne. L'entete et le corps en decoulent, donc ils ne peuvent pas se
 * contredire.
 */
function tableau(colonnes, lignes, options) {
    const o = options || {};
    const table = elt("table", "board");

    if (o.legende) {
        table.appendChild(elt("caption", "visually-hidden", o.legende));
    }

    const thead = elt("thead");
    const trh = elt("tr");
    for (const c of colonnes) {
        const th = elt("th", c.classe, c.titre);
        th.scope = "col";
        trh.appendChild(th);
    }
    thead.appendChild(trh);
    table.appendChild(thead);

    const tbody = elt("tbody");

    if (!lignes || lignes.length === 0) {
        const tr = elt("tr");
        const td = elt("td", "vide", o.vide || "Rien a afficher pour l'instant.");
        td.colSpan = colonnes.length;
        tr.appendChild(td);
        tbody.appendChild(tr);
    } else {
        for (const ligne of lignes) {
            const tr = elt("tr");
            if (o.marquer) o.marquer(tr, ligne);
            for (const c of colonnes) {
                /* Une colonne rend du texte, ou un noeud quand la cellule doit
                   porter un bouton ou un lien. Les deux passent par le meme
                   descripteur de colonnes, donc l'entete reste juste dans les
                   deux cas. */
                if (c.noeud) {
                    const td = elt("td", c.classe);
                    td.appendChild(c.noeud(ligne));
                    tr.appendChild(td);
                } else {
                    tr.appendChild(elt("td", c.classe, c.valeur(ligne)));
                }
            }
            tbody.appendChild(tr);
        }
    }

    table.appendChild(tbody);
    return table;
}

/* ------------------------------------------------------------------ version */

async function chargerVersion() {
    const cibles = document.querySelectorAll("[data-version]");
    if (cibles.length === 0) return null;
    try {
        const data = await api("/api/v1/version");
        for (const el of cibles) el.textContent = data.version;
        return data;
    } catch (err) {
        console.error("version", err);
        return null;
    }
}

/* ------------------------------------------------------------------- compte */

/*
 * LA BARRE DE COMPTE EST LA MEME SUR LES CINQ PAGES.
 *
 * Elle est bâtie par le script et non ecrite dans chaque page pour une raison
 * simple : cinq copies d'un meme fragment finissent par diverger, et celle qui
 * diverge est toujours celle qu'on regarde le moins.
 *
 * Deux etats seulement. Connecte : le pseudo et un bouton de deconnexion.
 * Anonyme : un lien vers la page de compte. Pas d'etat intermediaire visible,
 * la barre reste vide tant que la reponse n'est pas la, ce qui evite de montrer
 * « Se connecter » a quelqu'un qui l'est deja.
 */
const abonnesCompte = [];
let compteRecu = false;

function surCompte(f) {
    abonnesCompte.push(f);
    /* Un abonne arrive apres la reponse recoit quand meme l'etat courant. Sans
       cela, l'ordre de chargement des scripts deciderait de ce qui s'affiche,
       ce qui est la definition d'un defaut intermittent. */
    if (compteRecu) f(NS.compte);
}

function diffuserCompte(compte) {
    NS.compte = compte;
    compteRecu = true;
    for (const f of abonnesCompte) {
        try {
            f(compte);
        } catch (err) {
            console.error("abonne compte", err);
        }
    }
}

function peindreBarre(compte) {
    const barre = document.getElementById("compte-barre");
    if (!barre) return;
    vider(barre);

    if (!compte) {
        const lien = elt("a", "btn btn-small", "Se connecter");
        lien.href = "/compte.html";
        barre.appendChild(lien);
        return;
    }

    const nom = elt("span", "pseudo", compte.username);
    const sortir = elt("button", "btn btn-small", "Se deconnecter");
    sortir.type = "button";
    sortir.addEventListener("click", async () => {
        sortir.disabled = true;
        try {
            await api("/api/v1/auth/logout", { method: "POST" });
        } catch (err) {
            console.error("deconnexion", err);
        }
        diffuserCompte(null);
    });
    barre.append(nom, sortir);
}

async function chargerCompte() {
    try {
        const data = await api("/api/v1/me");
        diffuserCompte(data && data.username ? data : null);
    } catch (err) {
        /* 401 est la reponse normale d'un visiteur non connecte. */
        if (err.statut !== 401) console.error("compte", err);
        diffuserCompte(null);
    }
}

/* ------------------------------------------------------------------ exports */

const NS = {
    api: api,
    lireCookie: lireCookie,
    nombre: formatNombre,
    octets: octets,
    duree: duree,
    elt: elt,
    vider: vider,
    etat: etat,
    tableau: tableau,
    surCompte: surCompte,
    diffuserCompte: diffuserCompte,
    chargerCompte: chargerCompte,
    compte: null,
    version: null,
};
window.NS = NS;

surCompte(peindreBarre);
chargerCompte();
chargerVersion().then((v) => { NS.version = v; });

})();
