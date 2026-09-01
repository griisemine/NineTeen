/*
 * accueil.js, les deux galeries de la page d'accueil.
 *
 * Elles se construisent depuis /media/manifeste.json, ecrit par
 * tools/site-media.py en meme temps que les fichiers eux-memes. C'est ce qui
 * empeche la page de mentir : elle a annonce quinze bornes pendant deux
 * versions parce que le nombre etait ecrit a la main. Il n'y en a plus un seul,
 * la galerie fait la longueur de ce que le script a reellement produit, qui
 * fait lui-meme la longueur de ce que la scene declare.
 *
 * La troisieme galerie, celle des vues de la salle, est ecrite en dur dans la
 * page et c'est delibere : elle est ce qui reste quand ce script ne s'execute
 * pas.
 */

"use strict";

(function () {

const { elt, vider } = window.NS;

/* Le visiteur qui a demande moins d'animation. La requete est lue une fois et
 * relue si le reglage change en cours de visite. */
const moinsDAnimation = window.matchMedia("(prefers-reduced-motion: reduce)");

/* La police d'enseigne (sega.ttf) n'a pas de glyphe accentue : on retire les
 * diacritiques pour ce seul libelle, comme le faisait la texture de marquee
 * d'origine. */
function sansAccent(texte) {
    return String(texte).normalize("NFD").replace(/\p{Diacritic}/gu, "").toUpperCase();
}

/* --- L'accueil ---------------------------------------------------------- */

/*
 * Sous prefers-reduced-motion, on ne se contente pas de mettre la video en
 * pause : on lui RETIRE ses sources. Une video en pause a deja demande ses
 * metadonnees, et selon le navigateur une partie du flux. Un visiteur qui a
 * demande moins d'animation n'a aucune raison de payer cela pour voir l'affiche
 * fixe que le CSS lui montre a la place.
 */
function reglerAccueil() {
    const video = document.getElementById("video-salle");
    if (!video) return;

    /* Les sources sont relevees au premier passage, avant d'etre eventuellement
       retirees : c'est ce qui permet de les remettre si le visiteur change
       d'avis en cours de route, sans recharger la page. */
    if (!reglerAccueil.sources) {
        reglerAccueil.sources = [...video.querySelectorAll("source")].map(
            (s) => [s.src, s.type]);
    }

    if (moinsDAnimation.matches) {
        video.pause();
        vider(video);
        video.removeAttribute("autoplay");
        video.load();
        return;
    }

    if (video.querySelector("source") === null) {
        for (const [src, type] of reglerAccueil.sources) {
            const source = elt("source");
            source.src = src;
            source.type = type;
            video.appendChild(source);
        }
        video.load();
        video.play().catch(() => {
            /* Un navigateur peut refuser la lecture automatique meme en
               sourdine. L'affiche reste alors visible, ce qui est exactement le
               repli voulu. */
        });
    }
}

/* --- Les boucles de jeu ------------------------------------------------- */

function construireBoucle(jeu) {
    const item = elt("li", "loop");
    const bouton = elt("button", "loop-bouton");
    bouton.type = "button";

    const media = elt("div", "loop-media");

    const affiche = elt("img");
    affiche.src = jeu.affiche_jpg;
    affiche.alt = jeu.alt;
    affiche.width = 640;
    affiche.height = 360;
    affiche.loading = "lazy";
    affiche.decoding = "async";

    /* preload="none" : les huit boucles ne pesent rien tant qu'on n'en regarde
       aucune. C'est ce qui permet d'en mettre huit sur une page. */
    const video = elt("video");
    video.muted = true;
    video.loop = true;
    video.playsInline = true;
    video.preload = "none";
    video.setAttribute("aria-hidden", "true");
    for (const [url, type] of [[jeu.webm, "video/webm"], [jeu.mp4, "video/mp4"]]) {
        const source = elt("source");
        source.src = url;
        source.type = type;
        video.appendChild(source);
    }

    media.append(affiche, video);

    const corps = elt("div", "loop-corps");
    const nom = elt("span", "loop-nom", sansAccent(jeu.nom));
    const etiquette = elt("span", "loop-etat", "lire");
    corps.append(nom, etiquette);

    bouton.append(media, corps);
    /* L'etiquette dit le jeu ET l'action : au clavier on entend « Lire la
       boucle de Demineur », pas « bouton ». */
    bouton.setAttribute("aria-label", "Lire la boucle de " + jeu.nom);
    item.appendChild(bouton);

    let joue = false;
    const demarrer = () => {
        if (joue) return;
        joue = true;
        item.classList.add("is-playing");
        etiquette.textContent = "en cours";
        bouton.setAttribute("aria-label", "Arreter la boucle de " + jeu.nom);
        video.play().catch(() => arreter());
    };
    const arreter = () => {
        if (!joue) return;
        joue = false;
        item.classList.remove("is-playing");
        etiquette.textContent = "lire";
        bouton.setAttribute("aria-label", "Lire la boucle de " + jeu.nom);
        video.pause();
    };

    bouton.addEventListener("click", () => (joue ? arreter() : demarrer()));

    /* Le survol et le focus ne lancent la boucle que si l'animation est la
       bienvenue. Le clic, lui, marche toujours : c'est une demande explicite. */
    const auPassage = (entre) => {
        if (moinsDAnimation.matches) return;
        if (entre) demarrer(); else arreter();
    };
    bouton.addEventListener("pointerenter", () => auPassage(true));
    bouton.addEventListener("pointerleave", () => auPassage(false));
    bouton.addEventListener("focus", () => auPassage(true));
    bouton.addEventListener("blur", () => auPassage(false));

    return item;
}

/* --- Les bornes --------------------------------------------------------- */

function construireBorne(borne) {
    const item = elt("li", "cab");

    const image = elt("img");
    image.src = borne.image;
    image.alt = borne.alt;
    image.width = 640;
    image.height = 360;
    image.loading = "lazy";
    image.decoding = "async";

    const corps = elt("div", "cab-corps");
    corps.appendChild(elt("span", "cab-slot", "Borne " + borne.slot));
    corps.appendChild(elt("span", "cab-marquee", sansAccent(borne.libelle)));

    const detail = borne.jeu === "leaderboard"
        ? "Le classement de la salle"
        : "Regime " + borne.difficulte;
    corps.appendChild(elt("span", "cab-detail", detail));

    item.append(image, corps);
    return item;
}

/* --- Chargement --------------------------------------------------------- */

async function chargerMedia() {
    const galerieJeux = document.getElementById("galerie-jeux");
    const galerieBornes = document.getElementById("galerie-bornes");
    if (!galerieJeux && !galerieBornes) return;

    let manifeste = null;
    try {
        const reponse = await fetch("/media/manifeste.json", { credentials: "same-origin" });
        if (!reponse.ok) throw new Error("manifeste " + reponse.status);
        manifeste = await reponse.json();
    } catch (err) {
        console.error("manifeste", err);
        /* Le contenu de repli reste affiche. La page ne perd que ses deux
           galeries construites, pas sa lisibilite. */
        return;
    }

    if (galerieJeux && Array.isArray(manifeste.jeux)) {
        vider(galerieJeux);
        galerieJeux.append(...manifeste.jeux.map(construireBoucle));
    }

    if (galerieBornes && Array.isArray(manifeste.bornes)) {
        vider(galerieBornes);
        galerieBornes.append(...manifeste.bornes.map(construireBorne));
    }

    /* LES CHIFFRES DE LA PAGE VIENNENT DU MANIFESTE, jamais de la main. Celui
       qui les ecrivait a la main a annonce quinze bornes pendant deux
       versions. */
    const compte = manifeste.compte || {};
    for (const el of document.querySelectorAll("[data-chiffre]")) {
        const valeur = compte[el.dataset.chiffre];
        if (typeof valeur === "number") el.textContent = String(valeur);
    }
}

reglerAccueil();
moinsDAnimation.addEventListener("change", reglerAccueil);
chargerMedia();

})();
