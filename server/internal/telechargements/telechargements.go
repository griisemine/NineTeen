// Package telechargements sert les paquets du jeu depuis un repertoire.
//
// POURQUOI CE PAQUET EXISTE
// -------------------------
// La page de telechargement batissait trois liens vers
// `github.com/griisemine/NineTeen/releases/download/v<version>/...` et les
// eteignait tant que `NINETEEN_RELEASE_PUBLIEE` n'etait pas posee. Consequence
// mesuree : qui monte la pile avec `docker compose up --build` obtient un site
// complet et zero bouton de telechargement, parce qu'aucune release GitHub
// n'existe pour la version qu'il vient de construire. Le projet se lancait, et
// ne se distribuait pas.
//
// Ici, la verite est un REPERTOIRE. Ce qui s'y trouve est offert, ce qui ne s'y
// trouve pas n'est pas annonce. Aucune variable ne peut mentir a ce sujet,
// parce qu'il n'y a plus de variable : il y a `os.ReadDir`.
//
// CE QUE CE PAQUET NE FAIT PAS
// ----------------------------
// Il ne fabrique rien. Le remplissage du repertoire est le travail de
// `packaging/linux/paquets.sh` et de `cpack`, appeles par la composition
// Docker ou par l'integration continue. Un serveur qui compilerait le jeu pour
// repondre a une requete HTTP serait une machine a deni de service.
package telechargements

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"io"
	"log/slog"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

// Fichier decrit un paquet offert au telechargement.
//
// `SHA256` peut etre vide, et c'est un etat normal : la somme d'un fichier de
// 175 Mio prend un instant, et le repertoire se remplit pendant que le serveur
// tourne (la composition Docker fabrique les paquets en parallele). On annonce
// donc le fichier des qu'il est la, et sa somme des qu'elle est calculee,
// plutot que de retenir l'un en attendant l'autre.
type Fichier struct {
	Nom        string `json:"nom"`
	URL        string `json:"url"`
	Plateforme string `json:"plateforme"`
	Arch       string `json:"arch"`
	Format     string `json:"format"`
	Octets     int64  `json:"octets"`
	SHA256     string `json:"sha256"`
}

// Annexe decrit un fichier qui accompagne les paquets sans en etre un :
// manifeste de signature, cle publique, signature detachee.
type Annexe struct {
	Nom    string `json:"nom"`
	URL    string `json:"url"`
	Octets int64  `json:"octets"`
}

// Prefixe est la racine HTTP sous laquelle les fichiers sont servis.
const Prefixe = "/telechargements/"

// Depot est un repertoire de paquets, relu a chaque demande.
//
// Relu, et non mis en cache : le repertoire change sous le serveur, sans
// redemarrage, quand le service de fabrication finit son travail. Le cout d'un
// `os.ReadDir` sur une dizaine d'entrees est sans commune mesure avec le
// travail qu'il economise de devoir invalider un cache correctement.
//
// Les SOMMES, elles, sont gardees : ce sont elles qui coutent.
type Depot struct {
	racine string

	mu     sync.Mutex
	sommes map[cle]string
}

// cle identifie un contenu sans le lire. Le nom seul ne suffit pas : un paquet
// refabrique garde son nom et change de contenu, et servir alors l'ancienne
// somme serait pire que n'en servir aucune.
type cle struct {
	nom     string
	octets  int64
	modifie int64
}

// Ouvrir prend un repertoire. Une racine vide donne un depot toujours vide,
// ce qui est le cas d'un serveur qui n'heberge pas les paquets.
func Ouvrir(racine string) *Depot {
	return &Depot{racine: racine, sommes: map[cle]string{}}
}

// Actif dit si un repertoire a ete configure.
func (d *Depot) Actif() bool { return d != nil && d.racine != "" }

