<?php
/**
 * inscription_api.php — Inscription durcie.
 *
 * Corrige : NIN-01 (SQL préparé), NIN-06 (Argon2id, politique de mot de passe),
 *           NIN-10 (encodage, pas de renvoi brut), NIN-13, NIN-14 (rate limit),
 *           NIN-15 (CSRF — à brancher côté formulaire HTML).
 */

declare(strict_types=1);
require __DIR__ . '/db.php';
securityHeaders();

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    respond(405, ['ok' => false, 'error' => 'méthode non autorisée']);
}
if (!rateLimit('signup', 5)) {
    respond(429, ['ok' => false, 'error' => 'trop de tentatives, réessayez plus tard']);
}

$username = trim((string) ($_POST['identifiant'] ?? ''));
$password = (string) ($_POST['password'] ?? '');
$email    = trim((string) ($_POST['email'] ?? ''));

$errors = [];

// --- Politique de mot de passe (NIN-06) : 12+ caractères, pas de restriction de charset ---
if (mb_strlen($password) < 12) {
    $errors['password'] = '12 caractères minimum';
}
// --- Identifiant : lettres/chiffres/tiret/underscore, 3–24 ---
if (!preg_match('/^[A-Za-z0-9_-]{3,24}$/', $username)) {
    $errors['identifiant'] = '3 à 24 caractères : lettres, chiffres, - et _';
}
// --- Email ---
if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
    $errors['email'] = "format d'email incorrect";
}

if ($errors) {
    respond(422, ['ok' => false, 'errors' => $errors]);
}

try {
    $pdo = db();

    // Unicité de l'identifiant (préparé)
    $stmt = $pdo->prepare('SELECT 1 FROM nineteen_players WHERE username = ? LIMIT 1');
    $stmt->execute([$username]);
    if ($stmt->fetch()) {
        respond(409, ['ok' => false, 'errors' => ['identifiant' => 'déjà utilisé']]);
    }

    $hash = password_hash($password, PASSWORD_ARGON2ID);

    // Transaction : créer le joueur + initialiser ses 15 lignes de score de façon atomique
    $pdo->beginTransaction();

    $ins = $pdo->prepare('INSERT INTO nineteen_players (username, password, email) VALUES (?, ?, ?)');
    $ins->execute([$username, $hash, $email]);
    $userId = (int) $pdo->lastInsertId();

    $insScore = $pdo->prepare('INSERT INTO nineteen_scores (userId, gameId) VALUES (?, ?)');
    for ($gameId = 1; $gameId <= 15; $gameId++) {
        $insScore->execute([$userId, $gameId]);
    }

    $pdo->commit();
    respond(201, ['ok' => true, 'message' => 'inscription réussie']);

} catch (Throwable $ex) {
    if (isset($pdo) && $pdo->inTransaction()) {
        $pdo->rollBack();
    }
    fail('signup: ' . $ex->getMessage(), 500, 'erreur serveur');
}
