<?php
/**
 * leaderboard_api.php — Classement durci.
 *
 * Corrige : NIN-01 (SQL préparé, LIMIT/OFFSET typés en int — pas d'UNION),
 *           NIN-10 (JSON, pas d'écho de contenu utilisateur en HTML), NIN-13.
 */

declare(strict_types=1);
require __DIR__ . '/db.php';
securityHeaders();

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    respond(405, ['ok' => false, 'error' => 'méthode non autorisée']);
}

$gameId = filter_input(INPUT_POST, 'gameID', FILTER_VALIDATE_INT);
$offset = filter_input(INPUT_POST, 'offset', FILTER_VALIDATE_INT);
$limit  = filter_input(INPUT_POST, 'limite', FILTER_VALIDATE_INT);

// LIMIT/OFFSET ne peuvent pas être des paramètres liés dans MySQL prepared :
// on les force donc en entiers bornés (jamais de chaîne interpolée). NIN-01.
$offset = ($offset === false || $offset === null || $offset < 0) ? 0 : min($offset, 100000);
$limit  = ($limit === false || $limit === null || $limit < 1) ? 15 : min($limit, 100);

try {
    $pdo = db();

    if ($gameId === 0) {
        // Classement global (somme pondérée par multiplicateur).
        $sql = "SELECT p.username,
                       FLOOR(SUM(s.score * m.multiplicator)) AS total
                  FROM nineteen_scores s
                  JOIN nineteen_multiplicators m ON m.gameId = s.gameId
                  JOIN nineteen_players p        ON p.userId = s.userId
              GROUP BY s.userId
              ORDER BY total DESC, p.username
                 LIMIT {$limit} OFFSET {$offset}";
        $rows = $pdo->query($sql)->fetchAll();
    } else {
        if ($gameId === false || $gameId === null || $gameId < 1 || $gameId > 15) {
            respond(400, ['ok' => false, 'error' => 'gameId invalide']);
        }
        // gameId validé (int 1–15), limit/offset entiers bornés.
        $sql = "SELECT p.username, s.score
                  FROM nineteen_players p
                  JOIN nineteen_scores s ON s.userId = p.userId
                 WHERE s.gameId = {$gameId}
              ORDER BY s.score DESC, p.username
                 LIMIT {$limit} OFFSET {$offset}";
        $rows = $pdo->query($sql)->fetchAll();
    }

    respond(200, ['ok' => true, 'entries' => $rows]);

} catch (Throwable $ex) {
    fail('leaderboard: ' . $ex->getMessage(), 500, 'erreur serveur');
}
