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

        // Le rail de bornes est rempli depuis la même source, pour qu'ajouter un
        // jeu en base suffise à le faire apparaître sur le site.
        const rail = document.getElementById("rail-jeux");
        const seen = new Map();
        for (const game of data.games) {
            const key = game.name;
            if (!seen.has(key)) seen.set(key, []);
            seen.get(key).push(game.difficulty);
        }
        if (seen.size > 0) {
            rail.replaceChildren();
            for (const [name, difficulties] of seen) {
                const item = document.createElement("li");
                item.className = "cab";

                const marquee = document.createElement("span");
                marquee.className = "cab-marquee";
                /* La police d'enseigne n'a pas de glyphe accentué : on retire les
                   diacritiques pour ce seul libellé, comme le faisait la texture
                   de marquee d'origine (demineur_font.jpg). */
                marquee.textContent = name
                    .normalize("NFD")
                    .replace(/\p{Diacritic}/gu, "")
                    .toUpperCase();

                const meta = document.createElement("span");
                meta.className = "cab-meta";
                if (difficulties.length === 1 && difficulties[0] === "normal") {
                    meta.textContent = "nouvelle borne";
                    item.classList.add("is-new");
                } else {
                    meta.textContent = difficulties
                        .map((d) => (d === "easy" ? "facile" : d === "hard" ? "difficile" : d))
                        .join(" · ");
                }
                item.append(marquee, meta);
                rail.append(item);
            }
        }
    } catch (err) {
        console.error("liste des jeux", err);
        // Le contenu de repli du HTML reste affiché : le site reste lisible.
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
        const base = "https://github.com/griisemine/NineTeen/releases/download/v" + data.version;
        const files = {
            windows: `Nineteen-${data.version}-windows-x64.zip`,
            macos: `Nineteen-${data.version}-macos-universal.dmg`,
            linux: `Nineteen-${data.version}-linux-x64.AppImage`,
        };
        document.querySelectorAll("[data-dl]").forEach((el) => {
            const key = el.dataset.dl;
            if (files[key]) el.href = `${base}/${files[key]}`;
        });
    } catch (err) {
        console.error("version", err);
    }
}

/* -------------------------------------------------------------- démarrage */

loadGames().then(() => loadBoard(0));
loadAccount();
loadVersion();
