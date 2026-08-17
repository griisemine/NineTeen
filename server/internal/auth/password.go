// Package auth — mots de passe, sessions et jetons.
//
// Réponse directe à trois constats de l'audit du code d'origine :
//
//	NIN-06  les mots de passe étaient hachés en MD5 non salé, avec une politique
//	        « au moins 4 caractères, alphanumériques uniquement » — ce qui
//	        interdisait justement les mots de passe forts.
//	NIN-09  les clés de session venaient de rand(), jamais semé.
//	NIN-02  un « jeton sécurisé » était un MD5 d'horodatage que le client
//	        pouvait recalculer, le secret étant dans le binaire distribué.
package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"errors"
	"fmt"
	"strings"
	"unicode"
	"unicode/utf8"

	"golang.org/x/crypto/argon2"
)

// Paramètres Argon2id.
//
// Calibrés pour environ 50 ms sur un cœur de serveur modeste : assez lent pour
// qu'une attaque par dictionnaire coûte cher, assez rapide pour ne pas devenir
// un levier de déni de service sur la page de connexion. Ils sont stockés dans
// le hachage lui-même, donc on peut les durcir plus tard sans invalider les
// comptes existants.
const (
	argonTime    = 2
	argonMemory  = 64 * 1024 // 64 Mio
	argonThreads = 2
	argonKeyLen  = 32
	saltLen      = 16
)

var (
	ErrInvalidHash  = errors.New("format de hachage non reconnu")
	ErrWeakPassword = errors.New("mot de passe trop faible")
)

// HashPassword produit un hachage Argon2id encodé, sel compris.
func HashPassword(password string) (string, error) {
	if err := ValidatePassword(password); err != nil {
		return "", err
	}

	salt := make([]byte, saltLen)
	if _, err := rand.Read(salt); err != nil {
		return "", fmt.Errorf("génération du sel : %w", err)
	}

	key := argon2.IDKey([]byte(password), salt, argonTime, argonMemory, argonThreads, argonKeyLen)

	return fmt.Sprintf("$argon2id$v=%d$m=%d,t=%d,p=%d$%s$%s",
		argon2.Version, argonMemory, argonTime, argonThreads,
		base64.RawStdEncoding.EncodeToString(salt),
		base64.RawStdEncoding.EncodeToString(key)), nil
}

// VerifyPassword compare un mot de passe à un hachage encodé.
//
// La comparaison est à temps constant : un test avec `==` fuiterait le nombre
// d'octets corrects par son temps d'exécution.
func VerifyPassword(password, encoded string) (bool, error) {
	parts := strings.Split(encoded, "$")
	if len(parts) != 6 || parts[1] != "argon2id" {
		return false, ErrInvalidHash
	}

	var version int
	if _, err := fmt.Sscanf(parts[2], "v=%d", &version); err != nil {
		return false, ErrInvalidHash
	}
	if version != argon2.Version {
		return false, ErrInvalidHash
	}

	var memory uint32
	var time uint32
	var threads uint8
	if _, err := fmt.Sscanf(parts[3], "m=%d,t=%d,p=%d", &memory, &time, &threads); err != nil {
		return false, ErrInvalidHash
	}

	salt, err := base64.RawStdEncoding.Strict().DecodeString(parts[4])
	if err != nil {
		return false, ErrInvalidHash
	}
	want, err := base64.RawStdEncoding.Strict().DecodeString(parts[5])
	if err != nil {
		return false, ErrInvalidHash
	}

	got := argon2.IDKey([]byte(password), salt, time, memory, threads, uint32(len(want)))
	return subtle.ConstantTimeCompare(got, want) == 1, nil
}

// ValidatePassword applique la politique de mots de passe.
//
// À l'opposé de l'original, qui exigeait des caractères alphanumériques et
// acceptait quatre caractères : la longueur est ce qui protège réellement, et
// restreindre le jeu de caractères ne fait que réduire l'espace de recherche.
func ValidatePassword(password string) error {
	const minLen, maxLen = 12, 256

	n := utf8.RuneCountInString(password)
	if n < minLen {
		return fmt.Errorf("%w : %d caractères, minimum %d", ErrWeakPassword, n, minLen)
	}
	// La borne haute n'est pas une contrainte de sécurité mais une protection
	// contre un déni de service : Argon2 sur une entrée de 10 Mio coûte cher.
	if n > maxLen {
		return fmt.Errorf("%w : plus de %d caractères", ErrWeakPassword, maxLen)
	}

	// Refuser une chaîne d'un seul caractère répété, et les mots de passe les
	// plus courants. Le reste est autorisé, accents et espaces compris.
	distinct := map[rune]struct{}{}
	for _, r := range password {
		distinct[r] = struct{}{}
	}
	if len(distinct) < 5 {
		return fmt.Errorf("%w : trop peu de caractères distincts", ErrWeakPassword)
	}

	lower := strings.ToLower(password)
	for _, bad := range commonPasswords {
		if lower == bad {
			return fmt.Errorf("%w : mot de passe trop répandu", ErrWeakPassword)
		}
	}
	return nil
}

var commonPasswords = []string{
	"password", "motdepasse", "azertyuiop", "qwertyuiop", "123456789012",
	"motdepasse123", "password1234", "administrateur", "nineteen1234",
}

// ValidateUsername vérifie le nom de compte.
//
// L'original insérait directement l'entrée dans la requête SQL ; ici les
// requêtes sont préparées, mais on garde une validation stricte pour que les
// noms restent affichables et non ambigus.
func ValidateUsername(name string) error {
	n := utf8.RuneCountInString(name)
	if n < 3 || n > 24 {
		return errors.New("le nom doit faire entre 3 et 24 caractères")
	}
	for _, r := range name {
		if !unicode.IsLetter(r) && !unicode.IsDigit(r) && r != '_' && r != '-' {
			return errors.New("le nom n'accepte que lettres, chiffres, tiret et souligné")
		}
	}
	return nil
}

// NewSessionKey produit une clé de session opaque de 256 bits.
//
// Remplace le rand() non semé de l'original. La valeur renvoyée est celle que
// reçoit le client ; c'est son empreinte qui est stockée en base, de sorte
// qu'une fuite de la table des sessions ne permette pas de se faire passer pour
// un joueur.
func NewSessionKey() (clear string, stored string, err error) {
	raw := make([]byte, 32)
	if _, err := rand.Read(raw); err != nil {
		return "", "", fmt.Errorf("génération de la clé de session : %w", err)
	}
	clear = base64.RawURLEncoding.EncodeToString(raw)
	return clear, HashSessionKey(clear), nil
}

// HashSessionKey calcule l'empreinte stockée d'une clé de session.
//
// SHA-256 sans sel est ici le bon choix, contrairement aux mots de passe : la
// clé est déjà 256 bits d'aléa cryptographique, elle n'est pas devinable, et le
// hachage doit rester rapide puisqu'il est calculé à chaque requête authentifiée.
func HashSessionKey(clear string) string {
	sum := sha256.Sum256([]byte(clear))
	return base64.RawStdEncoding.EncodeToString(sum[:])
}
