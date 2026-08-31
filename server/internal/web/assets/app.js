/*
 * app.js — le peu de script dont le site a besoin.
 *
 * Trois principes, hérités de l'audit :
 *
 *  1. Rien n'est inséré en HTML. Tout passe par textContent, donc un pseudo
 *     contenant `<script>` s'affiche comme du texte. Le site d'origine
 *     concaténait les valeurs de la base dans ses pages, ce qui donnait un XSS
 *     stocké : il suffisait de choisir le bon pseudo à l'inscription.
 *
 *  2. Toute requête modifiante porte le jeton CSRF. Il est lu dans un cookie
 *     accessible au script et recopié dans un en-tête ; un site tiers peut faire
 *     envoyer le cookie par le navigateur, mais ne peut pas en lire la valeur.
 *
 *  3. Aucune erreur du serveur n'est affichée telle quelle si elle n'est pas
 *     prévue : on montre un message générique et on garde le détail en console.
 */

"use strict";

/* ------------------------------------------------------------------ outils */

function readCookie(name) {
    const prefix = name + "=";
    for (const part of document.cookie.split(";")) {
        const trimmed = part.trim();
        if (trimmed.startsWith(prefix)) {
            return decodeURIComponent(trimmed.slice(prefix.length));
        }
    }
    return "";
}

async function api(path, { method = "GET", body = null } = {}) {
    const headers = {};
    if (body !== null) {
        headers["Content-Type"] = "application/json";
    }
    if (method !== "GET" && method !== "HEAD") {
        const csrf = readCookie("ns_csrf");
        if (csrf) headers["X-Nineteen-CSRF"] = csrf;
    }

    const response = await fetch(path, {
        method,
        headers,
        body: body === null ? null : JSON.stringify(body),
        credentials: "same-origin",
    });

    let payload = null;
    try {
        payload = await response.json();
    } catch {
        // Réponse non JSON : erreur d'infrastructure, pas de l'application.
    }

    if (!response.ok) {
        const message = payload && typeof payload.error === "string"
            ? payload.error
            : "Le service ne répond pas correctement.";
        const err = new Error(message);
        err.status = response.status;
        throw err;
    }
    return payload;
}

/* Formate un entier avec des espaces fines insécables comme séparateurs. */
const formatScore = new Intl.NumberFormat("fr-FR").format;

/* ------------------------------------------------------------- classement */

const boardBody = document.querySelector("#tableau-classement tbody");
const boardStatus = document.getElementById("statut-classement");
const gameSelect = document.getElementById("choix-jeu");

function setStatus(text, state) {
    if (!boardStatus) return;
    boardStatus.textContent = text;
    if (state) {
        boardStatus.dataset.state = state;
    } else {
        delete boardStatus.dataset.state;
    }
}

function renderBoard(entries) {
    boardBody.replaceChildren();

    if (!entries || entries.length === 0) {
        const row = document.createElement("tr");
        const cell = document.createElement("td");
        cell.colSpan = 3;
        cell.className = "empty";
        cell.textContent = "Aucun score enregistré pour l'instant. La première place est libre.";
        row.append(cell);
        boardBody.append(row);
        return;
    }

    const rows = entries.map((entry) => {
        const row = document.createElement("tr");

        const rank = document.createElement("td");
        rank.className = "c-rank";
        rank.textContent = String(entry.rank);

        // textContent, jamais innerHTML : le pseudo vient de la base.
        const name = document.createElement("td");
        name.textContent = entry.username;

        const score = document.createElement("td");
        score.className = "c-score";
        score.textContent = formatScore(entry.score);

        row.append(rank, name, score);
        return row;
    });
    boardBody.append(...rows);
}

async function loadBoard(gameId) {
    setStatus("Chargement…");
    try {
        const data = await api(`/api/v1/leaderboard?game=${encodeURIComponent(gameId)}&limit=20`);
        renderBoard(data.entries);
        setStatus("À jour", "live");
    } catch (err) {
        console.error("classement", err);
        renderBoard([]);
        setStatus("Indisponible", "error");
    }
}

