#version 450
/*
 * blit.frag — recopie une texture à l'écran.
 *
 * Sert de dernière étape de la chaîne de rendu (la cible HDR tonemappée arrive
 * sur la swapchain) et de brique de débogage pour visualiser n'importe quelle
 * cible intermédiaire du G-buffer.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_source;

void main()
{
    o_color = texture(u_source, v_uv);
}
