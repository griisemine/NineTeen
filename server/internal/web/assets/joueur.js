/*
 * joueur.js, la fiche detaillee d'un joueur.
 *
 * TROIS BLOCS, ET CHACUN REPOND A UNE QUESTION QUE L'ON SE POSE VRAIMENT EN
 * REGARDANT UN CLASSEMENT :
 *
 *   Qui est-ce      depuis quand il joue, combien de parties, combien de
 *                   premieres places.
 *   Ou en est-il    sa saison en cours, son palier, son ecart, et la saison
 *                   d'avant a cote pour voir s'il monte ou s'il descend.
 *   Comment fait-il borne par borne, son record et la place que ce record
 *                   tient aujourd'hui.
 *
 * Le pseudo vient de l'adresse. Il n'est JAMAIS insere en HTML : comme partout
 * sur ce site, il passe par textContent, et l'adresse par encodeURIComponent.
 */

"use strict";

(function () {

const { api, elt, vider, etat, tableau, nombre, quand, medaille, jauge } = window.NS;

const zoneFiche = document.getElementById("fiche");
const zoneEtat = document.getElementById("etat-joueur");
const blocSaison = document.getElementById("bloc-saison");
const blocRecords = document.getElementById("bloc-records");

function regime(d) {
    if (d === "hard") return "difficile";
    if (d === "easy") return "facile";
    return d;
}

function stat(valeur, quoi) {
    const li = elt("li");
    li.appendChild(elt("strong", "valeur", valeur));
    li.appendChild(elt("span", "quoi", quoi));
    return li;
}

/* Le rang d'un record parmi tous ceux de la borne. « 1er sur 12 » dit deux
 * choses d'un coup : la place, et si elle vaut quelque chose. */
function place(m) {
    const r = Number(m.rang || 0);
    return (r === 1 ? "1er" : r + "e") + " sur " + nombre(m.joueurs);
}

const COLONNES_RECORDS = [
    { titre: "Borne",  classe: "c-nom",   valeur: (m) => m.jeuNom },
    { titre: "Regime", classe: "c-nom",   valeur: (m) => regime(m.difficulte) },
    { titre: "Record", classe: "c-score", valeur: (m) => nombre(m.score) },
    { titre: "Place",  classe: "c-nom",   valeur: (m) => place(m) },
    { titre: "Depuis", classe: "c-nom",   valeur: (m) => quand(m.quand) },
];

const COLONNES_CRENEAUX_SAISON = [
    { titre: "Borne",  classe: "c-nom",   valeur: (c) => c.jeuNom },
    { titre: "Regime", classe: "c-nom",   valeur: (c) => regime(c.difficulte) },
    { titre: "Score",  classe: "c-score", valeur: (c) => nombre(c.score) },
    { titre: "Place",  classe: "c-nom",
      valeur: (c) => (c.rang === 1 ? "1er" : c.rang + "e") + " sur " + nombre(c.joueurs) },
    { titre: "Points", classe: "c-score", valeur: (c) => nombre(c.points) },
    { titre: "Parties", classe: "c-score", valeur: (c) => nombre(c.parties) },
];

function peindreFiche(data) {
    const f = data.fiche;
    vider(zoneFiche);

    const carte = elt("div", "fiche");
    carte.appendChild(elt("h2", null, f.pseudo));
    carte.appendChild(elt("p", "depuis",
        "Inscrit le " + quand(f.inscrit) + ". Vu le " + quand(f.vuLe) + "."));

    const ors = (f.meilleurs || []).filter((m) => m.rang === 1).length;
    const podiums = (f.meilleurs || []).filter((m) => m.rang <= 3).length;

    const stats = elt("ul", "stats");
    stats.appendChild(stat(nombre(f.parties), "parties validees"));
    stats.appendChild(stat(nombre((f.meilleurs || []).length), "bornes tenues"));
    stats.appendChild(stat(nombre(ors), "records de borne"));
    stats.appendChild(stat(nombre(podiums), "podiums de toujours"));
    carte.appendChild(stats);

    zoneFiche.appendChild(carte);
}

function peindreUneSaison(bloc, titre, saison, paliers, avecDetail) {
    if (!saison) return;

    const tete = elt("div", "section-head");
    tete.appendChild(elt("h2", null, titre + " " + saison.libelle));
    bloc.appendChild(tete);

    const ligne = saison.ligne;
    if (!ligne) {
        bloc.appendChild(elt("p", "note",
            "Aucune partie classee sur cette saison."));
        return;
    }

    const carte = elt("div", "fiche");
    const stats = elt("ul", "stats");
    stats.appendChild(stat(nombre(ligne.points), "points"));
    stats.appendChild(stat(String(ligne.rang) + " / " + nombre(saison.joueurs), "rang"));
    stats.appendChild(stat(nombre(ligne.ors), "premieres places"));
    stats.appendChild(stat(nombre(ligne.parties), "parties"));
    carte.appendChild(stats);

    const p = elt("p", "depuis");
    p.appendChild(medaille(ligne.palier));
    carte.appendChild(p);

    if (paliers) {
        const j = jauge(paliers, ligne.points);
        if (j) {
            carte.appendChild(j.barre);
            carte.appendChild(elt("p", "note",
                nombre(j.manque) + " points avant " + j.suivant.nom));
        }
    }
    bloc.appendChild(carte);

    if (avecDetail && ligne.creneaux && ligne.creneaux.length) {
        const crt = elt("div", "crt");
        const ecran = elt("div", "crt-ecran");
        ecran.appendChild(tableau(COLONNES_CRENEAUX_SAISON, ligne.creneaux, {
            legende: "Detail de la saison",
            vide: "Aucune borne jouee.",
        }));
        crt.appendChild(ecran);
        const lignes = elt("div", "crt-scanlines");
        lignes.setAttribute("aria-hidden", "true");
        crt.appendChild(lignes);
        bloc.appendChild(crt);
    }
}

function peindreRecords(f) {
    vider(blocRecords);
    const tete = elt("div", "section-head");
    tete.appendChild(elt("h2", null, "Ses records, borne par borne"));
    tete.appendChild(elt("p", null,
        "Le meilleur score de toujours sur chaque borne, et la place que ce record tient " +
        "aujourd'hui. Ces scores ne remettent rien a zero, contrairement a ceux d'une saison."));
    blocRecords.appendChild(tete);

    const crt = elt("div", "crt");
    const ecran = elt("div", "crt-ecran");
    ecran.appendChild(tableau(COLONNES_RECORDS, f.meilleurs, {
        legende: "Records par borne",
        vide: "Ce joueur n'a encore aucun score valide.",
    }));
    crt.appendChild(ecran);
    const lignes = elt("div", "crt-scanlines");
    lignes.setAttribute("aria-hidden", "true");
    crt.appendChild(lignes);
    blocRecords.appendChild(crt);
}

async function charger() {
    const pseudo = new URLSearchParams(window.location.search).get("pseudo");
    if (!pseudo) {
        etat(zoneEtat, "Aucun joueur demande. Choisissez un nom dans le classement.", "erreur");
        return;
    }

    document.title = pseudo + " sur Nineteen";
    try {
        const data = await api("/api/v1/joueurs/" + encodeURIComponent(pseudo));
        peindreFiche(data);

        vider(blocSaison);
        /* La saison en cours porte son detail, la precedente non : on la met la
           pour COMPARER, pas pour la relire ligne a ligne. */
        peindreUneSaison(blocSaison, "Saison", data.saison,
            data.paliers || null, true);
        peindreUneSaison(blocSaison, "Saison precedente", data.precedente,
            null, false);

        peindreRecords(data.fiche);
        etat(zoneEtat, "", null);
        zoneEtat.hidden = true;
    } catch (err) {
        console.error("joueur", err);
        etat(zoneEtat, err.statut === 404
            ? "Ce joueur n'existe pas, ou son compte est ferme."
            : "Fiche indisponible pour le moment.", "erreur");
    }
}

charger();

})();