async function loadGames() {
    try {
        const data = await api("/api/v1/games");
        if (!data.games) return;

        for (const game of data.games) {
            const option = document.createElement("option");
            option.value = String(game.id);
            const suffix = game.difficulty === "normal" ? "" : ` — ${game.difficulty}`;
            option.textContent = game.name + suffix;
            gameSelect.append(option);
        }

    } catch (err) {
        console.error("liste des jeux", err);
        // Le contenu de repli du HTML reste affiché : le site reste lisible.
    }
}

/* ==========================================================================
   Le média du site

   Tout ce qui suit se construit depuis `/media/manifeste.json`, écrit par
   `tools/site-media.py` en même temps que les fichiers eux-mêmes. C'est ce qui
   empêche la page de mentir : elle a annoncé « quinze bornes » pendant deux
   versions parce que le nombre était écrit ici à la main. Il n'y en a plus un
   seul — la galerie fait la longueur de ce que le script a réellement produit,
   qui fait lui-même la longueur de ce que la scène déclare.
   ========================================================================== */

/* Le visiteur qui a demandé moins d'animation. La requête est lue UNE fois et
   relue si le réglage change en cours de visite. */
const moinsDAnimation = window.matchMedia("(prefers-reduced-motion: reduce)");

/* La police d'enseigne (sega.ttf) n'a pas de glyphe accentué : on retire les
   diacritiques pour ce seul libellé, comme le faisait la texture de marquee
   d'origine (demineur_font.jpg). */
function sansAccent(texte) {
    return texte.normalize("NFD").replace(/\p{Diacritic}/gu, "").toUpperCase();
}

/* --- L'accueil ---------------------------------------------------------- */

/*
 * Sous `prefers-reduced-motion`, on ne se contente pas de mettre la vidéo en
 * pause : on lui RETIRE ses sources. Une vidéo en pause a déjà demandé ses
 * métadonnées, et selon le navigateur une partie du flux ; un visiteur qui a
 * demandé moins d'animation n'a aucune raison de payer ça pour voir l'affiche
 * fixe que le CSS lui montre à la place.
 */
function reglerAccueil() {
    const video = document.getElementById("video-salle");
    if (!video) return;

    // Les sources sont relevées au premier passage, avant d'être éventuellement
    // retirées : c'est ce qui permet de les remettre si le visiteur change
    // d'avis en cours de route, sans recharger la page.
    if (!reglerAccueil.sources) {
        reglerAccueil.sources = [...video.querySelectorAll("source")].map(
            (s) => [s.src, s.type]);
    }

    if (moinsDAnimation.matches) {
        video.pause();
        video.replaceChildren();
        video.removeAttribute("autoplay");
        video.load();          // annule le téléchargement en cours
        return;
    }

    if (video.querySelector("source") === null) {
        for (const [src, type] of reglerAccueil.sources) {
            const source = document.createElement("source");
            source.src = src;
            source.type = type;
            video.append(source);
        }
        video.load();
        video.play().catch(() => {
            /* Un navigateur peut refuser la lecture automatique même en sourdine.
               L'affiche reste alors visible, ce qui est exactement le repli
               voulu : on ne force rien et on n'affiche pas d'erreur. */
        });
    }
}

/* --- Les boucles de jeu ------------------------------------------------- */

