#version 450
/*
 * gbuffer.frag — écrit les propriétés de surface dans le G-buffer.
 *
 * Trois cibles, choisies pour tenir dans le moins de bande passante possible
 * tout en gardant assez de précision :
 *
 *   RT0  RGBA8 sRGB   albédo.rgb + occlusion ambiante
 *   RT1  RGBA16F      normale encodée en octaédrique (xy) + rugosité + métallicité
 *   RT2  R11G11B10F   émissif (les néons et les écrans de bornes)
 *
 * L'encodage octaédrique met une normale unitaire dans deux composantes sans
 * perte visible, ce qui libère deux canaux pour la rugosité et la métallicité —
 * une cible de rendu en moins par rapport à un stockage naïf.
 */

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_tangent;

layout(location = 0) out vec4 o_albedo_ao;
layout(location = 1) out vec4 o_normal_rm;
layout(location = 2) out vec4 o_emissive;

/* set = 2 : textures du fragment shader */
layout(set = 2, binding = 0) uniform sampler2D u_albedo;
layout(set = 2, binding = 1) uniform sampler2D u_normalMap;
layout(set = 2, binding = 2) uniform sampler2D u_orm;      /* occlusion / rugosité / métal */

/* set = 3 : uniformes du fragment shader */
layout(set = 3, binding = 0) uniform Material {
    vec4  u_baseColor;
    vec4  u_emissive;          /* rgb : couleur, a : intensité */
    vec4  u_params;            /* x : métallicité, y : rugosité, z : a-t-on une normal map, w : temps */
    /* x : courbure, y : lignes de balayage, z : reflet de la vitre, w : est-ce
     * un écran. Les deux premières dorment dans `cabinets.json` depuis M4. */
    vec4  u_screen;
};

/*
 * Normalisation SÛRE, et une base tangente qui n'a pas de cas dégénéré.
 *
 * `normalize` d'un vecteur nul vaut 0/0, c'est-à-dire NaN — et un SEUL pixel
 * NaN dans le G-buffer suffit à noircir un quart de l'écran. Le chemin est
 * mesuré, pas supposé : la normale part dans l'éclairage, l'éclairage dans le
 * seuil de halo, et les cinq niveaux de flou séparable du halo étalent ce NaN
 * jusqu'à couvrir un rectangle entier ; le filet de sécurité du tone mapping,
 * qui traduit un non-fini en noir, le rend alors visible d'un coup. C'est le
 * « flash noir » qu'on voyait passer en tournant la tête.
 *
 * Deux vecteurs peuvent s'annuler ici, et les deux arrivent pour de vrai :
 *
 *   - la normale interpolée, quand les normales du triangle s'opposent ;
 *   - la tangente ORTHOGONALISÉE, dès que la tangente du modèle est parallèle
 *     à la normale — ce qui est le cas normal sur une couture d'UV, donc sur
 *     tous les modèles importés.
 */
vec3 safeNormalize(vec3 v, vec3 fallback)
{
    float l2 = dot(v, v);
    return (l2 > 1e-12) ? v * inversesqrt(l2) : fallback;
}

/* Un vecteur unitaire perpendiculaire à `n`, sans branche et sans cas
 * dégénéré (Duff et al., « Building an Orthonormal Basis, Revisited »).
 * Il sert de tangente de repli : n'importe quelle direction du plan tangent
 * convient quand le modèle n'en fournit pas d'utilisable. */
vec3 anyPerpendicular(vec3 n)
{
    float s = (n.z >= 0.0) ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float b = n.x * n.y * a;
    return vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
}

