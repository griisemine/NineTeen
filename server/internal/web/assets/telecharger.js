/*
 * telecharger.js, la page qui donne le jeu.
 *
 * ELLE NE DECIDE RIEN. Elle demande a /api/v1/telechargements ce que le serveur
 * a sous la main, et elle l'affiche. C'est le contraire de ce qu'elle faisait :
 * elle batissait trois liens vers une release GitHub a partir du seul numero de
 * version, sans jamais verifier qu'elle existe. Verifie contre l'API GitHub a
 * l'epoque : le depot repondait 200, la release de la version repondait 404, et
 * la liste des releases etait vide. Les trois boutons de la page qui existe
 * pour telecharger etaient trois 404.
 *
 * Un bouton mort est pire qu'un bouton absent : il fait douter du reste de la
 * page. Ce qui n'existe pas ne s'affiche donc pas.
 */

"use strict";

(function () {

const { api, elt, vider, etat, octets } = window.NS;

const etatBloc = document.getElementById("etat-telechargements");
const liste = document.getElementById("liste-telechargements");
const apres = document.getElementById("apres-telechargements");
const blocServeur = document.getElementById("bloc-serveur");

/* Les trois plateformes sont dans cet ordre, et il ne bouge pas d'un
 * chargement a l'autre. Une page dont les boutons changent de place est une
 * page qu'on ne peut pas apprendre. */
const PLATEFORMES = [
    { cle: "windows", nom: "Windows", detail: "Windows 10 ou plus recent, 64 bits" },
    { cle: "macos",   nom: "macOS",   detail: "macOS 11 ou plus recent, Apple Silicon ou Intel" },
    { cle: "linux",   nom: "Linux",   detail: "glibc 2.34 ou plus recente, Vulkan" },
];

const NOM_ARCH = {
    "x86_64": "Intel et AMD 64 bits",
    "arm64": "ARM 64 bits",
    "universel": "universel",
};

function carte(plateforme, fichiers) {
    const li = elt("li", "plateforme");
    li.appendChild(elt("h3", null, plateforme.nom));
    li.appendChild(elt("p", "detail", plateforme.detail));

    if (fichiers.length === 0) {
        li.appendChild(elt("p", "absent",
            "Aucun paquet pour cette plateforme sur ce serveur."));
        return li;
    }

    for (const f of fichiers) {
        const bloc = elt("div");

        const taille = octets(f.octets);
        const arch = NOM_ARCH[f.arch] || f.arch;
        const detail = [f.format, arch, taille].filter((x) => x).join(", ");
        bloc.appendChild(elt("p", "detail", detail));

        const lien = elt("a", "btn btn-primary", "Telecharger");
        lien.href = f.url;
        /* `download` demande au navigateur d'enregistrer au lieu d'essayer
           d'afficher. Il ne vaut que pour la meme origine, ce qui est le cas
           des paquets servis ici. Un lien vers GitHub l'ignore, et c'est sans
           consequence : GitHub pose son propre Content-Disposition. */
        lien.setAttribute("download", "");
        lien.rel = "nofollow";
        bloc.appendChild(lien);

        bloc.appendChild(elt("p", "somme", f.nom));
        if (f.sha256) {
            bloc.appendChild(elt("p", "somme", "SHA-256 " + f.sha256));
        }
        li.appendChild(bloc);
    }
    return li;
}

/* La ligne exacte a taper pour brancher le jeu sur CE serveur.
 *
 * L'adresse vient du serveur et non de window.location : les deux coincident en
 * developpement et divergent des qu'un proxy TLS est devant. Le site est alors
 * joint en https, que le client du jeu refuse explicitement d'ouvrir, donc
 * batir la ligne a partir de l'origine de la page donnerait au joueur une URL
 * que son jeu ne sait pas lire, sur le deploiement meme ou l'aide compte le
 * plus. Le serveur est le seul a connaitre son adresse joignable. */
function peindreServeur(adresse) {
    if (!blocServeur) return;
    vider(blocServeur);

    if (!adresse) {
        blocServeur.appendChild(elt("p", null,
            "Ce serveur n'annonce aucune adresse publique. Le jeu telecharge ici " +
            "demarrera hors ligne, et se branchera sur le serveur de votre choix " +
            "avec l'option ci-dessous."));
        blocServeur.appendChild(elt("code", "ligne", "nineteen --server=http://exemple:8080"));
        return;
    }

    blocServeur.appendChild(elt("p", null,
        "Le paquet publie ici porte deja cette adresse : lancez le jeu, il vise " +
        "ce serveur sans rien avoir a taper. Pour le pointer ailleurs, ou pour un " +
        "binaire construit autrement :"));
    blocServeur.appendChild(elt("code", "ligne", "nineteen --server=" + adresse));
    blocServeur.appendChild(elt("p", "note",
        "La meme valeur se pose dans la variable NINETEEN_SERVER_URL, ou dans le " +
        "fichier nineteen.env a cote du jeu."));
}

function peindreApres(source, version) {
    if (!apres) return;
    vider(apres);

    if (source === "locale") {
        apres.appendChild(elt("p", "note",
            "Ces fichiers sont servis par ce serveur. La somme SHA-256 affichee " +
            "sous chaque bouton est calculee sur le fichier lui-meme, apres coup, " +
            "et pas recopiee d'un manifeste."));
        return;
    }
    if (source === "github") {
        apres.appendChild(elt("p", "note",
            "Ces fichiers sont publies avec la version " + version +
            " et telecharges depuis GitHub. Les sommes SHA-256 sont dans le " +
            "manifeste de signature publie a cote."));
        return;
    }
    apres.appendChild(elt("p", "note",
        "Aucun paquet n'est disponible sur ce serveur pour l'instant. Le jeu se " +
        "construit depuis les sources, et la pile Docker du depot le fabrique " +
        "elle-meme au premier demarrage."));
}

function peindreAnnexes(manifestes) {
    if (!apres || !manifestes || manifestes.length === 0) return;
    const titre = elt("h3", null, "A cote des paquets");
    const ul = elt("ul", "annexes");
    for (const m of manifestes) {
        const li = elt("li");
        const a = elt("a", null, m.nom);
        a.href = m.url;
        li.appendChild(a);
        li.appendChild(elt("span", "note", " " + octets(m.octets)));
        ul.appendChild(li);
    }
    apres.append(titre, ul);
}

async function charger() {
    etat(etatBloc, "Chargement");
    let data = null;
    try {
        data = await api("/api/v1/telechargements");
    } catch (err) {
        console.error("telechargements", err);
        etat(etatBloc, "La liste des paquets ne repond pas", "erreur");
        return;
    }

    const fichiers = data.fichiers || [];
    vider(liste);
    for (const p of PLATEFORMES) {
        liste.appendChild(carte(p, fichiers.filter((f) => f.plateforme === p.cle)));
    }

    if (fichiers.length === 0) {
        etat(etatBloc, "Aucun paquet disponible", "erreur");
    } else {
        etat(etatBloc, fichiers.length === 1
            ? "1 paquet disponible"
            : fichiers.length + " paquets disponibles", "direct");
    }

    peindreApres(data.source, data.version);
    peindreAnnexes(data.manifestes);
    peindreServeur(data.serveur);
}

charger();

})();