function construireBoucle(jeu) {
    const item = document.createElement("li");
    item.className = "loop";

    const bouton = document.createElement("button");
    bouton.type = "button";
    bouton.className = "loop-bouton";

    const media = document.createElement("div");
    media.className = "loop-media";

    const affiche = document.createElement("img");
    affiche.src = jeu.affiche_jpg;
    affiche.alt = jeu.alt;
    affiche.width = 640;
    affiche.height = 360;
    affiche.loading = "lazy";
    affiche.decoding = "async";

    // `preload="none"` : les huit boucles ne pèsent RIEN tant qu'on n'en
    // regarde aucune. C'est ce qui permet d'en mettre huit sur une page.
    const video = document.createElement("video");
    video.muted = true;
    video.loop = true;
    video.playsInline = true;
    video.preload = "none";
    video.setAttribute("aria-hidden", "true");
    for (const [url, type] of [[jeu.webm, "video/webm"], [jeu.mp4, "video/mp4"]]) {
        const source = document.createElement("source");
        source.src = url;
        source.type = type;
        video.append(source);
    }

    media.append(affiche, video);

    const corps = document.createElement("div");
    corps.className = "loop-corps";
    const nom = document.createElement("span");
    nom.className = "loop-nom";
    nom.textContent = sansAccent(jeu.nom);
    const etat = document.createElement("span");
    etat.className = "loop-etat";
    etat.textContent = "lire";
    corps.append(nom, etat);

    bouton.append(media, corps);
    // L'étiquette dit le jeu ET l'action : au clavier on entend « Lire la
    // boucle de Démineur », pas « bouton ».
    bouton.setAttribute("aria-label", `Lire la boucle de ${jeu.nom}`);
    item.append(bouton);

    let joue = false;
    const demarrer = () => {
        if (joue) return;
        joue = true;
        item.classList.add("is-playing");
        etat.textContent = "en cours";
        bouton.setAttribute("aria-label", `Arrêter la boucle de ${jeu.nom}`);
        // `play()` rend une promesse rejetée si le navigateur refuse : on la
        // rattrape, sinon la console se remplit d'erreurs non gérées.
        video.play().catch(() => arreter());
    };
    const arreter = () => {
        if (!joue) return;
        joue = false;
        item.classList.remove("is-playing");
        etat.textContent = "lire";
        bouton.setAttribute("aria-label", `Lire la boucle de ${jeu.nom}`);
        video.pause();
    };

    bouton.addEventListener("click", () => (joue ? arreter() : demarrer()));

    // Le survol et le focus ne lancent la boucle que si l'animation est la
    // bienvenue. Le clic, lui, marche toujours : c'est une demande explicite.
    const auPassage = (entre) => {
        if (moinsDAnimation.matches) return;
        entre ? demarrer() : arreter();
    };
    bouton.addEventListener("pointerenter", () => auPassage(true));
    bouton.addEventListener("pointerleave", () => auPassage(false));
    bouton.addEventListener("focus", () => auPassage(true));
    bouton.addEventListener("blur", () => auPassage(false));

    return item;
}

/* --- Les bornes --------------------------------------------------------- */

function construireBorne(borne) {
    const item = document.createElement("li");
    item.className = "cab cab-photo";
    if (borne.jeu === "leaderboard") item.classList.add("is-new");

    const image = document.createElement("img");
    image.src = borne.image;
    image.alt = borne.alt;
    image.width = 640;
    image.height = 360;
    image.loading = "lazy";
    image.decoding = "async";

    const corps = document.createElement("div");
    corps.className = "cab-corps";

    const slot = document.createElement("span");
    slot.className = "cab-slot";
    slot.textContent = `Borne ${String(borne.slot).padStart(2, "0")}`;

    const marquee = document.createElement("span");
    marquee.className = "cab-marquee";
    marquee.textContent = sansAccent(borne.libelle);

    const meta = document.createElement("span");
    meta.className = "cab-meta";
    meta.textContent = borne.jeu === "leaderboard" ? "classement" : borne.difficulte;

    corps.append(slot, marquee, meta);
    item.append(image, corps);
    return item;
}

/* --- Chargement --------------------------------------------------------- */

async function chargerMedia() {
    const galerieJeux = document.getElementById("galerie-jeux");
    const galerieBornes = document.getElementById("galerie-bornes");

    let manifeste;
    try {
        manifeste = await api("/media/manifeste.json");
    } catch (err) {
        console.error("manifeste du média", err);
        if (galerieJeux) {
            galerieJeux.replaceChildren();
            const vide = document.createElement("li");
            vide.className = "loops-attente";
            vide.textContent = "Les boucles de jeu ne sont pas disponibles.";
            galerieJeux.append(vide);
        }
        return;
    }

    if (galerieJeux && Array.isArray(manifeste.jeux)) {
        galerieJeux.replaceChildren(...manifeste.jeux.map(construireBoucle));
    }
    if (galerieBornes && Array.isArray(manifeste.bornes)) {
        galerieBornes.replaceChildren(...manifeste.bornes.map(construireBorne));
    }
}

