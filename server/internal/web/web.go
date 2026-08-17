// Package web — le site, embarqué dans le binaire.
package web

import "embed"

//go:embed all:assets
var FS embed.FS
