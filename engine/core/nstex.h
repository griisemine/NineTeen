/*
 * nstex — le conteneur de textures compressées par blocs.
 *
 * Pourquoi PAS KTX2
 * -----------------
 * KTX2 est le bon format pour échanger des textures entre outils. Ici rien
 * n'est échangé : `texgen` écrit, le moteur lit, les deux sont dans ce dépôt et
 * personne d'autre n'ouvrira ces fichiers. KTX2 apporterait en échange un
 * en-tête à niveaux (DFD, KVD, supercompression) dont on n'utiliserait aucun
 * champ, et une bibliothèque à vendorer pour le lire.
 *
 * Le format ci-dessous tient en vingt octets d'en-tête et se lit sans
 * dépendance. Il s'appelle `nstex` et pas `ktx2` : prétendre écrire du KTX2
 * sans en écrire serait pire que d'assumer un format maison.
 *
 * Ce qu'il contient
 * -----------------
 *   octets 0..3    « NSTX »
 *   octet  4       version (1)
 *   octet  5       format (voir `nstex_format`)
 *   octets 6..7    nombre de niveaux de mip
 *   octets 8..11   largeur du niveau 0
 *   octets 12..15  hauteur du niveau 0
 *   octets 16..19  taille totale des données, en octets
 *   puis           les niveaux, du plus grand au plus petit, bout à bout
 *
 * Les mips sont DANS LE FICHIER, et c'est une contrainte du format bloc, pas un
 * choix : le moteur génère ses mips sur GPU par blit, ce qu'aucune API ne sait
 * faire sur une texture compressée. Les calculer à la compilation est de toute
 * façon meilleur — on filtre l'image d'origine, pas un niveau déjà compressé.
 *
 * Où il vit, et pourquoi
 * -----------------------
 * Dans `engine/core/` et pas dans `tools/` ni dans `engine/rhi/` : c'est le
 * SEUL répertoire que les deux côtés incluent déjà. `texgen` écrit ce format,
 * `ns_rhi` le lit, et un format décrit à deux endroits finit par diverger d'un
 * champ — c'est exactement ce qui est arrivé au format canonique du journal de
 * partie, écrit en C et en Go, et qu'il a fallu tenir par un test.
 *
 * Tout est en petit-boutiste. Les trois plateformes visées le sont ; si une
 * quatrième ne l'était pas, ce commentaire est l'endroit où le découvrir.
 */
#ifndef NSTEX_H
#define NSTEX_H

#include <stdint.h>

#define NSTEX_MAGIC0 'N'
#define NSTEX_MAGIC1 'S'
#define NSTEX_MAGIC2 'T'
#define NSTEX_MAGIC3 'X'
#define NSTEX_VERSION 1u
#define NSTEX_HEADER_BYTES 20u

typedef enum nstex_format {
    /*
     * BC1 : RGB sur 4 bits par texel. Pour l'ORM, dont les trois canaux sont
     * des scalaires lents (occlusion, rugosité, métallicité) : les artefacts
     * de BC1 sur des dégradés doux sont sous le seuil de visibilité, et
     * l'éclairage les intègre encore.
     */
    NSTEX_FORMAT_BC1 = 1,
    /*
     * BC5 : deux canaux sur 8 bits par texel, chacun compressé comme un BC4.
     * C'est LE format d'une carte de normales, et pas un pis-aller : BC1 sur
     * une normale donne un banding vert bien connu, parce qu'il code le vert
     * sur 6 bits et interpole en RGB. BC5 ne garde que X et Y ; Z se
     * reconstruit dans le shader par sqrt(1 - x² - y²), ce qui est exact pour
     * une normale unitaire de l'hémisphère tangent.
     */
    NSTEX_FORMAT_BC5 = 2,
} nstex_format;

/* Octets par bloc de 4x4. */
static inline uint32_t nstex_block_bytes(uint32_t format)
{
    return (format == NSTEX_FORMAT_BC5) ? 16u : 8u;
}

/* Taille d'un niveau. Un bloc couvre 4x4 texels et un niveau plus petit que le
 * bloc en occupe quand même un entier — c'est la règle des formats bloc, et
 * l'oublier donne un téléversement trop court aux derniers niveaux. */
static inline uint32_t nstex_level_bytes(uint32_t w, uint32_t h, uint32_t format)
{
    const uint32_t bw = (w + 3u) / 4u, bh = (h + 3u) / 4u;
    return bw * bh * nstex_block_bytes(format);
}

#endif /* NSTEX_H */
