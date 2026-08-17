/*
 * ns_math.h — algèbre linéaire du moteur.
 *
 * Tout en `static inline` : pas d'appel de fonction sur le chemin chaud, et le
 * compilateur vectorise. Convention retenue, alignée sur Vulkan/Metal/D3D12 :
 *   - matrices **colonne-majeure**, vecteurs colonnes (v' = M * v) ;
 *   - repère main droite, Y vers le haut ;
 *   - profondeur de projection dans [0,1] (et non [-1,1] comme l'OpenGL de la V1),
 *     ce qui est ce qu'attendent les API modernes et améliore la précision du
 *     depth buffer.
 */
#ifndef NS_MATH_H
#define NS_MATH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define NS_PI      3.14159265358979323846f
#define NS_TAU     6.28318530717958647692f
#define NS_DEG2RAD (NS_PI / 180.0f)
#define NS_RAD2DEG (180.0f / NS_PI)
#define NS_EPS     1e-6f

typedef struct { float x, y; }       ns_v2;
typedef struct { float x, y, z; }    ns_v3;
typedef struct { float x, y, z, w; } ns_v4;

/* Colonne-majeure : m[colonne][ligne], comme GLSL. */
typedef struct { float m[4][4]; } ns_m4;

typedef struct { float x, y, z, w; } ns_quat;

typedef struct { ns_v3 min, max; } ns_aabb;

/* ---------------------------------------------------------------- scalaires */

static inline float ns_minf(float a, float b) { return a < b ? a : b; }
static inline float ns_maxf(float a, float b) { return a > b ? a : b; }
static inline float ns_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float ns_saturate(float v) { return ns_clampf(v, 0.0f, 1.0f); }
static inline float ns_lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* Interpolation indépendante du framerate : `rate` est la fraction rattrapée par
 * seconde. Un lerp naïf par image donne une vitesse qui varie avec les fps. */
static inline float ns_damp(float a, float b, float rate, float dt)
{
    return ns_lerpf(a, b, 1.0f - expf(-rate * dt));
}

/* ------------------------------------------------------------------- ns_v2 */

static inline ns_v2 ns_v2_make(float x, float y) { ns_v2 r = { x, y }; return r; }
static inline ns_v2 ns_v2_add(ns_v2 a, ns_v2 b) { return ns_v2_make(a.x + b.x, a.y + b.y); }
static inline ns_v2 ns_v2_sub(ns_v2 a, ns_v2 b) { return ns_v2_make(a.x - b.x, a.y - b.y); }
static inline ns_v2 ns_v2_scale(ns_v2 a, float s) { return ns_v2_make(a.x * s, a.y * s); }
static inline float ns_v2_dot(ns_v2 a, ns_v2 b) { return a.x * b.x + a.y * b.y; }
static inline float ns_v2_len(ns_v2 a) { return sqrtf(ns_v2_dot(a, a)); }
static inline ns_v2 ns_v2_norm(ns_v2 a)
{
    const float l = ns_v2_len(a);
    return (l > NS_EPS) ? ns_v2_scale(a, 1.0f / l) : ns_v2_make(0.0f, 0.0f);
}

/* ------------------------------------------------------------------- ns_v3 */