// Liste rend les paquets et leurs annexes, tries.
//
// L'ordre est stable et voulu : Windows, macOS, Linux, puis par nom. Une page
// dont les trois boutons changent de place d'un chargement a l'autre est une
// page qu'on ne peut pas apprendre.
func (d *Depot) Liste() ([]Fichier, []Annexe) {
	if !d.Actif() {
		return nil, nil
	}
	entrees, err := os.ReadDir(d.racine)
	if err != nil {
		// Repertoire absent ou illisible : un depot vide, pas une panne. La
		// composition Docker le cree avant meme que le service de fabrication
		// ait fini, et le site doit rester servi entre les deux.
		return nil, nil
	}

	var paquets []Fichier
	var annexes []Annexe

	for _, e := range entrees {
		if e.IsDir() {
			continue
		}
		info, err := e.Info()
		if err != nil {
			continue
		}
		nom := e.Name()

		if estAnnexe(nom) {
			annexes = append(annexes, Annexe{
				Nom:    nom,
				URL:    Prefixe + nom,
				Octets: info.Size(),
			})
			continue
		}

		plateforme, arch, format := Classer(nom)
		if plateforme == "" {
			continue
		}
		paquets = append(paquets, Fichier{
			Nom:        nom,
			URL:        Prefixe + nom,
			Plateforme: plateforme,
			Arch:       arch,
			Format:     format,
			Octets:     info.Size(),
			SHA256:     d.somme(nom, info.Size(), info.ModTime()),
		})
	}

	rang := map[string]int{"windows": 0, "macos": 1, "linux": 2}
	sort.Slice(paquets, func(i, j int) bool {
		ri, rj := rang[paquets[i].Plateforme], rang[paquets[j].Plateforme]
		if ri != rj {
			return ri < rj
		}
		return paquets[i].Nom < paquets[j].Nom
	})
	sort.Slice(annexes, func(i, j int) bool { return annexes[i].Nom < annexes[j].Nom })

	return paquets, annexes
}

// Chemin resout un nom de fichier demande par HTTP.
//
// LE NOM VIENT DU RESEAU. Il est donc refuse s'il porte un separateur, un
// point d'echappement, ou s'il ne designe pas un fichier ordinaire du depot.
// `filepath.Join` seul ne suffirait pas : « ../../etc/passwd » se joint tres
// bien. On verifie le nom AVANT de le joindre, et on verifie encore ce qu'on
// obtient.
func (d *Depot) Chemin(nom string) (string, bool) {
	if !d.Actif() || !nomSain(nom) {
		return "", false
	}
	chemin := filepath.Join(d.racine, nom)
	info, err := os.Stat(chemin)
	if err != nil || !info.Mode().IsRegular() {
		return "", false
	}
	return chemin, true
}

