// Package migrations — le schéma SQL, embarqué dans le binaire.
//
// Embarquer plutôt que livrer des fichiers à côté : il n'y a rien à copier sur
// le serveur, et une version du binaire correspond toujours exactement au
// schéma qu'elle attend.
package migrations

import "embed"

//go:embed *.sql
var FS embed.FS
