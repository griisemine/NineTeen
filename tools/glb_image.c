/* Voir glb_image.h pour ce que fait ce module et pourquoi il est à part. */
#include "glb_image.h"

#include "tool_json.h"
#include "tools_common.h"

/* Les quatre codes de la spécification binaire, en petit-boutiste. */
#define GLB_MAGIC 0x46546C67u   /* « glTF » */
#define GLB_JSON  0x4E4F534Au   /* « JSON » */
#define GLB_BIN   0x004E4942u   /* « BIN\0 » */

/* Un remplacement de texte dans le JSON : « de `debut` à `fin`, mettre `quoi` ».
 * On les collecte, on les trie, puis on recoud d'un seul passage. Recoudre au
 * fur et à mesure décalerait les positions suivantes, ce qui est exactement le
 * genre de faute qu'on ne voit qu'une fois le fichier illisible. */
typedef struct couture {
    int  debut, fin;
    char quoi[64];
} couture;

static void coudre(couture *c, int *n, int debut, int fin, const char *quoi)
{
    if (*n >= 4) tool_fatalf("trop de coutures dans le JSON du GLB");
    c[*n].debut = debut;
    c[*n].fin = fin;
    snprintf(c[*n].quoi, sizeof c[*n].quoi, "%s", quoi);
    ++*n;
}

static int couture_cmp(const void *a, const void *b)
{
    const couture *x = (const couture *)a, *y = (const couture *)b;
    return (x->debut > y->debut) - (x->debut < y->debut);
}

