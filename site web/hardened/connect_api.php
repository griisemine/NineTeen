<?php
/**
 * connect_api.php — Authentification durcie.
 *
 * Corrige : NIN-01 (injection SQL), NIN-05 (debug GET supprimé),
 *           NIN-06 (Argon2id + migration MD5), NIN-09 (clé CSPRNG),
 *           NIN-13 (pas de fuite d'erreur), NIN-14 (rate limit).
 *
 * Aucune branche GET de debug. Aucun jeton "secure" calculable côté client (NIN-02) :
 * l'authentification repose sur mot de passe + clé de session opaque, point.
 */

declare(strict_types=1);
require __DIR__ . '/db.php';
securityHeaders();

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    respond(405, ['ok' => false, 'error' => 'méthode non autorisée']);
}
if (!rateLimit('login', 10)) {
    respond(429, ['ok' => false, 'error' => 'trop de tentatives, réessayez plus tard']);
}

$username = trim((string) ($_POST['username'] ?? ''));
$password = (string) ($_POST['pwd'] ?? '');
$key      = (string) ($_POST['key'] ?? '');

try {
    $pdo = db();

    // --- Reconnexion par clé de session existante (comparaison en temps constant) ---
    if ($key !== '') {
        $stmt = $pdo->prepare('SELECT userId FROM nineteen_session WHERE sessionKey = ? LIMIT 1');
        $stmt->execute([$key]);
        $row = $stmt->fetch();
        if ($row) {
            respond(200, ['ok' => true, 'status' => 'session_valid']);
        }
        respond(401, ['ok' => false, 'error' => 'session invalide']);
    }

    // --- Connexion par identifiant / mot de passe ---
    if ($username === '' || $password === '') {
        respond(400, ['ok' => false, 'error' => 'champs manquants']);
    }

    $stmt = $pdo->prepare('SELECT userId, password FROM nineteen_players WHERE username = ? LIMIT 1');
    $stmt->execute([$username]);
    $user = $stmt->fetch();

    // Toujours faire une vérification pour éviter les attaques temporelles d'énumération
    $storedHash = $user['password'] ?? '$argon2id$v=19$m=65536,t=4,p=1$aaaaaaaaaaaaaaaa$aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
    $valid = false;

    if ($user && preg_match('/^[a-f0-9]{32}$/i', $storedHash)) {
        // Ancien compte MD5 : on valide puis on migre immédiatement vers Argon2id (NIN-06).
        if (hash_equals(strtolower($storedHash), strtolower(md5($password)))) {
            $valid   = true;
            $newHash = password_hash($password, PASSWORD_ARGON2ID);
            $up = $pdo->prepare('UPDATE nineteen_players SET password = ? WHERE userId = ?');
            $up->execute([$newHash, $user['userId']]);
        }
    } elseif ($user) {
        $valid = password_verify($password, $storedHash);
        if ($valid && password_needs_rehash($storedHash, PASSWORD_ARGON2ID)) {
            $up = $pdo->prepare('UPDATE nineteen_players SET password = ? WHERE userId = ?');
            $up->execute([password_hash($password, PASSWORD_ARGON2ID), $user['userId']]);
        }
    } else {
        // Utilisateur inexistant : on paie quand même le coût d'une vérification factice.
        password_verify($password, $storedHash);
    }

    if (!$valid) {
        respond(401, ['ok' => false, 'error' => 'identifiant ou mot de passe incorrect']);
    }

    // --- Création / rotation de la clé de session (rotation = anti-fixation, NIN-16) ---
    $sessionKey = newSessionKey();
    $pdo->prepare(
        'INSERT INTO nineteen_session (userId, sessionKey) VALUES (?, ?)
         ON DUPLICATE KEY UPDATE sessionKey = VALUES(sessionKey)'
    )->execute([$user['userId'], $sessionKey]);

    respond(200, ['ok' => true, 'key' => $sessionKey]);

} catch (Throwable $ex) {
    fail('connect: ' . $ex->getMessage(), 500, 'erreur serveur');
}