if (gameSelect) {
    gameSelect.addEventListener("change", () => loadBoard(gameSelect.value));
}

/* ------------------------------------------------------------------ compte */

const accountBox = document.getElementById("account");
const dialog = document.getElementById("connexion");

function renderAccount(username) {
    accountBox.replaceChildren();

    if (!username) {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "btn btn-ghost";
        button.textContent = "Se connecter";
        button.addEventListener("click", () => dialog.showModal());
        accountBox.append(button);
        return;
    }

    const who = document.createElement("span");
    who.className = "who";
    who.append("Connecté en tant que ");
    const strong = document.createElement("strong");
    strong.textContent = username;
    who.append(strong);

    const logout = document.createElement("button");
    logout.type = "button";
    logout.className = "btn btn-ghost btn-small";
    logout.textContent = "Se déconnecter";
    logout.addEventListener("click", async () => {
        try {
            await api("/api/v1/auth/logout", { method: "POST" });
        } catch (err) {
            console.error("déconnexion", err);
        }
        renderAccount(null);
    });

    accountBox.append(who, logout);
}

async function loadAccount() {
    try {
        const me = await api("/api/v1/me");
        renderAccount(me.username);
    } catch {
        renderAccount(null); // 401 attendu quand personne n'est connecté
    }
}

/* --- onglets de la fenêtre --- */

const tabLogin = document.getElementById("tab-connexion");
const tabRegister = document.getElementById("tab-inscription");
const panelLogin = document.getElementById("onglet-connexion");
const panelRegister = document.getElementById("onglet-inscription");

function selectTab(which) {
    const login = which === "login";
    tabLogin.setAttribute("aria-selected", String(login));
    tabRegister.setAttribute("aria-selected", String(!login));
    panelLogin.hidden = !login;
    panelRegister.hidden = login;
}

tabLogin?.addEventListener("click", () => selectTab("login"));
tabRegister?.addEventListener("click", () => selectTab("register"));

/* --- soumission --- */

function showFormError(form, message) {
    const box = form.querySelector(".form-error");
    if (!box) return;
    box.textContent = message;
    box.hidden = !message;
}

function bindAuthForm(form, endpoint, buildBody) {
    form?.addEventListener("submit", async (event) => {
        event.preventDefault();
        showFormError(form, "");

        const submit = form.querySelector("button[type=submit]");
        submit.disabled = true;

        try {
            const data = new FormData(form);
            const result = await api(endpoint, { method: "POST", body: buildBody(data) });
            renderAccount(result.username);
            form.reset();
            dialog.close();
            // Le classement peut contenir le joueur : on le rafraîchit.
            loadBoard(gameSelect ? gameSelect.value : 0);
        } catch (err) {
            showFormError(form, err.message);
        } finally {
            submit.disabled = false;
        }
    });
}

bindAuthForm(panelLogin, "/api/v1/auth/login", (data) => ({
    username: String(data.get("username") || ""),
    password: String(data.get("password") || ""),
}));

bindAuthForm(panelRegister, "/api/v1/auth/register", (data) => ({
    username: String(data.get("username") || ""),
    email: String(data.get("email") || ""),
    password: String(data.get("password") || ""),
}));

document.querySelectorAll("[data-open=connexion]").forEach((el) => {
    el.addEventListener("click", () => dialog.showModal());
});

/* ----------------------------------------------------------------- version */