void glb_image_replace(const char *entree, const char *sortie,
                       const void *image, size_t taille, const char *mime)
{
    if (!entree || !sortie || !image || !taille || !mime) tool_fatalf("glb_image_replace : appel vide");

    /* ---------------------------------------------------------- la lecture */
    FILE *f = fopen(entree, "rb");
    if (!f) tool_fatalf("« %s » : illisible", entree);
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 20) tool_fatalf("« %s » : trop court pour un GLB", entree);
    unsigned char *bytes = (unsigned char *)malloc((size_t)n);
    if (!bytes) tool_fatalf("mémoire épuisée (%ld octets)", n);
    if (fread(bytes, 1, (size_t)n, f) != (size_t)n) tool_fatalf("« %s » : lecture incomplète", entree);
    fclose(f);

    uint32_t magic = 0, version = 0;
    memcpy(&magic, bytes, 4);
    memcpy(&version, bytes + 4, 4);
    if (magic != GLB_MAGIC) tool_fatalf("« %s » : ce n'est pas un GLB", entree);
    if (version != 2u) tool_fatalf("« %s » : GLB version %u, seule la 2 est lue", entree, version);

    const char *json = NULL; size_t json_len = 0;
    const unsigned char *bin = NULL; size_t bin_len = 0;
    for (size_t o = 12; o + 8 <= (size_t)n; ) {
        uint32_t clen = 0, ctype = 0;
        memcpy(&clen, bytes + o, 4);
        memcpy(&ctype, bytes + o + 4, 4);
        if (o + 8 + (size_t)clen > (size_t)n) tool_fatalf("« %s » : morceau tronqué", entree);
        if (ctype == GLB_JSON) { json = (const char *)(bytes + o + 8); json_len = clen; }
        else if (ctype == GLB_BIN) { bin = bytes + o + 8; bin_len = clen; }
        o += 8u + clen;
    }
    if (!json || !bin) tool_fatalf("« %s » : morceau JSON ou binaire absent", entree);

    /* ------------------------------------------------------- ce qu'on vise */
    tool_json doc;
    tool_json_parse(&doc, json, json_len, "le JSON du GLB");
    const tool_json_value *racine = tool_json_root(&doc);
    const tool_json_value *images = tool_json_get(&doc, racine, "images");
    const tool_json_value *bvs = tool_json_get(&doc, racine, "bufferViews");
    const tool_json_value *bufs = tool_json_get(&doc, racine, "buffers");
    if (!images || !bvs || !bufs) tool_fatalf("« %s » : ni images, ni vues de tampon, ni tampons", entree);

    const tool_json_value *img = tool_json_at(&doc, images, 0);
    if (!img) tool_fatalf("« %s » : aucune image embarquée à remplacer", entree);
    const tool_json_value *img_bv = tool_json_get(&doc, img, "bufferView");
    if (!img_bv) {
        tool_fatalf("« %s » : l'image est un fichier externe (« uri ») et non un morceau "
                    "du bloc binaire — remplacement refusé", entree);
    }
    const int k = (int)tool_json_value_float(&doc, img_bv, -1.0f);
    const tool_json_value *vue = tool_json_at(&doc, bvs, k);
    if (!vue) tool_fatalf("« %s » : vue de tampon %d introuvable", entree, k);

    const size_t image_off = (size_t)tool_json_get_float(&doc, vue, "byteOffset", 0.0f);
    const size_t image_len = (size_t)tool_json_get_float(&doc, vue, "byteLength", 0.0f);

    /*
     * LA VÉRIFICATION QUI PORTE TOUT LE RESTE : l'image est-elle bien le
     * dernier morceau du bloc binaire ? Si elle ne l'est pas, changer sa
     * longueur décale les accesseurs qui la suivent, et le modèle sortirait
     * avec des sommets lus dans du JPEG. On refuse plutôt que de produire ça.
     */
    const int nbv = tool_json_array_count(&doc, bvs);
    for (int i = 0; i < nbv; ++i) {
        if (i == k) continue;
        const tool_json_value *o = tool_json_at(&doc, bvs, i);
        const size_t fin = (size_t)tool_json_get_float(&doc, o, "byteOffset", 0.0f)
                         + (size_t)tool_json_get_float(&doc, o, "byteLength", 0.0f);
        if (fin > image_off + image_len) {
            tool_fatalf("« %s » : l'image n'est pas le dernier morceau du bloc binaire "
                        "(la vue %d finit plus loin) — remplacement refusé", entree, i);
        }
    }
    if (image_off > bin_len) tool_fatalf("« %s » : vue d'image hors du bloc binaire", entree);

    /* -------------------------------------------------------- les coutures */
    couture c[4];
    int nc = 0;
    char nombre[32];

    const tool_json_value *v_bvlen = tool_json_get(&doc, vue, "byteLength");
    if (!v_bvlen) tool_fatalf("« %s » : la vue de l'image n'a pas de byteLength", entree);
    snprintf(nombre, sizeof nombre, "%zu", taille);
    coudre(c, &nc, v_bvlen->tok.start, v_bvlen->tok.end, nombre);

    const tool_json_value *buf = tool_json_at(&doc, bufs, 0);
    const tool_json_value *v_buflen = buf ? tool_json_get(&doc, buf, "byteLength") : NULL;
    if (!v_buflen) tool_fatalf("« %s » : le tampon 0 n'a pas de byteLength", entree);
    const size_t nouveau_bin = image_off + taille;
    snprintf(nombre, sizeof nombre, "%zu", nouveau_bin);
    coudre(c, &nc, v_buflen->tok.start, v_buflen->tok.end, nombre);

    /* Le type déclaré doit suivre le contenu : un JPEG annoncé PNG est un
     * personnage blanc, et rien ne le dirait avant l'écran. */
    const tool_json_value *v_mime = tool_json_get(&doc, img, "mimeType");
    if (!v_mime) {
        tool_fatalf("« %s » : l'image ne déclare pas de « mimeType » — impossible de "
                    "garantir que le moteur lira le bon format", entree);
    }
    coudre(c, &nc, v_mime->tok.start, v_mime->tok.end, mime);

    qsort(c, (size_t)nc, sizeof c[0], couture_cmp);

    size_t cap = json_len + 256;
    char *neuf = (char *)malloc(cap);
    if (!neuf) tool_fatalf("mémoire épuisée (JSON recousu)");
    size_t w = 0;
    int lu = 0;
    for (int i = 0; i < nc; ++i) {
        const size_t bloc = (size_t)(c[i].debut - lu);
        memcpy(neuf + w, json + lu, bloc); w += bloc;
        const size_t l = strlen(c[i].quoi);
        memcpy(neuf + w, c[i].quoi, l); w += l;
        lu = c[i].fin;
    }
    memcpy(neuf + w, json + lu, json_len - (size_t)lu); w += json_len - (size_t)lu;
    tool_json_free(&doc);

    /* Les deux morceaux sont alignés sur quatre octets — le JSON complété par
     * des ESPACES, le binaire par des zéros. La spécification l'impose, et
     * l'oublier donne un fichier que la moitié des lecteurs refusent. */
    while (w % 4u) neuf[w++] = ' ';
    const size_t bin_pad = (4u - (nouveau_bin % 4u)) % 4u;
    const size_t total = 12u + 8u + w + 8u + nouveau_bin + bin_pad;

    /* ---------------------------------------------------------- l'écriture */
    FILE *g = fopen(sortie, "wb");
    if (!g) tool_fatalf("« %s » : impossible d'écrire", sortie);
    const uint32_t out_magic = GLB_MAGIC, out_ver = 2u, out_len = (uint32_t)total;
    const uint32_t lj = (uint32_t)w, tj = GLB_JSON;
    const uint32_t lb = (uint32_t)(nouveau_bin + bin_pad), tb = GLB_BIN;
    fwrite(&out_magic, 4, 1, g); fwrite(&out_ver, 4, 1, g); fwrite(&out_len, 4, 1, g);
    fwrite(&lj, 4, 1, g); fwrite(&tj, 4, 1, g); fwrite(neuf, 1, w, g);
    fwrite(&lb, 4, 1, g); fwrite(&tb, 4, 1, g);
    fwrite(bin, 1, image_off, g);
    fwrite(image, 1, taille, g);
    for (size_t i = 0; i < bin_pad; ++i) fputc(0, g);
    const bool rate = ferror(g) != 0;
    fclose(g);
    if (rate) tool_fatalf("« %s » : écriture incomplète", sortie);

    free(neuf);
    free(bytes);
}
