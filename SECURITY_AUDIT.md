# Nineteen — Audit de sécurité (rétro-analyse du code d'origine, v1.1.2)

> Version dossier interactif : artifact HTML publié séparément.
> Version code corrigée : `site web/hardened/`.

Revue de code statique du client C (`main.c`, `include/libWeb.c`, `include/hashage.c`,
`include/communFunctions.c`) et des endpoints PHP (`site web/*.php`).

**Verdict : non déployable en l'état.** Les intentions défensives étaient présentes
(jeton anti-rejeu, protection mémoire du score, échappement du ticket, validation d'email),
mais l'exécution laisse passer des contournements triviaux.

## Résumé

| Sévérité | Nb | IDs |
|---|---|---|
| 🔴 Critique | 5 | NIN-01 · NIN-02 · NIN-03 · NIN-04 · NIN-05 |
| 🟠 Élevé | 6 | NIN-06 · NIN-07 · NIN-08 · NIN-09 · NIN-10 · NIN-11 |
| 🟡 Moyen | 5 | NIN-12 · NIN-13 · NIN-14 · NIN-15 · NIN-16 |
| 🟢 Faible/Info | 4 | NIN-17 · NIN-18 · NIN-19 · NIN-20 |

## Critiques

**NIN-01 — Injection SQL généralisée** (CWE-89) · `connect.php`, `updateYourScore.php`,
`leaderboard.php`, `coins.php`, `inscription.php`, `index.php`.
Toutes les requêtes concatènent les entrées utilisateur. `username = admin' -- ` = bypass
d'authentification ; `$score`/`$gameId` injectables = réécriture arbitraire du classement.
→ Requêtes préparées partout (`site web/hardened/`).

**NIN-02 — Le jeton « secure » ne protège rien** (CWE-330/294) · `fonction.php`, `libWeb.c`.
`md5(date + secret_en_dur + heure/min/sec)`. Le client récupère l'heure serveur puis
recompose le même MD5 : le « secret » est dans le binaire et l'entrée est publique.
→ Autorité du score côté serveur (voir MODERNIZATION.md).

**NIN-03 — Vérification TLS désactivée** (CWE-295) · `libWeb.c`.
`CURLOPT_SSL_VERIFYHOST 0` + `CURLOPT_SSL_VERIFYPEER 0` → MITM complet (identifiants, clé de session).
→ Retirer les deux options, bundle CA, épinglage.

**NIN-04 — Déconnexion globale non authentifiée** (CWE-306) · `disconnectAll.php`.
`DELETE FROM nineteen_session WHERE 1` en accès anonyme = DoS trivial. → Supprimer / auth admin.

**NIN-05 — Endpoint debug divulguant des jetons valides** (CWE-215) · `connect.php` (GET).
Imprime `secure(0..10)`. → Retirer tout debug de production.

## Élevées

**NIN-06 — MD5 non salé** (CWE-916) + politique « alphanumérique, 4 car. min ». → Argon2id, 12 car. min.
**NIN-07 — Commandes shell par `sprintf`+`system`** (CWE-78) · `communFunctions.c`. → `SDL_ShowSimpleMessageBox`.
**NIN-08 — Protection mémoire du score = obfuscation** (CWE-656) · `hashage.c`. Fonction linéaire inversible, clés en mémoire voisine, `rand()` non semé, division entière lossy. → Autorité serveur.
**NIN-09 — Clés de session non cryptographiques** (CWE-338) · `random()`/`rand()`. → `random_bytes(32)`.
**NIN-10 — XSS réfléchi/stocké** (CWE-79) · `inscription.php`, `index.php`. → `htmlspecialchars` + CSP.
**NIN-11 — Débordements de tampon C** (CWE-120/131) · `strcpy(key, response)` non borné ; `_malloc` fait `sizeof(type)` sur une valeur int. → bornage, taille réelle, `-fsanitize=address,undefined`.

## Moyennes

**NIN-12** — Spoofing IP via `X-Forwarded-For`. → `REMOTE_ADDR`.
**NIN-13** — Divulgation d'erreurs SQL au client. → log serveur + message générique.
**NIN-14** — Aucun rate-limiting / anti-bruteforce. → limitation par IP+compte.
**NIN-15** — Aucun jeton CSRF sur les formulaires. → token par session.
**NIN-16** — Sessions sans expiration ni rotation (fixation). → TTL + rotation à la connexion.

## Faibles / Info

**NIN-17** — Secrets en dur dans le binaire distribué. → rien de sensible côté client.
**NIN-18** — `test.html` / `tesst.html` exposés. → retirer du déploiement.
**NIN-19** — Pas d'en-têtes de sécurité HTTP. → CSP/HSTS/X-Frame-Options (`hardened/.htaccess`).
**NIN-20** — `download.php` : mapping fixe (OK) mais `filename=$fichier` non contrôlé. → `basename()` + liste blanche.

## Racine commune & priorités

NIN-01, NIN-02 et NIN-08 partagent la même cause : **le serveur fait confiance à un score
calculé par le client.** Correction de fond = déplacer l'autorité du score vers le serveur.

| Priorité | Action | Effort |
|---|---|---|
| P0 | Supprimer `disconnectAll.php` + debug GET (NIN-04, 05) | minutes |
| P0 | Requêtes préparées partout (NIN-01) | 1 j |
| P0 | Réactiver TLS client (NIN-03) | minutes |
| P1 | Argon2id + CSPRNG sessions (NIN-06, 09) | heures |
| P1 | Encodage sortie + CSP, retirer `system()` (NIN-10, 07) | heures |
| P2 | Score serveur (NIN-02, 08) — refonte | projet |
| P2 | Rate-limit, CSRF, en-têtes, sanitizers CI | jours |