async function loadVersion() {
    try {
        const data = await api("/api/v1/version");
        document.querySelectorAll("[data-version]").forEach((el) => {
            el.textContent = data.version;
        });
        // Les liens de téléchargement pointent vers la version publiée sur
        // GitHub : les paquets sont produits par l'intégration continue, le
        // serveur n'a pas à les héberger.
        //
        // MAIS SEULEMENT SI ELLE EXISTE, et c'est une mesure qui l'a imposé.
        // Cette fonction bâtissait les trois liens à partir du seul numéro de
        // version. Vérifié contre l'API GitHub : le dépôt répond 200,
        // `releases/tags/v17.0.0` répond 404, et la liste des releases est
        // vide. Les trois boutons « Télécharger » — ce pour quoi la page
        // existe — étaient trois 404. Un bouton mort est pire qu'un bouton
        // absent : il fait douter du reste de la page.
        //
        // C'est le SERVEUR qui sait, et pas le navigateur : interroger GitHub
        // depuis chaque visiteur coûterait une requête par chargement pour une
        // information qui ne change qu'aux versions, sur une API limitée à
        // soixante appels par heure et par adresse.
        const publiee = data.publiee === true;
        const base = "https://github.com/griisemine/NineTeen/releases/download/v" + data.version;
        // CES TROIS NOMS SONT CEUX QUE LA RELEASE PRODUIT, PAS CEUX QU'ON
        // AIMERAIT. Les précédents étaient faux tous les trois, et la garde
        // au-dessus (`publiee`) ne pouvait pas le voir : elle vérifie que la
        // release existe, pas que le fichier demandé s'y trouve. Le jour où
        // `publiee` serait passée à 1, les trois boutons auraient rendu 404
        // une seconde fois.
        //
        //   .zip                -> l'empaquetage Windows est un installateur
        //                          NSIS depuis la 17.0.0, donc un .exe
        //   -macos-             -> CPack écrit « macOS », avec la capitale
        //   -linux-x64.AppImage -> `paquets.sh` nomme l'AppImage d'après
        //                          `uname -m`, comme le veut la convention
        //                          AppImage : « x86_64 », et pas de « linux »
        //
        // `TestLesLiensDeTelechargementExistentVraiment` les compare désormais
        // à `packaging/`, pour que la prochaine divergence se voie ici et non
        // sur la page.
        const files = {
            windows: `Nineteen-${data.version}-windows-x64.exe`,
            macos: `Nineteen-${data.version}-macOS-universal.dmg`,
            linux: `Nineteen-${data.version}-x86_64.AppImage`,
        };
        document.querySelectorAll("[data-dl]").forEach((el) => {
            const key = el.dataset.dl;
            if (!files[key]) return;
            if (publiee) {
                el.href = `${base}/${files[key]}`;
                el.removeAttribute("aria-disabled");
                el.classList.remove("btn-attente");
                return;
            }
            // Ni `href`, ni rôle de lien : un lecteur d'écran ne doit pas
            // annoncer « lien » sur ce qui n'en est pas un.
            el.removeAttribute("href");
            el.setAttribute("aria-disabled", "true");
            el.classList.add("btn-attente");
            el.textContent = "Bientôt";
        });
        const attente = document.getElementById("note-attente");
        if (attente) attente.hidden = publiee;

        // L'ADRESSE À DONNER AU JEU, écrite telle qu'on la tape.
        //
        // Elle vient du SERVEUR (`NINETEEN_PUBLIC_URL`) et non de
        // `window.location` : les deux coïncident en développement et divergent
        // dès qu'un proxy TLS est devant. Le site est joint en `https://`, que
        // le client du jeu refuse explicitement d'ouvrir — bâtir la ligne à
        // partir de l'origine de la page donnerait donc au joueur une URL que
        // son jeu ne sait pas lire, sur le déploiement même où l'aide compte le
        // plus. Le serveur est le seul à connaître son adresse joignable.
        //
        // `textContent`, comme partout ici : cette valeur vient de la
        // configuration, mais rien sur cette page n'entre en HTML.
        const noteServeur = document.getElementById("note-serveur");
        const ligneServeur = document.getElementById("ligne-serveur");
        const adresse = typeof data.serveur === "string" ? data.serveur : "";
        if (noteServeur && ligneServeur) {
            ligneServeur.textContent = "nineteen --server=" + adresse;
            // Rien à annoncer : on cache le bloc plutôt que de montrer une
            // ligne à trou. Un serveur lancé sans rien dire n'affirme rien.
            noteServeur.hidden = adresse === "";
        }
    } catch (err) {
        console.error("version", err);
    }
}

/* -------------------------------------------------------------- démarrage */

reglerAccueil();
moinsDAnimation.addEventListener("change", reglerAccueil);

chargerMedia();
loadGames().then(() => loadBoard(0));
loadAccount();
loadVersion();
