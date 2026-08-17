# Backend Nineteen — version durcie

Réécriture sécurisée des endpoints critiques du site, en réponse à l'audit
(`../../SECURITY_AUDIT.md`). Chaque fichier documente en tête les constats qu'il corrige.

## Ce qui change par rapport à l'original

| Domaine | Avant | Après |
|---|---|---|
| Requêtes SQL | concaténation de `$_POST`/`$_GET` (NIN-01) | **PDO + requêtes préparées** partout ; `LIMIT/OFFSET` forcés en entiers bornés |
| Mots de passe | `md5()` non salé (NIN-06) | **Argon2id** (`password_hash`), migration transparente des anciens MD5 à la connexion |
| Clés de session | `rand()` + graine devinable (NIN-09) | **`random_bytes(32)`** (CSPRNG), rotation à la connexion |
| Jeton « secure » | MD5 d'horodatage recalculable côté client (NIN-02) | **supprimé** ; l'auth repose sur mot de passe + clé opaque |
| Erreurs | `echo $link->error` (NIN-13) | **journalisées côté serveur**, message générique au client |
| Sortie | echo brut → XSS (NIN-10) | JSON encodé / `htmlspecialchars` via `e()` |
| Identifiants DB | en dur dans `secure.php` | **variables d'environnement** |
| Débit | aucun (NIN-14) | **rate-limiting** par IP + action |
| En-têtes HTTP | aucun (NIN-19) | CSP, HSTS, X-Frame-Options, nosniff (`.htaccess` + `securityHeaders()`) |
| IP client | `X-Forwarded-For` en premier (NIN-12) | `REMOTE_ADDR` |

## Correspondance fichiers

- `db.php` — connexion PDO stricte + helpers (`respond`, `fail`, `e`, `newSessionKey`, `securityHeaders`, `rateLimit`). **Non accessible en direct.**
- `connect_api.php` — remplace `connect.php` (login + reconnexion par clé).
- `inscription_api.php` — remplace `inscription.php` (création de compte).
- `score_api.php` — remplace `updateYourScore.php` (envoi de score).
- `leaderboard_api.php` — remplace `leaderboard.php` (classement).
- `.htaccess` — en-têtes de sécurité + refus des fichiers sensibles.

## Déploiement

1. **Variables d'environnement** (jamais dans le code, jamais commit) :
   ```
   NINETEEN_DB_HOST=127.0.0.1
   NINETEEN_DB_NAME=nineteen
   NINETEEN_DB_USER=nineteen_app      # compte SQL à privilèges minimaux
   NINETEEN_DB_PASS=****
   ```
   Via `SetEnv` (Apache), `env` (systemd/php-fpm) ou un `.env` hors racine web.

2. **Supprimer du serveur public** : `disconnectAll.php` (NIN-04), la branche debug GET de
   `connect.php` (NIN-05), `test.html` / `tesst.html` (NIN-18).

3. **Client C** : réactiver la vérification TLS dans `include/libWeb.c` (NIN-03) —
   retirer les deux `CURLOPT_SSL_VERIFY*` à 0, fournir un bundle CA, viser l'épinglage.

4. **Base** : compte SQL dédié en lecture/écriture limitée aux tables `nineteen_*`,
   jamais le compte root.

## Ce que ceci ne corrige PAS (par conception)

`score_api.php` borne le champ des dégâts (paramétrage + plafond de plausibilité) mais
**fait toujours confiance à un score envoyé par le client**. La correction de fond (NIN-02,
NIN-08) est architecturale : recalculer le score côté serveur à partir d'événements de
partie signés. Voir `../../MODERNIZATION.md` § Anti-cheat.
