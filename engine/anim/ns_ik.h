/*
 * ns_ik.h — cinématique inverse à deux os.
 *
 * Un bras est une chaîne épaule → coude → poignet. On connaît où doit arriver le
 * poignet ; il faut en déduire où placer le coude. À deux os la solution est
 * analytique — loi des cosinus — et il n'y a aucune raison d'itérer.
 *
 * Le coude reste libre sur un cercle autour de l'axe épaule-poignet : c'est le
 * **vecteur de pôle** qui tranche. Sans lui, un bras tendu vers un bouton
 * placerait son coude n'importe où, y compris dans le torse.
 *
 * Trois cas dégénérés, tous traités explicitement, parce qu'ils produisent
 * autrement des NaN qui se propagent dans la matrice et font disparaître le
 * membre sans un mot :
 *   - cible hors d'atteinte : le bras se tend, `reached` vaut false ;
 *   - cible trop proche : la chaîne se replie autant qu'elle peut ;
 *   - pôle aligné avec la chaîne : un axe perpendiculaire est choisi.
 */
#ifndef NS_IK_H
#define NS_IK_H

#include "ns_math.h"

typedef struct ns_ik2 {
    ns_v3 root;      /* épaule — recopiée telle quelle */
    ns_v3 joint;     /* coude */
    ns_v3 end;       /* poignet : la cible si elle est atteignable */
    bool  reached;   /* false si la cible était hors d'atteinte ou trop proche */
} ns_ik2;

/*
 * `pole` est un POINT, pas une direction : le coude est poussé vers lui. Un point
 * se déplace naturellement avec le corps, là où une direction devrait être
 * retournée à chaque changement d'orientation.
 *
 * Des longueurs nulles ou négatives ne sont pas une erreur fatale : la fonction
 * renvoie une chaîne dégénérée mais finie, avec `reached` à false. Un moteur qui
 * s'arrête parce qu'une animation a mal été décrite est pire que le membre
 * fautif.
 */
ns_ik2 ns_ik_two_bone(ns_v3 root, ns_v3 target, ns_v3 pole,
                      float upper_length, float lower_length);

#endif /* NS_IK_H */