static inline ns_v3 ns_v3_make(float x, float y, float z) { ns_v3 r = { x, y, z }; return r; }
static inline ns_v3 ns_v3_splat(float s) { return ns_v3_make(s, s, s); }
static inline ns_v3 ns_v3_zero(void) { return ns_v3_make(0.0f, 0.0f, 0.0f); }
static inline ns_v3 ns_v3_add(ns_v3 a, ns_v3 b) { return ns_v3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline ns_v3 ns_v3_sub(ns_v3 a, ns_v3 b) { return ns_v3_make(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline ns_v3 ns_v3_mul(ns_v3 a, ns_v3 b) { return ns_v3_make(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline ns_v3 ns_v3_scale(ns_v3 a, float s) { return ns_v3_make(a.x * s, a.y * s, a.z * s); }
static inline ns_v3 ns_v3_neg(ns_v3 a) { return ns_v3_make(-a.x, -a.y, -a.z); }
static inline float ns_v3_dot(ns_v3 a, ns_v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float ns_v3_len_sq(ns_v3 a) { return ns_v3_dot(a, a); }
static inline float ns_v3_len(ns_v3 a) { return sqrtf(ns_v3_dot(a, a)); }
static inline float ns_v3_dist(ns_v3 a, ns_v3 b) { return ns_v3_len(ns_v3_sub(a, b)); }

static inline ns_v3 ns_v3_cross(ns_v3 a, ns_v3 b)
{
    return ns_v3_make(a.y * b.z - a.z * b.y,
                      a.z * b.x - a.x * b.z,
                      a.x * b.y - a.y * b.x);
}

static inline ns_v3 ns_v3_norm(ns_v3 a)
{
    const float l = ns_v3_len(a);
    return (l > NS_EPS) ? ns_v3_scale(a, 1.0f / l) : ns_v3_zero();
}

static inline ns_v3 ns_v3_lerp(ns_v3 a, ns_v3 b, float t)
{
    return ns_v3_make(ns_lerpf(a.x, b.x, t), ns_lerpf(a.y, b.y, t), ns_lerpf(a.z, b.z, t));
}

static inline ns_v3 ns_v3_min(ns_v3 a, ns_v3 b)
{
    return ns_v3_make(ns_minf(a.x, b.x), ns_minf(a.y, b.y), ns_minf(a.z, b.z));
}
static inline ns_v3 ns_v3_max(ns_v3 a, ns_v3 b)
{
    return ns_v3_make(ns_maxf(a.x, b.x), ns_maxf(a.y, b.y), ns_maxf(a.z, b.z));
}

/* Réflexion d'un vecteur incident sur une normale — base des réflexions du rendu. */
static inline ns_v3 ns_v3_reflect(ns_v3 i, ns_v3 n)
{
    return ns_v3_sub(i, ns_v3_scale(n, 2.0f * ns_v3_dot(i, n)));
}

/* ------------------------------------------------------------------- ns_v4 */

static inline ns_v4 ns_v4_make(float x, float y, float z, float w) { ns_v4 r = { x, y, z, w }; return r; }
static inline ns_v4 ns_v4_from_v3(ns_v3 v, float w) { return ns_v4_make(v.x, v.y, v.z, w); }
static inline ns_v3 ns_v4_xyz(ns_v4 v) { return ns_v3_make(v.x, v.y, v.z); }
static inline ns_v4 ns_v4_scale(ns_v4 a, float s) { return ns_v4_make(a.x * s, a.y * s, a.z * s, a.w * s); }
static inline float ns_v4_dot(ns_v4 a, ns_v4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

/* ------------------------------------------------------------------- ns_m4 */

static inline ns_m4 ns_m4_identity(void)
{
    ns_m4 r = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
    return r;
}

static inline ns_m4 ns_m4_mul(ns_m4 a, ns_m4 b)
{
    ns_m4 r;
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i < 4; ++i) {
            r.m[c][i] = a.m[0][i] * b.m[c][0] + a.m[1][i] * b.m[c][1]
                      + a.m[2][i] * b.m[c][2] + a.m[3][i] * b.m[c][3];
        }
    }
    return r;
}

static inline ns_v4 ns_m4_mul_v4(ns_m4 a, ns_v4 v)
{
    return ns_v4_make(
        a.m[0][0] * v.x + a.m[1][0] * v.y + a.m[2][0] * v.z + a.m[3][0] * v.w,
        a.m[0][1] * v.x + a.m[1][1] * v.y + a.m[2][1] * v.z + a.m[3][1] * v.w,
        a.m[0][2] * v.x + a.m[1][2] * v.y + a.m[2][2] * v.z + a.m[3][2] * v.w,
        a.m[0][3] * v.x + a.m[1][3] * v.y + a.m[2][3] * v.z + a.m[3][3] * v.w);
}

/* Transforme un point (w=1) puis divise par w : utile pour projeter. */
static inline ns_v3 ns_m4_project(ns_m4 a, ns_v3 p)
{
    const ns_v4 h = ns_m4_mul_v4(a, ns_v4_from_v3(p, 1.0f));
    const float inv = (fabsf(h.w) > NS_EPS) ? 1.0f / h.w : 0.0f;
    return ns_v3_make(h.x * inv, h.y * inv, h.z * inv);
}

static inline ns_m4 ns_m4_translate(ns_v3 t)
{
    ns_m4 r = ns_m4_identity();
    r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
    return r;
}

static inline ns_m4 ns_m4_scale_v(ns_v3 s)
{
    ns_m4 r = ns_m4_identity();
    r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
    return r;
}

static inline ns_m4 ns_m4_rotate_axis(ns_v3 axis, float radians)
{
    const ns_v3 a = ns_v3_norm(axis);
    const float c = cosf(radians), s = sinf(radians), t = 1.0f - c;
    ns_m4 r = ns_m4_identity();
    r.m[0][0] = t * a.x * a.x + c;       r.m[0][1] = t * a.x * a.y + s * a.z; r.m[0][2] = t * a.x * a.z - s * a.y;
    r.m[1][0] = t * a.x * a.y - s * a.z; r.m[1][1] = t * a.y * a.y + c;       r.m[1][2] = t * a.y * a.z + s * a.x;
    r.m[2][0] = t * a.x * a.z + s * a.y; r.m[2][1] = t * a.y * a.z - s * a.x; r.m[2][2] = t * a.z * a.z + c;
    return r;
}

static inline ns_m4 ns_m4_transpose(ns_m4 a)
{
    ns_m4 r;
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 4; ++i)
            r.m[c][i] = a.m[i][c];
    return r;
}

/* Caméra regardant `center` depuis `eye`. */
static inline ns_m4 ns_m4_look_at(ns_v3 eye, ns_v3 center, ns_v3 up)
{
    const ns_v3 f = ns_v3_norm(ns_v3_sub(center, eye));
    const ns_v3 s = ns_v3_norm(ns_v3_cross(f, up));
    const ns_v3 u = ns_v3_cross(s, f);

    ns_m4 r = ns_m4_identity();
    r.m[0][0] = s.x; r.m[1][0] = s.y; r.m[2][0] = s.z;
    r.m[0][1] = u.x; r.m[1][1] = u.y; r.m[2][1] = u.z;
    r.m[0][2] = -f.x; r.m[1][2] = -f.y; r.m[2][2] = -f.z;
    r.m[3][0] = -ns_v3_dot(s, eye);
    r.m[3][1] = -ns_v3_dot(u, eye);
    r.m[3][2] =  ns_v3_dot(f, eye);
    return r;
}

/*
 * Projection perspective, profondeur dans [0,1].
 * `reverse_z` inverse le mapping (near->1, far->0) : gratuit, et cela répartit
 * bien mieux la précision flottante du depth buffer sur les grandes distances.
 * La salle mesure ~30 m mais on veut des ombres de contact propres à 5 cm.
 */
static inline ns_m4 ns_m4_perspective(float fov_y_radians, float aspect, float znear, float zfar,
                                      bool reverse_z)
{
    ns_m4 r = { { { 0 } } };
    const float t = tanf(fov_y_radians * 0.5f);
    r.m[0][0] = 1.0f / (aspect * t);
    r.m[1][1] = 1.0f / t;
    r.m[2][3] = -1.0f;
    if (reverse_z) {
        r.m[2][2] = znear / (zfar - znear);
        r.m[3][2] = (zfar * znear) / (zfar - znear);
    } else {
        r.m[2][2] = zfar / (znear - zfar);
        r.m[3][2] = (zfar * znear) / (znear - zfar);
    }
    return r;
}

static inline ns_m4 ns_m4_ortho(float l, float r_, float b, float t, float znear, float zfar)
{
    ns_m4 r = ns_m4_identity();
    r.m[0][0] = 2.0f / (r_ - l);
    r.m[1][1] = 2.0f / (t - b);
    r.m[2][2] = 1.0f / (zfar - znear);
    r.m[3][0] = -(r_ + l) / (r_ - l);
    r.m[3][1] = -(t + b) / (t - b);
    r.m[3][2] = -znear / (zfar - znear);
    return r;
}

/* Inverse général (méthode des cofacteurs). Utilisé pour reconstruire la position
 * monde depuis la profondeur dans les shaders de ray tracing. */
static inline ns_m4 ns_m4_inverse(ns_m4 mat)
{
    const float *m = &mat.m[0][0];
    float inv[16];

    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    ns_m4 out = ns_m4_identity();
    if (fabsf(det) < 1e-12f) return out;      /* singulière : identité plutôt que NaN */
    det = 1.0f / det;
    float *o = &out.m[0][0];
    for (int i = 0; i < 16; ++i) o[i] = inv[i] * det;
    return out;
}

/* --------------------------------------------------------------- quaternions */

static inline ns_quat ns_quat_identity(void) { ns_quat q = { 0, 0, 0, 1 }; return q; }

static inline ns_quat ns_quat_from_axis(ns_v3 axis, float radians)
{
    const ns_v3 a = ns_v3_norm(axis);
    const float h = radians * 0.5f, s = sinf(h);
    ns_quat q = { a.x * s, a.y * s, a.z * s, cosf(h) };
    return q;
}

static inline ns_quat ns_quat_mul(ns_quat a, ns_quat b)
{
    ns_quat r;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return r;
}

static inline ns_quat ns_quat_norm(ns_quat q)
{
    const float l = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (l < NS_EPS) return ns_quat_identity();
    const float i = 1.0f / l;
    ns_quat r = { q.x * i, q.y * i, q.z * i, q.w * i };
    return r;
}

/* Interpolation sphérique, avec bascule en linéaire quand l'angle est minuscule
 * (sinon division par ~0). Sert aux animations et au lissage de caméra. */
static inline ns_quat ns_quat_slerp(ns_quat a, ns_quat b, float t)
{
    float d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (d < 0.0f) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; d = -d; }

    if (d > 0.9995f) {
        ns_quat r = { ns_lerpf(a.x, b.x, t), ns_lerpf(a.y, b.y, t),
                      ns_lerpf(a.z, b.z, t), ns_lerpf(a.w, b.w, t) };
        return ns_quat_norm(r);
    }
    const float theta = acosf(ns_clampf(d, -1.0f, 1.0f));
    const float s = sinf(theta);
    const float wa = sinf((1.0f - t) * theta) / s;
    const float wb = sinf(t * theta) / s;
    ns_quat r = { a.x*wa + b.x*wb, a.y*wa + b.y*wb, a.z*wa + b.z*wb, a.w*wa + b.w*wb };
    return r;
}

static inline ns_m4 ns_m4_from_quat(ns_quat q)
{
    q = ns_quat_norm(q);
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    ns_m4 r = ns_m4_identity();
    r.m[0][0] = 1 - 2*(y*y + z*z); r.m[0][1] =     2*(x*y + z*w); r.m[0][2] =     2*(x*z - y*w);
    r.m[1][0] =     2*(x*y - z*w); r.m[1][1] = 1 - 2*(x*x + z*z); r.m[1][2] =     2*(y*z + x*w);
    r.m[2][0] =     2*(x*z + y*w); r.m[2][1] =     2*(y*z - x*w); r.m[2][2] = 1 - 2*(x*x + y*y);
    return r;
}

/* Composition translation/rotation/échelle, l'ordre habituel T * R * S. */
static inline ns_m4 ns_m4_trs(ns_v3 t, ns_quat r, ns_v3 s)
{
    return ns_m4_mul(ns_m4_translate(t), ns_m4_mul(ns_m4_from_quat(r), ns_m4_scale_v(s)));
}

/* ------------------------------------------------------------------ ns_aabb */

static inline ns_aabb ns_aabb_empty(void)
{
    ns_aabb b;
    b.min = ns_v3_splat( 3.4e38f);
    b.max = ns_v3_splat(-3.4e38f);
    return b;
}

static inline ns_aabb ns_aabb_add_point(ns_aabb b, ns_v3 p)
{
    b.min = ns_v3_min(b.min, p);
    b.max = ns_v3_max(b.max, p);
    return b;
}

static inline ns_aabb ns_aabb_union(ns_aabb a, ns_aabb b)
{
    ns_aabb r;
    r.min = ns_v3_min(a.min, b.min);
    r.max = ns_v3_max(a.max, b.max);
    return r;
}

static inline ns_v3  ns_aabb_center(ns_aabb b) { return ns_v3_scale(ns_v3_add(b.min, b.max), 0.5f); }
static inline ns_v3  ns_aabb_extent(ns_aabb b) { return ns_v3_sub(b.max, b.min); }
static inline bool   ns_aabb_valid(ns_aabb b)  { return b.min.x <= b.max.x && b.min.y <= b.max.y && b.min.z <= b.max.z; }

static inline float ns_aabb_surface(ns_aabb b)
{
    if (!ns_aabb_valid(b)) return 0.0f;
    const ns_v3 e = ns_aabb_extent(b);
    return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
}

static inline bool ns_aabb_overlaps(ns_aabb a, ns_aabb b)
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x
        && a.min.y <= b.max.y && a.max.y >= b.min.y
        && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

static inline bool ns_aabb_contains(ns_aabb b, ns_v3 p)
{
    return p.x >= b.min.x && p.x <= b.max.x
        && p.y >= b.min.y && p.y <= b.max.y
        && p.z >= b.min.z && p.z <= b.max.z;
}

/*
 * Intersection rayon / AABB, méthode des dalles. Renvoie l'intervalle [t0,t1].
 * Le rayon porte l'inverse de sa direction, calculé une fois : c'est ce qui rend
 * la traversée du BVH rapide (une division par axe au lieu d'une par nœud).
 */
static inline bool ns_ray_aabb(ns_v3 origin, ns_v3 inv_dir, ns_aabb b, float tmax, float *out_t)
{
    const float tx1 = (b.min.x - origin.x) * inv_dir.x, tx2 = (b.max.x - origin.x) * inv_dir.x;
    float tmin = ns_minf(tx1, tx2), tmx = ns_maxf(tx1, tx2);

    const float ty1 = (b.min.y - origin.y) * inv_dir.y, ty2 = (b.max.y - origin.y) * inv_dir.y;
    tmin = ns_maxf(tmin, ns_minf(ty1, ty2));
    tmx  = ns_minf(tmx,  ns_maxf(ty1, ty2));

    const float tz1 = (b.min.z - origin.z) * inv_dir.z, tz2 = (b.max.z - origin.z) * inv_dir.z;
    tmin = ns_maxf(tmin, ns_minf(tz1, tz2));
    tmx  = ns_minf(tmx,  ns_maxf(tz1, tz2));

    if (tmx < ns_maxf(tmin, 0.0f) || tmin > tmax) return false;
    if (out_t) *out_t = ns_maxf(tmin, 0.0f);
    return true;
}

/*
 * Intersection rayon / triangle (Möller–Trumbore). Utilisée par le lancer de
 * rayons du rendu, la collision du joueur et l'occlusion audio — une seule
 * implémentation pour les trois, donc un seul endroit à valider.
 */
static inline bool ns_ray_triangle(ns_v3 orig, ns_v3 dir, ns_v3 a, ns_v3 b, ns_v3 c,
                                   float tmax, float *out_t, float *out_u, float *out_v)
{
    const ns_v3 e1 = ns_v3_sub(b, a);
    const ns_v3 e2 = ns_v3_sub(c, a);
    const ns_v3 p  = ns_v3_cross(dir, e2);
    const float det = ns_v3_dot(e1, p);

    if (fabsf(det) < 1e-9f) return false;              /* rayon parallèle au plan */
    const float inv = 1.0f / det;

    const ns_v3 t = ns_v3_sub(orig, a);
    const float u = ns_v3_dot(t, p) * inv;
    if (u < -1e-6f || u > 1.0f + 1e-6f) return false;

    const ns_v3 q = ns_v3_cross(t, e1);
    const float v = ns_v3_dot(dir, q) * inv;
    if (v < -1e-6f || u + v > 1.0f + 1e-6f) return false;

    const float dist = ns_v3_dot(e2, q) * inv;
    if (dist < 1e-5f || dist > tmax) return false;     /* derrière l'origine, ou trop loin */

    if (out_t) *out_t = dist;
    if (out_u) *out_u = u;
    if (out_v) *out_v = v;
    return true;
}

/* ------------------------------------------------- générateur pseudo-aléatoire */
/*
 * PCG32 : rapide, bien distribué, et surtout **reproductible à graine donnée**.
 * La V1 utilisait `rand()` sans jamais appeler `srand()` (include/hashage.c),
 * donc une suite identique à chaque lancement — l'inverse de ce qui était voulu.
 * Ici la graine est explicite, ce qui permet aussi de rejouer une partie à
 * l'identique côté serveur pour valider un score.
 */
typedef struct ns_rng { uint64_t state, inc; } ns_rng;

static inline void ns_rng_seed(ns_rng *r, uint64_t seed, uint64_t seq)
{
    r->state = 0u;
    r->inc   = (seq << 1u) | 1u;
    /* deux tours d'amorçage, comme le prescrit PCG */
    r->state = r->state * 6364136223846793005ULL + r->inc;
    r->state += seed;
    r->state = r->state * 6364136223846793005ULL + r->inc;
}

static inline uint32_t ns_rng_u32(ns_rng *r)
{
    const uint64_t old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    const uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    const uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

/* Sans biais modulo, contrairement à `rand() % n`. */
static inline uint32_t ns_rng_below(ns_rng *r, uint32_t bound)
{
    if (bound == 0) return 0;
    const uint32_t threshold = (uint32_t)(-(int32_t)bound) % bound;
    for (;;) {
        const uint32_t v = ns_rng_u32(r);
        if (v >= threshold) return v % bound;
    }
}

static inline float ns_rng_float(ns_rng *r)      /* [0,1) */
{
    return (float)(ns_rng_u32(r) >> 8) * (1.0f / 16777216.0f);
}

static inline float ns_rng_range(ns_rng *r, float lo, float hi)
{
    return lo + (hi - lo) * ns_rng_float(r);
}

#endif /* NS_MATH_H */