// nomSain refuse tout ce qui n'est pas un nom de fichier simple.
func nomSain(nom string) bool {
	if nom == "" || len(nom) > 128 {
		return false
	}
	if nom == "." || nom == ".." || strings.HasPrefix(nom, ".") {
		return false
	}
	if strings.ContainsAny(nom, `/\`) || strings.ContainsRune(nom, 0) {
		return false
	}
	for _, r := range nom {
		ok := r == '.' || r == '-' || r == '_' || r == '+' ||
			(r >= '0' && r <= '9') || (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z')
		if !ok {
			return false
		}
	}
	return true
}

// Classer deduit la plateforme, l'architecture et le format d'un nom de
// paquet. Une plateforme vide veut dire « ce fichier n'est pas un paquet ».
//
// LE NOM EST LA SEULE SOURCE, et c'est assume : ces noms sont produits par
// `packaging/` et par CPack, dans ce depot, et le test les compare a ce que
// `packaging/` fabrique reellement. Ouvrir chaque fichier pour deviner son type
// couterait une lecture par entree a chaque affichage de la page, pour une
// information que le nom porte deja.
func Classer(nom string) (plateforme, arch, format string) {
	bas := strings.ToLower(nom)

	switch {
	case strings.HasSuffix(bas, ".exe"):
		plateforme, format = "windows", "installateur .exe"
	case strings.HasSuffix(bas, ".msi"):
		plateforme, format = "windows", "installateur .msi"
	case strings.HasSuffix(bas, ".zip"):
		plateforme, format = "windows", "archive .zip"
	case strings.HasSuffix(bas, ".dmg"):
		plateforme, format = "macos", "image disque .dmg"
	case strings.HasSuffix(bas, ".pkg"):
		plateforme, format = "macos", "installateur .pkg"
	case strings.HasSuffix(bas, ".appimage"):
		plateforme, format = "linux", "AppImage"
	case strings.HasSuffix(bas, ".deb"):
		plateforme, format = "linux", "paquet .deb"
	case strings.HasSuffix(bas, ".rpm"):
		plateforme, format = "linux", "paquet .rpm"
	case strings.HasSuffix(bas, ".tar.gz"):
		plateforme, format = "linux", "archive .tar.gz"
	default:
		return "", "", ""
	}

	switch {
	case strings.Contains(bas, "universal") || strings.Contains(bas, "universel"):
		arch = "universel"
	case strings.Contains(bas, "aarch64") || strings.Contains(bas, "arm64"):
		arch = "arm64"
	case strings.Contains(bas, "x86_64") || strings.Contains(bas, "amd64") ||
		strings.Contains(bas, "x64"):
		arch = "x86_64"
	}
	return plateforme, arch, format
}

// estAnnexe reconnait ce qui accompagne un paquet sans en etre un.
func estAnnexe(nom string) bool {
	bas := strings.ToLower(nom)
	return strings.HasPrefix(bas, "signature-") ||
		strings.HasSuffix(bas, ".asc") ||
		strings.HasSuffix(bas, ".sig") ||
		bas == "sha256sums.txt"
}

// somme rend la somme connue, ou la chaine vide.
func (d *Depot) somme(nom string, octets int64, modifie time.Time) string {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.sommes[cle{nom, octets, modifie.UnixNano()}]
}

// Chauffer calcule en fond les sommes SHA-256 qui manquent.
//
// EN FOND, ET PAS A LA DEMANDE, pour une raison mesuree : le .deb de la 17.0.0
// pese 167,4 Mio, et le hacher prend de l'ordre d'une demi-seconde. Le faire
// dans le gestionnaire HTTP ferait payer cette demi-seconde par plateforme et
// par chargement de page, a tous les visiteurs, pour une valeur qui ne change
// qu'a la fabrication.
//
// La boucle ne s'arrete pas apres le premier passage : le repertoire se
// remplit APRES le demarrage quand la composition Docker fabrique les paquets
// en parallele du serveur. Elle repasse donc, et ne fait rien tant que rien n'a
// change.
func (d *Depot) Chauffer(ctx context.Context, log *slog.Logger, periode time.Duration) {
	if !d.Actif() {
		return
	}
	for {
		d.chauffeUnTour(log)
		select {
		case <-ctx.Done():
			return
		case <-time.After(periode):
		}
	}
}

func (d *Depot) chauffeUnTour(log *slog.Logger) {
	paquets, _ := d.Liste()
	for _, f := range paquets {
		if f.SHA256 != "" {
			continue
		}
		chemin, ok := d.Chemin(f.Nom)
		if !ok {
			continue
		}
		info, err := os.Stat(chemin)
		if err != nil {
			continue
		}
		somme, err := hacher(chemin)
		if err != nil {
			if log != nil {
				log.Warn("somme SHA-256 illisible", "fichier", f.Nom, "err", err)
			}
			continue
		}
		// La taille et la date sont RELUES apres le hachage, et la somme est
		// rangee sous celles-la. Un fichier encore en cours d'ecriture par le
		// service de fabrication changerait entre les deux, et la somme serait
		// alors rangee sous une cle que personne ne redemandera, au lieu d'etre
		// servie pour un contenu qu'elle ne decrit plus.
		d.mu.Lock()
		d.sommes[cle{f.Nom, info.Size(), info.ModTime().UnixNano()}] = somme
		d.mu.Unlock()
	}
}

func hacher(chemin string) (string, error) {
	f, err := os.Open(chemin)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

// Depot officiel du projet, la ou l'integration continue publie ses paquets.
const depotGitHub = "https://github.com/griisemine/NineTeen/releases/download/v"

// SurGitHub rend les paquets d'une release publiee, quand ce serveur n'en
// heberge aucun.
//
// CES NOMS SONT CEUX QUE `packaging/` PRODUIT, PAS CEUX QU'ON AIMERAIT. Les
// precedents etaient faux tous les trois, et rien ne pouvait le voir : la garde
// « la release est-elle publiee » verifie que la release existe, pas que le
// fichier demande s'y trouve. Le jour de la publication, les trois boutons
// auraient rendu 404 une seconde fois.
//
//	.zip                -> l'empaquetage Windows est un installateur NSIS
//	                       depuis la 17.0.0, donc un .exe
//	-macos-             -> CPack ecrit « macOS », avec la capitale
//	-linux-x64.AppImage -> `paquets.sh` nomme l'AppImage d'apres `uname -m`,
//	                       comme le veut la convention AppImage
//
// `TestLesNomsDeReleaseSuiventPackaging` les compare a `packaging/`, pour que
// la prochaine divergence se voie ici et non sur la page.
func SurGitHub(version string) []Fichier {
	noms := []string{
		"Nineteen-" + version + "-windows-x64.exe",
		"Nineteen-" + version + "-macOS-universal.dmg",
		"Nineteen-" + version + "-x86_64.AppImage",
	}
	out := make([]Fichier, 0, len(noms))
	for _, nom := range noms {
		plateforme, arch, format := Classer(nom)
		out = append(out, Fichier{
			Nom:        nom,
			URL:        depotGitHub + version + "/" + nom,
			Plateforme: plateforme,
			Arch:       arch,
			Format:     format,
		})
	}
	return out
}
