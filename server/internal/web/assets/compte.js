/*
 * compte.js, l'inscription et la connexion.
 *
 * Le formulaire disparait une fois connecte et laisse la place au profil.
 * Deux etats, pas trois : un formulaire de connexion visible sous le nom de
 * quelqu'un qui est deja connecte est la premiere chose qui fait douter.
 *
 * Les mots de passe ne passent que par fetch, en JSON, sur la meme origine.
 * Aucune valeur de champ n'est journalisee, ni en console ni ailleurs.
 */

"use strict";

(function () {

const { api, elt, vider, nombre, surCompte, diffuserCompte } = window.NS;

const zone = document.getElementById("zone-compte");
const forme = document.getElementById("forme-compte");
if (!zone || !forme) return;

const ongletConnexion = document.getElementById("onglet-connexion");
const ongletInscription = document.getElementById("onglet-inscription");
const tabConnexion = document.getElementById("tab-connexion");
const tabInscription = document.getElementById("tab-inscription");

/* --------------------------------------------------------------- onglets */

function choisirOnglet(inscription) {
    tabConnexion.setAttribute("aria-selected", String(!inscription));
    tabInscription.setAttribute("aria-selected", String(inscription));
    ongletConnexion.hidden = inscription;
    ongletInscription.hidden = !inscription;
}

tabConnexion.addEventListener("click", () => choisirOnglet(false));
tabInscription.addEventListener("click", () => choisirOnglet(true));

/* -------------------------------------------------------------- messages */

function message(panneau, texte, ton) {
    const p = panneau.querySelector(".message");
    if (!p) return;
    p.textContent = texte || "";
    p.hidden = !texte;
    p.dataset.ton = ton || "erreur";
}

/* ------------------------------------------------------------ soumission */

function brancher(panneau, chemin, corps) {
    panneau.addEventListener("submit", async (e) => {
        e.preventDefault();
        message(panneau, "");

        const bouton = panneau.querySelector("button[type=submit]");
        bouton.disabled = true;

        try {
            const donnees = new FormData(panneau);
            await api(chemin, { method: "POST", body: corps(donnees) });
            /* Le formulaire est vide AVANT de recharger le compte : les champs
               de mot de passe ne restent pas remplis dans le document une fois
               la page passee au profil. */
            panneau.reset();
            const moi = await api("/api/v1/me");
            diffuserCompte(moi);
        } catch (err) {
            /* Le message du serveur est affiche tel quel parce qu'il est ecrit
               pour cela : « identifiant deja pris », « identifiants
               incorrects ». Tout le reste est generique, et le detail reste en
               console. */
            message(panneau, err.message || "Le service ne repond pas correctement.");
            console.error("compte", err.statut || 0);
        } finally {
            bouton.disabled = false;
        }
    });
}

brancher(ongletConnexion, "/api/v1/auth/login", (d) => ({
    username: String(d.get("username") || ""),
    password: String(d.get("password") || ""),
}));

brancher(ongletInscription, "/api/v1/auth/register", (d) => ({
    username: String(d.get("username") || ""),
    email: String(d.get("email") || ""),
    password: String(d.get("password") || ""),
}));

/* ---------------------------------------------------------------- profil */

function profil(compte) {
    const bloc = elt("div", "forme");
    bloc.appendChild(elt("h2", null, "Connecte comme " + compte.username));

    /* `scores` arrive en objet, borne par borne : { "snake": 4200, ... }. Le
       trier par score decroissant met devant celui dont on est fier. */
    const scores = Object.entries(compte.scores || {})
        .sort((a, b) => b[1] - a[1]);
    if (scores.length === 0) {
        bloc.appendChild(elt("p", null,
            "Aucun score enregistre pour l'instant. Lancez le jeu avec l'adresse " +
            "de ce serveur, et la premiere partie finie apparaitra ici."));
    } else {
        bloc.appendChild(elt("p", null, "Vos meilleurs scores, borne par borne."));
        const ul = elt("ul", "annexes");
        for (const [slug, score] of scores) {
            ul.appendChild(elt("li", null, slug + " " + nombre(score)));
        }
        bloc.appendChild(ul);
    }

    const p = elt("p");
    const versClassement = elt("a", "btn", "Voir le classement");
    versClassement.href = "/classement.html";
    const versTelechargement = elt("a", "btn btn-primary", "Telecharger le jeu");
    versTelechargement.href = "/telecharger.html";
    p.append(versTelechargement, elt("span", null, " "), versClassement);
    bloc.appendChild(p);

    return bloc;
}

surCompte((compte) => {
    vider(zone);
    zone.appendChild(compte ? profil(compte) : forme);
    if (!compte) choisirOnglet(false);
});

})();
