<?php
/**
 * score_api.php — Enregistrement de score durci.
 *
 * Corrige : NIN-01 (SQL préparé, gameId/score typés), NIN-02 (plus de jeton
 *           "secure" bidon), NIN-13, NIN-14.
 *
 * IMPORTANT (NIN-02 / NIN-08) : ceci reste une correction de surface. La vraie
 * défense anti-triche est de recalculer le score côté serveur à partir
 * d'événements de partie signés — voir MODERNIZATION.md § "Anti-cheat". Ici on
 * borne au moins le champ des dégâts : requêtes paramétrées + garde-fou de
 * plausibilité (score dans une plage crédible par jeu).
 */

declare(strict_types=1);
require __DIR__ . '/db.php';
securityHeaders();

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    respond(405, ['ok' => false, 'error' => 'méthode non autorisée']);
}
if (!rateLimit('score', 30)) {
    respond(429, ['ok' => false, 'error' => 'trop de requêtes']);
}

$gameId     = filter_input(INPUT_POST, 'gameID', FILTER_VALIDATE_INT);
$score      = filter_input(INPUT_POST, 'score', FILTER_VALIDATE_INT);
$sessionKey = (string) ($_POST['key'] ?? '');

if ($gameId === false || $gameId === null || $gameId < 1 || $gameId > 15) {
    respond(400, ['ok' => false, 'error' => 'gameId invalide']);
}
if ($score === false || $score === null || $score < 0) {
    respond(400, ['ok' => false, 'error' => 'score invalide']);
}
if ($sessionKey === '') {
    respond(400, ['ok' => false, 'error' => 'session manquante']);
}

// Garde-fou de plausibilité : plafond crédible par jeu (à ajuster).
// Un score au-delà est refusé plutôt que stocké (NIN-02 mitigation).
const SCORE_MAX = [
    1 => 500000, 2 => 100000, 3 => 100000, 4 => 2000000, 5 => 2000000,
    6 => 100000, 7 => 2000000, 8 => 200000, 9 => 200000, 10 => 200000,
    11 => 200000, 12 => 200000, 13 => 200000, 14 => 200000, 15 => 200000,
];
if ($score > (SCORE_MAX[$gameId] ?? 200000)) {
    fail("score implausible g={$gameId} s={$score}", 422, 'score refusé');
}

try {
    $pdo = db();

    // Résoudre le userId depuis la clé de session (préparé).
    $stmt = $pdo->prepare('SELECT userId FROM nineteen_session WHERE sessionKey = ? LIMIT 1');
    $stmt->execute([$sessionKey]);
    $sess = $stmt->fetch();
    if (!$sess) {
        respond(401, ['ok' => false, 'error' => 'session invalide']);
    }

    // Mise à jour "meilleur score uniquement", entièrement paramétrée.
    $upd = $pdo->prepare(
        'UPDATE nineteen_scores
            SET score = GREATEST(score, ?)
          WHERE gameId = ? AND userId = ?'
    );
    $upd->execute([$score, $gameId, (int) $sess['userId']]);

    respond(200, ['ok' => true]);

} catch (Throwable $ex) {
    fail('score: ' . $ex->getMessage(), 500, 'erreur serveur');
}
