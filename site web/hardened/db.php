<?php
/**
 * db.php — Connexion PDO durcie + helpers de sécurité.
 *
 * Remplace include/secure.php (mysqli + concaténation). Corrige NIN-01, NIN-13.
 *
 * Les identifiants NE SONT PAS dans le code : ils viennent de variables
 * d'environnement (fichier .env hors racine web, ou config du serveur).
 *   NINETEEN_DB_HOST, NINETEEN_DB_NAME, NINETEEN_DB_USER, NINETEEN_DB_PASS
 */

declare(strict_types=1);

/** Renvoie une connexion PDO configurée en mode strict (exceptions, pas d'émulation). */
function db(): PDO
{
    static $pdo = null;
    if ($pdo instanceof PDO) {
        return $pdo;
    }

    $host = getenv('NINETEEN_DB_HOST') ?: '127.0.0.1';
    $name = getenv('NINETEEN_DB_NAME') ?: 'nineteen';
    $user = getenv('NINETEEN_DB_USER') ?: '';
    $pass = getenv('NINETEEN_DB_PASS') ?: '';

    $dsn = "mysql:host={$host};dbname={$name};charset=utf8mb4";
    $pdo = new PDO($dsn, $user, $pass, [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION, // erreurs -> exceptions
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES   => false,                 // vraies requêtes préparées
    ]);
    return $pdo;
}

/** Réponse JSON propre (jamais d'écho de contenu utilisateur brut). NIN-10, NIN-13. */
function respond(int $httpCode, array $payload): void
{
    http_response_code($httpCode);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode($payload, JSON_UNESCAPED_UNICODE);
    exit;
}

/** Journalise l'erreur côté serveur, renvoie un message générique au client. NIN-13. */
function fail(string $internalMessage, int $httpCode = 400, string $publicMessage = 'requête invalide'): void
{
    error_log('[nineteen] ' . $internalMessage);
    respond($httpCode, ['ok' => false, 'error' => $publicMessage]);
}

/** Encodage de sortie systématique pour tout contexte HTML. NIN-10. */
function e(string $value): string
{
    return htmlspecialchars($value, ENT_QUOTES | ENT_HTML5, 'UTF-8');
}

/** Jeton de session opaque, CSPRNG, sans biais. Remplace random()/rand(). NIN-09. */
function newSessionKey(): string
{
    return bin2hex(random_bytes(32)); // 256 bits
}

/** En-têtes de sécurité HTTP à appeler en tête de chaque endpoint. NIN-19. */
function securityHeaders(): void
{
    header('X-Content-Type-Options: nosniff');
    header('X-Frame-Options: DENY');
    header('Referrer-Policy: no-referrer');
    header("Content-Security-Policy: default-src 'none'; frame-ancestors 'none'");
    // HSTS : à activer une fois le HTTPS confirmé partout
    header('Strict-Transport-Security: max-age=31536000; includeSubDomains');
}

/**
 * Limitation de débit minimale par IP+action (NIN-14). Stockage fichier simple ;
 * en production, préférer Redis/APCu. Renvoie true si la requête est autorisée.
 */
function rateLimit(string $action, int $maxPerMinute = 20): bool
{
    $ip  = $_SERVER['REMOTE_ADDR'] ?? '0.0.0.0'; // NIN-12 : jamais X-Forwarded-For ici
    $key = sys_get_temp_dir() . '/nineteen_rl_' . md5($action . '|' . $ip);
    $now = time();
    $hits = [];
    if (is_file($key)) {
        $hits = array_filter((array) json_decode((string) file_get_contents($key), true),
            static fn($t) => $t > $now - 60);
    }
    if (count($hits) >= $maxPerMinute) {
        return false;
    }
    $hits[] = $now;
    file_put_contents($key, json_encode(array_values($hits)), LOCK_EX);
    return true;
}
