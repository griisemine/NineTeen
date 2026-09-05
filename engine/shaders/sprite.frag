#version 450
/*
 * sprite.frag — un texel, une couleur, un mélange.
 *
 * C'est le premier étage du moteur à employer le MÉLANGE ALPHA : tout le reste
 * est opaque, et la seule transparence existante était un `discard` sous 0,35
 * dans le G-buffer. Le mélange est déclaré côté pipeline, pas ici ; ce shader ne
 * fait que produire la couleur prémultipliée qu'on lui demande.
 */

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_atlas;

void main()
{
    vec4 t = texture(u_atlas, v_uv);
    o_color = t * v_color;
    /* Un fragment totalement transparent ne doit pas écrire : sans ce rejet, les
     * bords des glyphes déposent du noir sur le fond au lieu de le laisser voir. */
    if (o_color.a <= 0.002) discard;
}