/* Encodage octaédrique : projette la sphère unité sur un carré [-1,1]². */
vec2 encodeOctahedral(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 e = n.xy;
    if (n.z < 0.0) {
        /* Repliement de l'hémisphère inférieur sur les coins du carré. */
        e = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return e;
}

/*
 * Le verre bombé d'une borne.
 *
 * Trois choses distinctes, et il faut les trois : sans la courbure, l'écran est
 * une affiche ; sans les lignes, c'est un écran plat moderne ; sans le reflet,
 * la vitre n'existe pas. C'est le reflet qui fait qu'on VOIT la vitre, et donc
 * qu'on comprend qu'il y a un écran derrière.
 *
 * `u_screen` porte, dans l'ordre : intensité de courbure, force des lignes de
 * balayage, force du reflet, et un drapeau qui dit si le matériau est un écran.
 * Ces deux premières valeurs vivent dans `cabinets.json` depuis M4 sous les noms
 * `curvature` et `scanlineStrength`, et n'avaient **jamais été lues**.
 */
vec2 barrel(vec2 uv, float amount)
{
    /* Déformation en barillet autour du centre de la dalle. Le carré de la
     * distance suffit — un tube cathodique n'est pas une lentille, et la racine
     * ne changerait rien de visible. */
    vec2 c = uv * 2.0 - 1.0;

    /*
     * La RENTRÉE préalable, et c'est elle qui débloque tout le reste.
     *
     * Sans elle, le barillet POUSSE les bords hors de la texture : au coin, où
     * `dot(c, c)` vaut 2, le facteur est 1 + a/2, et ce qui dépasse revient en
     * bande noire. C'était la raison invoquée pour ne courber QUE la dalle
     * vivante — « une image fixe déjà cadrée pour la dalle se retrouverait
     * rognée » — et le prix en était que dix-huit bornes sur dix-neuf
     * n'avaient pas de tube du tout. Le motif était juste, la correction non :
     * il suffisait de rentrer d'abord de l'inverse exact.
     *
     * `k` résout k (1 + a k² / 2) = 1. Deux tours du point fixe suffisent :
     * à la courbure employée (0,16) ils donnent 0,9358 pour 0,9347 exact, soit
     * un dixième de pour cent — un huitième de pixel sur une dalle de 512.
     */
    float k = 1.0 / (1.0 + amount * 0.5);
    k = 1.0 / (1.0 + amount * 0.5 * k * k);
    c *= k;

    c *= 1.0 + amount * dot(c, c) * 0.25;
    return c * 0.5 + 0.5;
}

void main()
{
    vec2 uv = v_uv;
    /*
     * L'ESPÈCE DE L'ÉCRAN, et non plus un booléen : 0 rien, 1 tube, 2 dalle
     * plate. `ns_screen_kind` (ns_scene.h) porte la mesure qui a rendu la
     * distinction nécessaire — le téléviseur du bar recevait le traitement de
     * tube, et ses filets DROITS sortaient bombés de dix pixels.
     */
    const bool is_tube = u_screen.w > 0.5 && u_screen.w < 1.5;
    const bool is_plat = u_screen.w > 1.5;

    if (is_tube) {
        uv = barrel(uv, u_screen.x);
        /*
         * Hors de la dalle après déformation, on est sur le cadre : du noir, pas
         * un bord étiré. Un `clamp` donnerait une bande de pixels tirés le long
         * des quatre côtés, ce qui se lit exactement comme le défaut que c'est.
         */
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            o_albedo_ao = vec4(0.02, 0.02, 0.025, 1.0);
            o_normal_rm = vec4(encodeOctahedral(safeNormalize(v_normal, vec3(0.0, 0.0, 1.0))), 0.22, 0.0);
            o_emissive  = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }

    vec4 albedo = texture(u_albedo, uv) * u_baseColor;

    /* Le découpage alpha sert aux grilles et aux affiches découpées. Le faire
     * ici plutôt que dans la passe d'éclairage évite d'écrire un G-buffer que
     * l'on rejettera ensuite. */
    if (albedo.a < 0.35) discard;

    vec3 orm = texture(u_orm, uv).rgb;
    float occlusion = orm.r;

    /*
     * La rugosité du matériau est l'autorité ; la carte ORM ne fait que la
     * moduler localement (usure, grain, joints). Multiplier les deux, comme on
     * le faisait, écrasait la valeur de famille : une moquette annoncée à 0.96
     * redescendait à 0.27 là où la carte était sombre, et chacune des 49 sources
     * y laissait un point spéculaire — un semis de points blancs sur le sol.
     * La carte est donc recentrée sur 1.0 : elle fait varier de ±25 %, pas plus.
     */
    float roughness = clamp(u_params.y * (0.75 + 0.5 * orm.g), 0.03, 1.0);
    float metallic  = clamp(u_params.x, 0.0, 1.0);

    /* --- normale --- */
    vec3 N = safeNormalize(v_normal, vec3(0.0, 0.0, 1.0));
    if (u_params.z > 0.5) {
        vec3 T = safeNormalize(v_tangent.xyz - N * dot(N, v_tangent.xyz), anyPerpendicular(N));
        vec3 B = cross(N, T) * v_tangent.w;
        /*
         * Z est RECONSTRUIT, jamais lu.
         *
         * Une normale de l'espace tangent est unitaire et pointe vers
         * l'extérieur : z = sqrt(1 - x² - y²) la détermine entièrement. Le
         * stocker était donc un canal payé pour rien, et c'est ce qui permet
         * aux cartes de passer en BC5, qui ne code que deux canaux et qui est
         * LE format d'une carte de normales — BC1 y donne le banding vert bien
         * connu, parce qu'il code le vert sur six bits et interpole en RGB.
         *
         * Le calcul est valable pour les DEUX sources sans drapeau ni
         * variante : sur une carte RVB il retrouve le z qui y était écrit, à
         * l'erreur d'encodage près. C'est ce qui rend le passage au format
         * bloc sans risque de migration.
         */
        vec2 nxy = texture(u_normalMap, uv).xy * 2.0 - 1.0;
        vec3 tn = vec3(nxy, sqrt(max(0.0, 1.0 - dot(nxy, nxy))));
        N = safeNormalize(mat3(T, B, N) * tn, N);
    }
    /* Une face vue de dos (mur regardé depuis l'extérieur, géométrie non
     * fermée du modèle d'origine) doit renvoyer sa normale, sinon elle
     * apparaît noire. */
    if (!gl_FrontFacing) N = -N;

    o_albedo_ao = vec4(albedo.rgb, occlusion);
    o_normal_rm = vec4(encodeOctahedral(N), roughness, metallic);

    /*
     * Émissif modulé par la texture, et non constant.
     *
     * C'est la correction la plus rentable de tout le moteur, pour une
     * multiplication. Écrire `u_emissive.rgb * u_emissive.a` seul faisait sortir
     * chaque matériau émissif en **aplat uniforme** : les dix-huit matériaux du
     * modèle qui portent `Ke 1 1 1` devenaient des rectangles blancs purs. Deux
     * conséquences très visibles, et longtemps prises pour deux bugs distincts :
     *
     *   - les quinze écrans de bornes s'affichaient en blanc, alors que leurs
     *     images (flappy_hard_font.jpg, tetris_font.jpg, snake_font.jpg…) sont
     *     bien présentes et bien échantillonnées dans `albedo` ;
     *   - le plafond, dont la texture est un motif art déco sombre à liserés
     *     dorés, remplissait le haut du cadre d'un gris plat. L'œil s'y adaptait,
     *     et toute la salle était perçue comme sombre — d'où « il manque un toit »
     *     et « la salle est trop sombre », qui étaient le même défaut.
     *
     * La correction suit la convention glTF elle-même : `emissiveFactor` y
     * *multiplie* `emissiveTexture`. Faute de texture émissive séparée dans ce
     * modèle, c'est l'albédo qui joue ce rôle — et c'est légitime, puisque c'est
     * précisément l'image que la surface est censée émettre.
     */
    vec3 emissive = u_emissive.rgb * u_emissive.a * albedo.rgb;

    if (is_plat) {
        /*
         * LA DALLE PLATE : un téléviseur d'aujourd'hui, et rien de ce qui
         * précède.
         *
         * Ce qui distingue un panneau moderne d'un tube n'est pas une question
         * de degré, c'est une liste de choses qu'il N'A PAS : pas de courbure,
         * pas de lignes de balayage, pas de triade de phosphore, pas de coins
         * assombris. Les quatre sont donc absentes ici — le shader ne les
         * atténue pas, il ne les calcule pas.
         *
         * Reste ce qu'il a EN PROPRE, et c'est la vitre. Un tube porte un verre
         * bombé, gris, qui diffuse ce qu'il renvoie ; une dalle haut de gamme
         * porte une glace plane. La différence se voit à une seule grandeur : la
         * rugosité. À 0,06 — la valeur du tube — un néon se lit comme une tache
         * de 30 cm ; à 0,035 il se lit comme le néon, avec ses bords. C'est ce
         * reflet net qui fait dire « écran éteint » plutôt que « affiche
         * sombre », et c'est la seule chose qu'on VOIT d'un panneau qui n'émet
         * pas.
         *
         * MÉTAL À ZÉRO, contrairement au tube. Le tube force 0,35 pour forcer un
         * reflet visible sur un verre gris : c'est un artifice, et il TEINTE le
         * reflet de la couleur de la dalle, puisqu'un métal colore sa
         * réflexion. Une glace est un diélectrique : sa réflexion à incidence
         * normale vaut 4 % et elle est BLANCHE. Le modèle PBR pose déjà F0 =
         * 0,04 pour un métal nul — il n'y a donc rien à forcer, il suffit de ne
         * pas mentir.
         */
        roughness = mix(roughness, 0.035, u_screen.z);
        metallic  = mix(metallic, 0.0, u_screen.z);

        /*
         * Le NOIR d'une dalle. Le tube garde 6 % de son image en albédo — un
         * verre de tube éteint reste gris. Une dalle éteinte est plus noire que
         * ça : 2 %. C'est ce qui donne au panneau son contraste, puisque tout ce
         * qui n'est pas émis tombe au niveau du cadre.
         */
        albedo.rgb *= 0.02;

        o_normal_rm = vec4(encodeOctahedral(N), roughness, metallic);
        o_albedo_ao = vec4(albedo.rgb, occlusion);
    } else if (is_tube) {
        /*
         * Lignes de balayage et masque de phosphore, EN COORDONNÉES DE TEXTURE
         * et non d'écran.
         *
         * C'est le point qui décide si l'illusion tient : indexées sur le pixel
         * de l'écran, les lignes glisseraient sur la dalle quand on bouge la
         * tête, comme un moiré collé à la caméra. Indexées sur la surface, elles
         * sont GRAVÉES dans le tube — on peut tourner autour, elles restent où
         * elles sont. La dalle fait 480 lignes, comme un tube d'arcade.
         */
        const float lines = 480.0;
        float scan = 1.0 - u_screen.y * 0.5 * (0.5 + 0.5 * cos(uv.y * lines * 6.28318530718));

        /* Triade RVB : une colonne sur trois porte chaque primaire. À distance
         * elle disparaît, de près elle donne le grain d'un vrai tube. */
        const float triad = 640.0;
        float ph = fract(uv.x * triad);
        vec3 mask = vec3(ph < 0.3333 ? 1.0 : 0.72,
                         (ph >= 0.3333 && ph < 0.6666) ? 1.0 : 0.72,
                         ph >= 0.6666 ? 1.0 : 0.72);
        mask = mix(vec3(1.0), mask, u_screen.y);

        /* Assombrissement des bords : un tube est plus sombre dans les coins. */
        vec2 e = uv * 2.0 - 1.0;
        float edge = 1.0 - 0.28 * dot(e, e) * dot(e, e);

        emissive *= scan * edge;
        emissive *= mask;

        /*
         * Le verre d'un tube est SOMBRE. L'image ne vient pas de ce qu'il
         * renvoie, elle vient de ce qu'il émet — c'est même à ça qu'on reconnaît
         * un écran éteint : un rectangle presque noir.
         *
         * Laisser l'albédo de la dalle à sa valeur de texture la faisait éclairer
         * DEUX fois : une fois par son émissif, une fois par les plafonniers et
         * les néons de la salle qui la traitaient comme une affiche blanche. Le
         * ciel de Flappy Bird, teinté (0,31 ; 0,75 ; 0,79), ressortait blanc
         * cassé, et l'oiseau se perdait dedans.
         *
         * 6 % de l'image : assez pour qu'un écran éteint garde une teinte, pas
         * assez pour concurrencer ce qu'il émet.
         */
        albedo.rgb *= 0.06 * scan * edge;

        /*
         * Le reflet de la vitre.
         *
         * Il ne se calcule pas ici — le G-buffer ne connaît ni les lumières ni
         * l'environnement. On y prépare seulement le terrain : une dalle de verre
         * est LISSE et légèrement métallique, donc la passe d'éclairage y fera
         * naître les reflets spéculaires des néons et des plafonniers tout
         * seule. C'est ça qui fait qu'on voit la vitre, plutôt qu'une image
         * peinte sur une planche.
         */
        roughness = mix(roughness, 0.06, u_screen.z);
        metallic  = mix(metallic, 0.35, u_screen.z);
        o_normal_rm = vec4(encodeOctahedral(N), roughness, metallic);
        o_albedo_ao = vec4(albedo.rgb, occlusion);
    }

    o_emissive = vec4(emissive, 1.0);
}
