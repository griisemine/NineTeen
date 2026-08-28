#version 450
/*
 * bloom_threshold.frag — isole ce qui dépasse le seuil de floraison.
 *
 * Ce sont les néons, les enseignes et les écrans de bornes : la salle tire son
 * atmosphère de ces halos. Le seuil est appliqué en douceur (« soft knee »)
 * plutôt que net, sinon une surface qui franchit le seuil en se déplaçant fait
 * apparaître le halo d'un coup, ce qui se voit.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_hdr;

layout(set = 3, binding = 0) uniform Params {
    vec4 u_settings;   /* x : seuil, y : douceur du coude, z : intensité, w : inutilisé */
};

void main()
{
    vec3 c = texture(u_hdr, v_uv).rgb;

    /*
     * Le halo est le SEUL endroit du moteur où un pixel contamine ses voisins,
     * et il le fait cinq fois de suite : un non-fini isolé dans l'image
     * éclairée y devient, après les cinq niveaux de flou séparable, un
     * rectangle de plusieurs centaines de pixels que le tone mapping traduit en
     * noir. La cause tenait dans une normale dégénérée du G-buffer et elle est
     * corrigée là-bas ; ce garde-fou-ci est la deuxième ligne, parce qu'un seul
     * pixel ne doit jamais pouvoir noircir un quart de l'écran, quelle que
     * soit la passe amont qui l'a produit.
     *
     * On ÉCARTE le pixel (contribution nulle) plutôt que de le noircir en
     * sortie : un trou d'un pixel dans le halo ne se voit pas.
     */
    if (isnan(c.r) || isnan(c.g) || isnan(c.b) ||
        isinf(c.r) || isinf(c.g) || isinf(c.b)) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    /* Luminance perceptuelle plutôt que le maximum des canaux : un bleu très
     * saturé ne doit pas fleurir autant qu'un blanc de même intensité. */
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));

    float threshold = u_settings.x;
    float knee = max(u_settings.y, 1e-4);

    /* Courbe quadratique dans la zone du coude, linéaire au-delà. */
    float soft = clamp(luma - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(soft, luma - threshold) / max(luma, 1e-4);

    o_color = vec4(c * contribution * u_settings.z, 1.0);
}
