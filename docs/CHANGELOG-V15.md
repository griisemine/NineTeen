# Journal de la reconstruction

Ce document dit ce qui tourne, ce qui reste, et ce qui a été appris en route. Il ne prétend pas
que le chantier est terminé.

---

## Ce qui tourne

### Build et portabilité
- **Les cartes sont compressées par blocs.** BC5 pour les normales, BC1 pour l'ORM, mips
  comprises, dans `engine/core/nstex.h` — vingt octets d'en-tête, lu sans dépendance. Pas du
  KTX2 : rien n'est échangé avec un tiers ici, et prétendre écrire du KTX2 sans en écrire serait
  pire qu'assumer un format maison. BC5 plutôt que BC1 pour les normales n'est pas un détail :
  BC1 y donne le banding vert bien connu, parce qu'il code le vert sur six bits et interpole en
  RGB. Le shader RECONSTRUIT z par sqrt(1 - x² - y²), ce qui est exact pour une normale unitaire
  — et valable pour les deux sources sans drapeau, donc sans risque de migration. Les mips
  viennent du fichier : aucune API ne sait en générer sur une texture bloc, et les calculer à la
  compilation est de toute façon meilleur (on filtre l'image d'origine, pas un niveau déjà
  compressé). Cartes : 148 -> 69 Mio ; archive : 225 -> 153 Mio ; image inchangée (médiane 63
  sur `allee`, avant comme après).
- **Les cartes étaient écrites à la résolution de leur source.** Des normales en 3840 x 2160 pour
  l'image d'un écran de jeu, en 2048² pour le flanc d'une radio : 330 des 421 Mio de l'archive de
  release. Une carte de normales et une carte ORM ne portent pas la même information qu'un
  albédo — l'albédo porte du lettrage et de la trame et se lit au texel, les cartes portent une
  variation que l'éclairage intègre sur plusieurs pixels. `texgen --max-map=` les plafonne à 1024
  (l'albédo dé-cuit garde sa résolution), la moyenne de boîte renormalise les normales pour ne
  pas aplatir le relief deux fois, et l'archive tombe à 225 Mio pour une image identique
  (médiane 64 -> 63 sur `allee`). Un test relit l'entête PNG de chaque carte et refuse celle qui
  dépasse — mutation-testé : à 256 il en signale 128.
- **`cmake --install` n'emportait que le binaire.** `ns_paths` monte « <dossier du binaire>/assets »
  — la disposition d'un paquet installé, et son commentaire le dit — mais la règle d'installation
  n'installait pas un seul asset : l'arbre produit n'avait ni scène, ni texture, ni son. Invisible
  parce que personne n'installait, on lançait toujours depuis l'arbre de build où le chemin est
  passé en dur. Corrigé, plus `cpack` et un workflow de release, vérifiés en déballant l'archive
  et en lançant le jeu **assets de build masqués** — sans quoi le test ne prouve rien.
- Un seul arbre CMake, presets `linux-x64`, `linux-x64-asan`, `macos-universal`, `windows-x64`.
- SDL3 récupéré sur un tag épinglé ; dépendances header-only vendorées, donc build hermétique.
- Shaders GLSL compilés au build et **embarqués dans le binaire** : rien à retrouver à
  l'exécution. En SPIR-V pour Vulkan ; traduits en MSL par `tools/spv2msl` (sur SPIRV-Cross)
  pour Metal, aux emplacements de ressources exacts qu'impose SDL3.
- Le masque de formats présenté à `SDL_CreateGPUDevice` est **dérivé des blobs embarqués** :
  demander un backend qu'on ne sait pas alimenter est devenu impossible.
- CI GitHub Actions : matrice trois OS, tâche ASan+UBSan dédiée, tâche serveur Go avec
  PostgreSQL, `go vet` et `gosec`.
- Avertissements en erreur là où ils traduisent un vrai bug.

### Noyau moteur
- Journalisation à niveaux, sortie fichier, boîte de dialogue native sur assertion.
- Arènes mémoire et allocation tracée avec canaris.
- Horloge à pas fixe 120 Hz avec accumulateur et interpolation au rendu.
- Résolution de chemins par points de montage, refus des chemins remontants.
- Configuration persistante écrite atomiquement.
- Algèbre : matrices colonne-majeure en profondeur [0,1] avec reverse-Z, quaternions,
  intersections rayon/AABB et rayon/triangle partagées par le rendu, la collision et l'audio.
- PCG32 à graine explicite, sans biais modulo.

### Rendu
- G-buffer sur trois cibles, normales encodées en octaédrique.
- Éclairage différé Cook-Torrance, 64 sources.
- Occlusion ambiante en espace écran, échantillonnage hémisphérique cosinus.
- Ombres, illumination globale à un rebond et réflexions **lancées en compute** sur le BVH de
  la salle, avec accumulation temporelle et débruitage à-trous guidé par la géométrie.
- Halo séparable sur cinq niveaux, tone mapping ACES, brouillard, vignettage, grain.
- Élimination par frustum, regroupement des liaisons de texture par matériau.
- Visualisation des cibles intermédiaires en ligne de commande.

### Assets
- `obj2gltf` : la salle Blender vers glTF 2.0, avec conversion Blinn-Phong vers
  metallic-roughness adaptée à l'exporteur détecté, génération des tangentes, déduction des
  sources lumineuses par aire émissive, bibliothèque de matériaux par famille de texture.
- `texgen` : normal map, occlusion de cavité et rugosité dérivées des textures diffuses
  d'origine.
- `bvhbake` : BVH par découpage SAH, partagé entre rendu, collision et audio.

### Atmosphère
- **La suspension du billard existe.** La salle déclarait sa LUMIÈRE — chaude, basse, avec un
  commentaire disant qu'« une table de billard a sa propre lampe » — et aucun LUMINAIRE. Une
  source sans objet visible est précisément ce que le plan s'interdit : c'est ce qui fait qu'une
  pièce paraît éclairée par magie. On ne pouvait pas l'ajouter parce qu'un prop ne savait faire
  que des boîtes et des panneaux ; les types `cylinder` et `sphere` (sur `geo_revolve`) manquaient.
  Câble, abat-jour tronconique ouvert vers le bas, ampoule émissive, et la lumière descendue de
  2,05 à 1,68 m pour passer SOUS l'abat-jour. La table gagne au passage trois billes posées —
  une partie interrompue, pas un meuble. Mesuré : médiane 26 -> 28, pixels sous 16 : 32,5 % -> 29,9 %.
- **Les mains étaient celles d'un enfant de quatre ans.** `VM_HAND` valait 10 cm du poignet au
  bout du doigt, sous un commentaire annonçant « bras d'adulte » : c'est la PAUME seule d'un
  adulte. Portée à 13,5 cm — un compromis assumé et non la cote anatomique (18,5), parce que
  `VM_HAND` sert aussi de distance dans l'IK qui pose le doigt sur sa cible : à 18,5 le poignet
  recule de 8,5 cm et les deux mains sortent par le bas d'un champ de vision de 58°. Les
  viewmodels de jeu à la première personne sont raccourcis pour cette raison exacte ; celui-ci
  le dit maintenant au lieu de prétendre le contraire.
- **La peau était délavée, pas surexposée.** Mesuré au pixel sur une capture au ras d'une borne :
  l'ancienne peau (rapport 1 : 0,68 : 0,54) ressortait à (178 162 150), soit 1 : 0,91 : 0,84 —
  presque grise. 178 n'est pas 255, donc ce n'était pas une surexposition : ACES désature ce qui
  est clair, et le spéculaire blanc s'ajoute par-dessus. Une peau qui doit se LIRE comme de la
  peau à cette luminance part donc plus saturée qu'un nuancier ne le dirait : 1 : 0,51 : 0,36.
- **Les deux bouchons de `geo_cylinder` étaient enroulés à l'envers depuis A3.** Un cylindre fermé
  de rayon 2 et de hauteur 3 rendait un volume signé de 12,55 au lieu de 37,70 : le bouchon du haut
  se RETRANCHAIT. Invisible jusqu'ici parce que la normale de sommet, elle, était juste et que le
  rendu n'élimine pas les faces arrière — mais `gbuffer.frag` retourne la normale d'une face vue de
  dos, si bien que **le dessus de chaque bouton, de chaque grille, de chaque rondelle et de chaque
  pied de tabouret de la salle était éclairé comme s'il regardait le sol**. Aucun test ne mesurait
  le volume d'un cylindre ; il y en a un maintenant, plus un cône et une sphère.
- **`geo_revolve`** : la primitive de révolution que le plan réclamait depuis A3 pour « les pieds de
  tabouret, l'abat-jour, les bouteilles, le jeton ». Le manche de borne y gagne une VRAIE boule :
  il était fait de trois troncs de cône empilés, dont les arêtes vives accrochaient chacune un
  liseré — l'œil comptait trois anneaux au lieu de voir une sphère, sur le seul objet que le joueur
  touche et le plus proche de ses yeux pendant toute une partie.
- **Le panneau de commande a une sérigraphie** (`tools/panelart`). Les dix-neuf bornes portaient
  le `bordeaux.jpg` de 2020 en pavage : un APLAT, sans bord, sans motif, sans repère — sur la plus
  grande surface que le joueur ait sous les yeux quand il joue, plus grande que la dalle. La
  planche reprend la trame en losanges du flanc à un pas trois fois plus court, avec deux filets
  sous la rangée de boutons et un nez de panneau plus clair du seul côté que les avant-bras usent.
  En niveaux de gris, teintée par le matériau : la forme vient du dessin, la couleur du jeu.
- **Les grilles de haut-parleur existent enfin.** Les deux couronnes du bandeau étaient dessinées
  dans le noir de ce bandeau — noir sur noir, donc absentes : entre l'écran et le marquee la borne
  n'avait qu'une plaque morte. Ce qui fait lire une grille n'est pas sa couleur mais son MÉTAL,
  d'où un matériau propre (`borne_grille`, `metal_plate_02`, métallique 0,9, pavage 9 cm) qui
  accroche une lumière que la plaque mate absorbe.
- **Deux essais abandonnés, notés pour qu'on ne les refasse pas.** (1) Baisser de 38 % les onze
  luminaires de plafond : la saturation ne tombait qu'à 6,7 % mais la médiane de l'allée passait
  de 80 à 58 et le classement à 50 % de pixels sous 16. (2) Désaturer et assombrir les caissons
  pour qu'ils cessent de paraître plastique : sous cet éclairage un albédo neutre de valeur
  moyenne vire au PASTEL crayeux, ce qui est pire. La laque saturée est un correctif documenté du
  « trop sombre » (le caisson valait 4 % d'albédo) et il ne faut pas la défaire.
- **Le plafond lumineux est un diffuseur, plus un réflecteur blanc.** Le haut du cadre de
  l'allée était une nappe blanche continue — 8,6 % des pixels au-dessus de 200. La cause n'était
  pas l'émissif (le baisser de 1,05 à 0,58 n'a rien changé : 8,5 %) mais l'ALBÉDO, à (1,00 0,975
  0,94) : le luminaire est 18 cm sous la dalle et l'éclaire à bout portant, donc la face visible
  était dominée par sa réflexion, et une surface assez claire sature en blanc quelle que soit la
  teinte de son émissif. Albédo à (0,28 0,22 0,15) : 6,9 %, la trame des dalles redevient
  lisible, et le blanc devient crème. Les marquees passent de 1,15 à 1,52 — un panneau
  rétroéclairé, pas un autocollant.
- **Ombres par lumière.** Le lancer de rayons retient les **quatre sources** qui contribuent le
  plus à chaque pixel et écrit leur visibilité exacte ; les autres gardent la moyenne pondérée.
  Un point à l'ombre d'un pilier ne perd plus la lumière des écrans de bornes.
- **Brouillard volumétrique** en compute, à demi-résolution : marche le long du rayon de vue,
  phase de Henyey-Greenstein, densité bruitée, occultation par le BVH un pas sur quatre.
  Recomposé par une remontée **guidée par la profondeur**, entre l'éclairage et le halo.
- **Adaptation d'exposition** : luminance logarithmique moyenne mesurée en compute dans un
  tampon de stockage persistant, lissage asymétrique (l'œil s'habitue plus lentement au sombre).
  C'est ce qui a supprimé le plafond brûlé.
- **Les flashs noirs.** Deux causes, toutes deux dans le chemin de présentation. `ns_renderer_draw`
  sortait SANS RIEN ÉCRIRE quand les cibles de rendu n'étaient pas disponibles — au milieu d'un
  redimensionnement, à un changement de palier — et l'appelant présentait quand même la swapchain,
  c'est-à-dire une image noire. Elle renvoie maintenant un `bool` et l'image est ABANDONNÉE
  (`ns_rhi_cancel_frame`) : sauter une image ne se voit pas, en présenter une noire, si. Et
  l'adaptation d'exposition repartait d'une luminance mesurée sur des cibles détruites, ce qui
  pouvait la faire plonger jusqu'à son plancher pendant quelques images.
- **Réverbération par zone.** La salle déclare ses zones dans `salle.room.json` — les toilettes
  carrelées, le sas d'entrée, le coin billard — avec leur niveau d'écho et leur décroissance.
  `roomgen` les valide et les exporte, le mixeur les applique en **départ/retour** sur les bus
  SFX et AMBIENCE, et l'on passe de l'une à l'autre par amortissement plutôt que par saut : le
  temps de franchir une porte. La musique reste sèche.
  `docs/audio-pas-sec.wav` et `docs/audio-pas-toilettes.wav` sont le même pas rendu hors ligne
  dans les deux espaces — le sec retombe au silence en 0,5 s, les toilettes tiennent jusqu'à
  1,1 s.
- L'émissif de complaisance du plafond est **retiré**. Ce qui le rend lisible n'est pas le
  rebond du sol — l'illumination indirecte ne tourne qu'au palier « ultra » — mais la diffusion
  volumétrique devant lui et l'exposition qui cesse de le brûler. Le plan annonçait la première
  explication ; la capture a donné la seconde.

### Le réseau
- **Le classement en ligne, activable.** `engine/net/ns_http.c` — HTTP/1.1, `GET`/`POST`, délais
  bornés, ~300 lignes sur des sockets POSIX/Winsock — et `ns_online.c`, un fil de travail qui ne
  bloque jamais la boucle de jeu. `NS_CFG_SERVER_URL` était une clé réservée depuis M1 que
  personne n'avait jamais lue ; elle a enfin un lecteur.
- **Aucun compte n'est demandé.** Sans jeton, le classement est en lecture seule — ce qui suffit à
  voir le meneur mondial sur la borne de classement. C'était l'erreur de fond de la V1.
- **Sans URL, aucune socket n'est ouverte** : le fil ne démarre même pas. `--offline` verrouille
  par-dessus, et `tests/test_online.c` le vérifie en demandant un classement puis en constatant
  qu'il n'arrive jamais. Le test ouvre de VRAIES sockets vers un serveur Go local : un client
  réseau qu'on ne fait jamais parler à personne est un client dont on ne sait rien.
- **Pas de TLS**, et une URL `https://` est REFUSÉE plutôt que tentée en clair — envoyer un jeton
  de session sur un port qui ne le comprend pas serait pire que d'échouer.
- **Une partie jouée hors ligne n'est pas soumettable**, par conception : le serveur tire la graine
  et le secret AVANT la partie, et un score sans eux n'est pas vérifiable. L'accepter reviendrait
  à la V1, où le client annonçait son score et le serveur le croyait.
- **Le billet de partie, et l'envoi.** Le client demande `POST /api/v1/runs` quand le joueur
  ARRIVE devant la borne, joue sur la graine reçue, et poste la partie scellée sur
  `POST /api/v1/runs/{id}/submit`. Rien n'attend : sans billet prêt, la partie se joue hors
  ligne. La file garde ce qu'un 5xx n'a pas pu livrer et le renvoie au démarrage suivant.
- **Le test qui déroule tout.** `ns_test_online <url> <jeton>` fait la chaîne entière contre le
  vrai `nineteend` et sa base. C'est le seul endroit qui confronte les deux moitiés écrites en C
  et en Go, et il a trouvé trois défauts qu'aucun test unitaire n'aurait pu voir, parce qu'ils
  vivent ENTRE les deux :
  1. Le serveur nomme ses tableaux `flappy-easy` / `flappy-hard` ; le moteur porte un jeu et une
     difficulté séparés. Le client cherchait « flappy », qui n'existe nulle part côté serveur :
     classement mondial introuvable pour les huit jeux, et ouverture de partie refusée.
  2. La graine de partie fait **63 bits** et se lisait dans un `float` — 24 bits de mantisse.
     Le client aurait joué une autre partie que celle ouverte, et chaque envoi aurait été refusé
     pour sceau invalide sans qu'une ligne dise pourquoi.
  3. Le fichier de file était posté tel quel, avec ses champs d'enveloppe ; le serveur décode
     avec `DisallowUnknownFields` et aurait répondu 400 — donc un 4xx, donc un fichier supprimé.
     Toutes les parties auraient été jetées une par une en croyant les envoyer.

### La borne, confrontée à tes images

Le profil de la référence se lit de bas en haut sur l'arête AVANT ; le dos est
vertical. Voici les neuf gradins que montrent tes vues, et ce que la borne
générée en fait. `docs/render-borne-profil.png` est la capture de côté qui
permet de vérifier au lieu de me croire.

| # | Hauteur | Sur tes images | Dans `build_cabinet` |
|---|---|---|---|
| 1 | 0 → 10 cm | socle noir, en retrait | plinthe en retrait de 3 cm |
| 2 | 10 → 86 cm | face avant verticale, porte à monnayeur | face verticale + bloc monnayeur, deux fentes, trappe de caisse |
| 3 | 86 → 92 cm | le panneau de commande jaillit | porte-à-faux du panneau |
| 4 | 92 → 97 cm | sa tôle inclinée | plan incliné, sérigraphié depuis B25b |
| 5 | 97 → 110 cm | la face recule sous l'écran | retrait du profil |
| 6 | 110 → 148 cm | l'écran incliné, en retrait | dalle 16:9 déclarée, cadre à quatre plats |
| 7 | 148 → 162 cm | panneau haut-parleurs, deux grilles rondes | plan incliné noir + deux grilles métalliques (B25) |
| 8 | 162 → 190 cm | marquee en SURPLOMB | caisson lumineux débordant de 16 cm |
| 9 | 190 cm → dos | le dessus redescend vers l'arrière | pente arrière du profil |

Les neuf y sont, et le caisson est une **extrusion de ce profil** — pas une
boîte avec des décalcomanies, ce qu'il était jusqu'à B14.

### Le modèle de référence : ce que j'ai cherché, et ce que j'ai trouvé

La consigne était double — « implémente la borne que je t'ai envoyée, à
l'identique » ET « n'aie pas peur de télécharger des modèles d'objet open source
bien faits ». J'ai longtemps répondu à la première par une objection de licence
sans avoir rien vérifié de la seconde. Voici les faits, mesurés :

| Source | Résultat |
|---|---|
| la page free3d de la référence | **HTTP 403** depuis ce conteneur — inatteignable, licence mise à part. *Vérifié depuis :* le modèle **coûte 19 $** et sa licence est **« Royalty Free — Editorial only »**. « Editorial only » interdit l'usage dans un produit : il n'est pas distribuable dans un jeu, **même acheté**. L'objection de licence, longtemps avancée sans preuve, se trouve être exacte — mais elle ne l'était pas encore quand elle a été faite |
| Poly Haven, 521 modèles CC0 | **aucune borne d'arcade**. Les résultats sur « cabinet » sont des meubles : gothique, chinois, à tiroirs, en bois peint |
| glTF-Sample-Assets de Khronos, 148 modèles | **aucune** |

Il n'existe donc pas, dans les deux bibliothèques ouvertes que ce projet peut
atteindre, de borne d'arcade à importer. Ce n'est plus une préférence de ma part,
c'est un constat — et c'est ce qui rend la borne paramétrique nécessaire plutôt
que seulement commode. Les modèles CC0 qui existent et servent déjà (tabourets,
canapé, extincteur) viennent de la même recherche, faite pour le mobilier.

Ce qui n'est PAS fait, et qui est donc une contrainte et non un oubli : le modèle
free3d lui-même n'est pas importé. Il demande un compte, il n'est pas CC0, et le
versionner casserait à la fois la reconstructibilité hors ligne promise en A2b et
la clarté des licences d'`assets/cc0/LICENSES.md`. La borne est **reproduite
d'après les images**, ce qui a par ailleurs l'avantage de garder les ancres
déclarées — centre d'écran, centre de panneau, fente à jetons, sommet du manche —
dont dépendent l'IK des bras et le placement des mini-jeux, et qu'un maillage
importé ne déclarerait pas.

### Les bornes
- **Le caisson est une EXTRUSION DE PROFIL**, plus une boîte. Ce qui fait qu'on reconnaît une
  borne d'arcade au premier coup d'œil n'est ni sa couleur ni son marquee : c'est son profil
  latéral en gradins — socle en retrait, face verticale, panneau de commande qui jaillit, retrait
  sous l'écran, écran incliné, panneau haut-parleurs, marquee en surplomb, dessus qui redescend.
  Neuf gradins, dont aucun n'existait.
- **Le marquee est un caisson lumineux en surplomb**, pas une décalcomanie ; le **panneau
  haut-parleurs** et ses deux grilles apparaissent ; la **porte à monnayeur** aussi — c'est le
  détail qui dit « borne » plus fort que tout le reste.
- Un matériau **`borne_noir`** pour les bandeaux. B1 avait éclairci `borne_cadre` au motif
  qu'aucune lumière ne rend visible un matériau noir : la leçon valait pour le CAISSON, pas pour
  le cadre d'écran. Un cadre sombre autour d'une dalle lumineuse est précisément ce qui fait lire
  l'écran comme un écran.
- Coût mesuré : **+2 014 sommets** sur toute la salle (144 941 → 146 955) et **611 ms/image** au
  palier medium, contre 675 ms avant. Aucune régression.

### Les bornes, remodélisées sous Blender

L'extrusion à neuf gradins ci-dessus a été **remplacée par un modèle construit
sous Blender**, `assets/blender/borne.py`, exporté en glTF et instancié par
`roomgen`. Le profil paramétrique avait progressé de version en version, mais il
lui manquait trois choses que les vues de référence disent sans ambiguïté, et
que `geo_shapes` n'avait aucun moyen de produire.

- **Le T-molding.** Le jonc de chant — le bourrelet vif qui court sur TOUS les
  chants du caisson : l'arête avant, l'arête arrière, le pourtour du marquee — est
  ce à quoi on reconnaît une borne d'arcade avant d'en lire la couleur. Il était
  **totalement absent** : la borne présentait des arêtes vives de caisson peint,
  c'est-à-dire une armoire. C'est le changement qui se voit de plus loin ; dans
  l'allée, c'est lui qui dessine la silhouette des dix-neuf bornes.
  Techniquement : un modificateur *Bevel* en mode POIDS sur le pourtour des deux
  flancs, 9,5 mm (un T-molding réel fait 3/4"), avec son propre matériau. Il
  reste DANS l'emprise de la borne — un bourrelet en saillie aurait élargi le
  caisson de 2 cm et fait mentir le contrôle de chevauchement de la salle.
- **La doucine.** Sous le panneau de commande, la face avant ne fait pas un
  décrochement droit : elle remonte en courbe **concave puis convexe**. Mesuré
  sur la vue de profil, hauteur → avancée : 0,719 → 0,300 ; 0,760 → 0,310 ;
  0,801 → 0,349 ; 0,842 → 0,406 ; 0,883 → 0,428. Rapportée à *t* ∈ [0,1] la suite
  vaut 0,016 0,078 0,234 0,383 0,633 0,828 0,945 — c'est un *smootherstep*
  (6t⁵−15t⁴+10t³) à quelques centièmes près, et non une droite.
- **La casquette penchait à l'envers.** Le profil faisait reculer le haut de la
  borne (z de 0,04 à −0,04) là où la référence le fait **avancer** (0,054 → 0,207
  entre y = 1,457 et 1,579) : le panneau haut-parleurs SURPLOMBE l'écran. C'est
  la différence entre une casquette et un pupitre, et c'est une des raisons pour
  lesquelles la borne se lisait comme un meuble de bureau.
- **Deux postes de jeu** : deux manches à boule (bleu et rouge, sur tige
  chromée) et **deux blocs de six boutons** (2 × 3), au lieu d'un manche et de
  quatre pastilles. Les deux blocs partent à DROITE de leur manche, pas en
  miroir : c'est la main droite qui appuie, dans les deux cas.
- La **porte à monnaie** est un cadre de 0,25 × 0,55 en saillie de 2 cm, avec
  deux fentes à insert rouge, deux trappes de renvoi, la porte de caisse et ses
  deux serrures chromées. Les **grilles de haut-parleur** sont quatre anneaux
  concentriques en relief — des bandes ouvertes, et non des disques : quatre
  disques empilés ne montrent que le plus grand, ce qui redonnait la plaque
  morte qu'on cherchait à supprimer.
- Un matériau **`borne_chrome`**, distinct de `borne_grille`. Une grille de
  haut-parleur est un métal SOMBRE — c'est ce qui la fait lire comme une trame —
  et la tige du manche prenait ce même noir : dans la pénombre de l'allée elle
  disparaissait sous sa boule.

**Ce que `roomgen` garde**, et pourquoi ce n'est pas négociable : la **dalle**
(son matériau est cloné par borne — sinon les dix-neuf écrans afficheraient la
même chose — et sa position doit être annoncée au moteur), les **quatre ancres**
(centre d'écran, grappe de boutons, fente à jetons, sommet du manche), et la
**table des matériaux**, pour que les dix-neuf bornes gardent leur teinte par
jeu avec un seul maillage. Ancres et dalle sont **lues** dans
`borne.ancres.json`, écrit par le script qui produit le glTF, depuis les mêmes
cotes : les recopier dans `roomgen` les ferait dériver de la géométrie à la
première retouche — c'est exactement ce qui avait laissé un second monnayeur
flotter 13 cm devant la borne.

**Ce qui a été contraint, et non mesuré.** La largeur (0,72), la profondeur
(0,88) et la hauteur (1,88) sont celles que la salle occupe déjà : les changer
déplacerait les dix-neuf bornes. La référence est proportionnellement plus
élancée — rapport hauteur/profondeur **3,05 contre 2,54 ici** — donc c'est la
FORME qui est reprise, pas l'élancement. De même, la référence place le panneau
de commande à 1,02 m ; il est tenu ici à moins d'un centimètre de sa hauteur
d'avant, pour ne pas défaire le réglage de B14b ni déplacer la pose des bras.
Enfin la référence fait redescendre le dessus de 25° vers l'arrière ; sur une
profondeur de 0,88 cela mettrait le dos à 1,61 m, donc **12,7°** (dos à 1,70 m).

**Coût mesuré.** Salle entière : **69 029 → 145 979 triangles** (+111 %), soit
**4 992 triangles par borne** — la référence en fait 18 330, pour un rendu de
catalogue ; on en met dix-neuf dans une salle temps réel. Temps d'image sur le
rasteriseur logiciel du conteneur, vue « allee », 120 images : **97,6 → 114,8 ms
en moyenne** (+18 %), minimum inchangé à 87,5 ms. Sur un vrai GPU l'écart serait
invisible ; il est écrit ici parce que c'est ce qui a été mesuré, pas ce qui
est probable. `ctest` : 32/32.

### La borne
- **Le flanc porte sa sérigraphie** : le dégradé qui s'éclaircit vers le marquee
  et la trame de losanges en diagonale des vues de référence. C'est la dernière
  chose qui manquait à la silhouette refaite en B14 — un caisson au bon profil
  mais peint d'un aplat se lit encore comme un meuble.
- Elle est **dessinée**, pas rapportée : `tools/sideart` la produit en vingt
  lignes d'arithmétique. Le modèle free3d demande un compte, donc le versionner
  casserait la reconstructibilité hors ligne et la clarté d'`assets/cc0/LICENSES.md` ;
  une planche générée n'a ni licence à démêler ni fichier à retrouver, se
  régénère à l'identique sur les trois plateformes, et se règle — pas de la
  trame, épaisseur des lignes, dégradé — au lieu d'être subie.
- Elle est en **niveaux de gris**, teintée par le matériau de chaque borne. Les
  dix-neuf bornes gardent donc leur couleur de jeu — on trouve « sa » borne de
  loin, ce qui est un vrai service rendu au joueur — tout en portant chacune le
  flanc de la référence.
- Techniquement, ce sont les **deux bouchons de l'extrusion** qui SONT les deux
  flancs. `geo_profile_extrude_capped` leur donne un matériau propre et cadre
  leurs UV sur la boîte englobante du profil, de (0,0) à (1,1) : une planche
  dessinée pour un flanc s'y pose entière quelle que soit la taille de la borne.
  Sans ce cadrage les UV resteraient en mètres par répétition — ce qu'il faut
  pour une moulure, ce qui couperait une sérigraphie.

### Les jeux
- **Une interface commune** (`games/games.h`) : un jeu déclare la taille de son état, ses
  planches, ses trois sons, son vocabulaire d'événements et sept fonctions. Ce n'est pas un
  moteur de jeu — un mini-jeu d'arcade est un état qu'on avance d'un pas fixe et qu'on dessine,
  et c'est la seule chose qu'on abstrait.
- **Flappy Bird**, aux cotes de 2020 au pixel près, avec sa physique réécrite en flottant.
- **Snake** — et ce n'est pas le Snake à cases : un serpent à **angle libre** qui tourne tant
  qu'on tient la direction, avec de la **digestion** (la bosse d'un fruit avalé descend
  visiblement le corps) et les **trente-deux objets** de 2020, table `FRUIT_PROPRIETES`
  recopiée intégralement. Le **hardcore est l'inverse du normal** : manger coûte cinq fois la
  valeur du fruit, et le score vient de ceux qu'on laisse pourrir. C'est la règle la plus
  surprenante de l'original, et elle est conservée.
- **Démineur** — la grille de **16 x 25** et ses **100 bombes** (le quart des cases, comme
  l'original), le découpage de `demineur.png` en tuiles de 54 x 54 à leurs positions exactes,
  et la règle qui distingue un démineur d'une loterie : **les bombes ne sont posées qu'après le
  premier dévoilement**, en épargnant la case jouée et ses huit voisines.
  - **Une borne n'a pas de souris.** Le curseur se déplace au manche, case par case, et
    **maintenir une direction fait défiler** — sans quoi traverser vingt-cinq colonnes
    demanderait vingt-cinq appuis. C'est la seule liberté prise avec le jeu d'origine, et elle
    est prise parce que le contraire rendrait le jeu injouable là où il est censé se jouer.
  - Le joueur automatique **déduit** au lieu de tirer au sort, et quand la déduction s'épuise il
    **estime le risque** de chaque case fermée à partir des chiffres voisins plutôt que d'ouvrir
    la première venue. Mesuré sur 200 parties : 99,5 cases ouvertes en moyenne contre 84,8, et
    10,4 s de survie contre 8,6. Il ne gagne jamais — une grille minée au quart ne se gagne pas
    par déduction locale — et c'est dit plutôt que caché : il sert à prouver qu'on peut
    enchaîner des milliers de pas sans NaN ni fuite, pas à jouer à notre place.
- **Tetris** — la grille de 10 x 20, la courbe de vitesse à quatre
  temporisations et son amortissement géométrique, et surtout le barème, qui
  n'est **pas** celui d'un Tetris standard : cent points la ligne, **doublés à
  chaque ligne simultanée** (un quadruple vaut donc 1 500, pas 400) et
  **multipliés par dix** quand la ligne est d'une seule couleur. Viser la
  couleur rapporte plus que viser le quadruple, et c'est ce qui rend le Tetris
  de 2020 reconnaissable.
  - La table des pièces — **onze mille deux cents entiers** écrits à la main,
    deux difficultés, deux tailles, sept pièces, quatre rotations — est
    **recopiée par un script**, pas à la main : une rotation fausse sur une
    pièce sur cinquante-six ne se voit pas sur une capture, elle se découvre en
    jouant. Le condensé du fichier de 2020 est vérifié par le test ; s'il
    change, le test le dit.
  - Deux constats mesurés qui interdisaient tout raccourci : les deux
    difficultés n'ont **aucune** forme en commun, et si la forme géante **est**
    la forme normale doublée, son **pivot ne l'est pas** dans 42 cas sur 56 —
    un générateur aurait donné la bonne forme et la mauvaise rotation.
- **Asteroid** — le vaisseau et son inertie de commande : rotation montée par
  une **rampe de neuf images**, poussée par une **rampe de cinq**, décélération
  de 1,015 par image, vitesse plafonnée. Les **six variétés d'astéroïdes** de 50
  à 500 points, leurs **quatre quartiers de taille** qui multiplient ce score par
  0,2 à 1 — un petit fragment rare vaut donc plus qu'un gros caillou commun —
  la **fragmentation** au-dessus de 36 px, et les **cinq armes** avec leurs
  tables de fréquence, vitesse, dégâts, rayon et durée. La **glace** ne fait
  aucun dégât : elle gèle, et c'est la règle la plus surprenante de l'arsenal.
  - Le vaisseau **rebondit** sur les murs au lieu de s'enrouler. Ce n'est pas ce
    qu'on attend d'un Asteroids, c'est ce que fait celui de 2020, et c'est ce
    qui rend le terrain lisible : on sait toujours où est son vaisseau.
  - La couche 2D sait maintenant **tourner un quad**. Elle en avait besoin ici et
    nulle part avant : un vaisseau qui pivote ne se dessine pas avec des
    rectangles alignés sur les axes, et l'approcher par une pile de bandes
    horizontales donne une écharde à quarante-cinq degrés.
- **Pac-Man** et **Piano** — et il faut dire ce qu'ils étaient. Le Pac-Man de
  2020 tient en 251 lignes : une grille de pastilles, un labyrinthe dont
  `carte1()` ne trace QUE le bord, un personnage qui avance en ligne droite. Pas
  de score, pas de mort, pas de niveau, et **aucun fantôme** — `enemy.png` est
  là, chargé par personne. Le Piano en fait 308 et **ne compte pas un seul
  point**. Ce ne sont pas des jeux, ce sont des amorces.
  - `rulesTable` le disait déjà : elle attend `pellet`, `power`, `ghost` et
    `level` pour l'un, `note` et `combo` pour l'autre. **Les tables décrivaient
    depuis M6 les jeux qu'ils devaient être.**
  - Pac-Man reçoit donc un vrai **labyrinthe** — écrit en clair, symétrique,
    avec ses tunnels — et **quatre fantômes** aux quatre comportements de la
    borne de 1980 (poursuite, embuscade, dispersion, hasard), leur alternance
    dispersion/poursuite, les super-pastilles et la chaîne 200/400/800/1600.
  - Piano garde ce qui faisait son jeu : la partition de `musique.txt` recopiée
    à l'entier près, et la règle qui décide de tout — **frapper une voie vide
    termine la partie**. Laisser passer une note, en revanche, ne fait que
    casser le combo : c'est la faute de commission qui est punie, pas l'oubli.
    La partition boucle **8 % plus vite à chaque tour**, sans quoi treize notes
    font cinq secondes de jeu.
- **Shooter** — le couloir étroit d'un tiers d'écran, le vaisseau à
  `SHIP_SPEED 10` px/image et son amortissement de 0,9, les **cinq emplacements
  d'armes** et la table `WEAPON_DISPOSITION` qui décide lesquels s'allument
  — deux canons ne sont pas les deux premiers mais le deuxième et le quatrième,
  donc symétriques — les trois missiles alliés, et les **cinq types d'ennemis**
  avec leurs points de vie `{1, 7, 25, 40, 90}`, leurs sept armes, leurs
  rechargements, leurs rafales et leur type de visée.
  - C'est **la seule table du serveur que le portage n'a pas eu à changer** :
    `scaled{"enemy": 15}` était déjà le barème, et la valeur d'un ennemi est ses
    points de vie. Un ennemi à 90 vaut 1 350, un ennemi à 1 en vaut 15.
- **Les dix-neuf bornes de la salle jouent toutes.** Plus une seule ne dit
  « pas encore porté ».
- Tout le temps de 2020 est compté **en images à 30 Hz**. Chaque constante est convertie en
  secondes, sa valeur d'origine écrite à côté, et un test vérifie que le jeu se comporte
  pareil à 120 Hz et à 40 Hz.
- **Le classement de la borne se construit tout seul** depuis la table des jeux portés, et
  **tourne les pages** toutes les six secondes : quatre colonnes à la fois, en grand. La dalle
  fait 62 cm et se lit à deux mètres et demi — y serrer seize colonnes donnerait un tableau
  complet et illisible, ce qui est pire qu'un tableau incomplet.

### Réglages
- **Un menu dessiné**, ouvert par `Échap` : palier de qualité, échelle de rendu, densité de
  poussière, luminosité, les quatre volumes, sensibilité de la souris. Tout est écrit dans
  `settings.cfg` à la fermeture.
- Chaque ligne porte **le chiffre qui aide à choisir** : le coût relatif du palier (`x0.17` à
  `x3.92`, mesuré) et le pourcentage de pixels que l'échelle économise. Ils existaient déjà dans
  le journal et la documentation ; les laisser hors de l'écran revenait à demander d'essayer les
  cinq paliers à l'aveugle.
- **La salle continue de vivre derrière le voile** — c'est ce qui permet de juger un réglage
  pendant qu'on le change. Seul le joueur est figé, et une partie en cours est en pause.
- `Échap` ne quitte plus le jeu. Il fallait deux appuis, le premier relâchait la souris et le
  second fermait la fenêtre : perdre sa partie ainsi est un défaut, pas un raccourci.
- Changer de palier reprend les défauts du palier **sauf** les trois réglages qui ont leur propre
  ligne. `tests/test_menu.c` (27 vérifications) le tient : sans ça, régler la poussière puis
  changer de palier l'effacerait sans le dire.

### Joueur
- Position des pieds et position de l'œil distinguées ; `eye_height` est enfin lue.
- Collision en capsule balayée contre la géométrie réelle, avec gravité, glissement le long des
  murs, **hauteur de marche** (une plinthe de trois centimètres n'arrête plus personne) et
  vérification du plafond au saut.
- Accroupissement avec contrôle de dégagement : on ne se relève pas sous une table.
- Oscillation de marche pilotée par la **distance réellement parcourue** — pas par le temps, pas
  par la vitesse demandée. Impulsion à l'atterrissage, roulis en virage, respiration à l'arrêt.
- Tout l'état d'animation est interpolé au rendu, et l'oscillation est appliquée *après*
  l'interpolation : échantillonnée à 120 Hz puis interpolée linéairement, une sinusoïde perd ses
  sommets.
- `roomgen` : la salle **générée depuis sa description** (`assets/scene/salle.room.json`), en
  mètres, objet par objet. Coquille avec baies, sols, plafond en dalles de 0,60 m sur rails en T,
  piliers, poutres, plinthes, corniche, cimaise, nez de marche. Il écrit aussi les lumières et le
  fichier de scène : rien n'y est déduit. Un matériau inconnu, une texture absente, un budget
  dépassé ou une baie incohérente cassent le build en nommant le coupable.
- `geo_mesh` / `geo_shapes` : le substrat — soudure, normales lissées pondérées par l'aire,
  tangentes de Lengyel, boîte chanfreinée, plan, panneau, extrusion de profil mitrée, pan de mur
  percé **sans aucune opération booléenne**. 114 vérifications, plus huit refus prouvés chacun dans
  son propre processus.

### Serveur et site
- Binaire Go unique, front et migrations embarqués, PostgreSQL.
- Argon2id, sessions opaques stockées hachées, limitation de débit, CSRF, en-têtes stricts.
- Autorité serveur sur les scores : journal de partie scellé par HMAC, score recalculé,
  invariants par jeu. Seize scénarios d'attaque couverts par des tests, et vérifié de bout en
  bout contre une vraie base.
- Site refait, direction artistique empruntée au décor du jeu, polices du jeu.
- `docker-compose` avec base non exposée, image distroless, conteneur en lecture seule.

### Documentation
- Audit du code d'origine : 22 constats, dont un prouvé sous AddressSanitizer et trois
  hypothèses explicitement écartées.

### Jouer

- **Couche 2D.** `engine/sprite/` : 4 096 quads en 128 lots, mélange alpha, police 5x7 intégrée
  au binaire, cible redirigeable. C'est elle qui permet à un jeu de dessiner aussi bien dans la
  dalle d'une borne que sur tout l'écran.
- **Flappy Bird jouable**, porté depuis les 1 402 lignes de 2020. Les planches et les cotes de
  découpe sont celles d'origine au pixel près ; les cotes du terrain viennent de ses constantes
  (`DISTANCE_BETWEEN_OBSTACLE`, `DISTANCE_UNDER_OBSTACLE`, l'échelle 4). La physique, elle, est
  réécrite en flottant au pas fixe : l'original intégrait en nombre d'images, ce qui rendait la
  chute dépendante de la fréquence d'affichage.
- **Le jeu tourne DANS la borne**, derrière son verre bombé — courbure barillet, lignes de
  balayage, masque de phosphore, reflet de vitre —, et la tête reste libre. On peut se pencher,
  reculer, regarder la borne d'à côté pendant qu'on joue.
- **Les mains sur les commandes** : la gauche empoigne le manche, la droite couvre les boutons,
  l'index s'enfonce à chaque battement d'aile. Les trois ancres d'une borne — fente, boutons,
  sommet de la boule — sont déclarées par `roomgen` et visées par une IK à deux os avec
  correction du bout du doigt. Vérifié à 3 cm près par `tests/test_ik.c`.
- **La poussière** dans les faisceaux : éclairée par les lumières réelles de la salle, réponse au
  carré pour que le grain ne se voie que DANS la lumière, et champ proche seulement — au-delà de
  quelques mètres c'est le brouillard volumétrique qui a raison.
- **Classement local**, écrit atomiquement, sans jamais demander de compte. Le journal de partie
  est scellé au format exact du serveur (HMAC-SHA256, charge canonique), et
  `tests/test_scores.c` le confronte à des vecteurs produits par le code Go lui-même.
- **Classement en ligne, de bout en bout.** Le client prend un billet de partie
  (`POST /api/v1/runs`) AVANT de jouer, joue sur la graine du serveur, scelle son journal avec
  le secret reçu, le range dans une file sur disque et l'envoie
  (`POST /api/v1/runs/{id}/submit`). Un 5xx ou un silence garde le fichier ; un verdict — 2xx
  comme 4xx — l'efface. Le tout **sans jamais faire attendre une partie** : le billet est tiré
  d'avance, et sans billet on joue hors ligne. La chaîne a été déroulée contre le vrai serveur
  Go et PostgreSQL — score recalculé côté serveur, verdict `ok`, ligne au classement mondial —
  et `ns_test_online <url> <jeton>` la rejoue.
- **Cinq paliers de qualité chiffrés** (`potato` à `ultra`), réglables en jeu par `F7`/`F8` et
  gardés d'une session à l'autre.
- **Du vrai mobilier** : `roomgen` sait instancier un glTF (`tools/geo_import.c`, sur le `cgltf`
  déjà vendoré). Tabourets de bar, canapé et extincteur viennent de modèles CC0 au lieu d'être
  des empilements de boîtes.

---

## Ce qui reste

- **Les huit mini-jeux sont portés.** Flappy Bird, Snake, Démineur, Tetris, Asteroid, Pac-Man,
  Piano et Shooter, jouables sur leur borne comme en plein écran. Les 9 800 lignes de 2020 sont
  toutes passées sur `ns_sprite`, au pas fixe, avec leurs tables recopiées et leurs règles
  vérifiées jeu par jeu. Ajouter un jeu est désormais une ligne
  dans `games/games.c` : c'est ce que Snake a vérifié, et que Démineur puis Tetris ont confirmé
  sans que `room/main.c` ait à connaître leur nom.
- **Les bras n'atteignent pas le panneau depuis le point de vue « borne ».** Constaté en
  remodélisant la borne, et **antérieur à ce changement** : la même capture prise avant donne
  exactement la même pose, mains pendantes à hauteur de monnayeur. Les quatre ancres sont
  pourtant justes — `test_ik` les atteint à 3 cm près sur des cotes synthétiques, et la hauteur
  du manche n'a pas bougé de plus de 4 mm. Le suspect est donc l'accrochage du joueur à la borne
  depuis un point de vue nommé, pas la géométrie. Non corrigé ici : ça touche
  `room_viewmodel.c`, où d'autres travaillent.
- **Le temps réel — présence et duels.** C'est tout ce qui reste côté réseau, et c'est
  délibéré : ça se conçoit avant de s'écrire. Le classement en ligne, lui, **fonctionne de
  bout en bout** (voir ci-dessus).
- **Un décimateur de maillage.** Les modèles CC0 sont taillés pour le cinéma — 14 000 triangles
  pour un tabouret. C'est ce qui limite aujourd'hui le mobilier importé à trois modèles :
  au-delà d'environ 160 000 sommets, le rasteriseur logiciel du conteneur de développement cesse
  de composer l'image finale, et je ne livre pas ce que je ne peux pas regarder.
- **La signature des paquets.** Les paquets eux-mêmes sont faits (`cpack`, une archive autonome
  par plateforme, et `.github/workflows/release.yml` qui les attache à une balise) et vérifiés en
  déballant puis en lançant le jeu **avec les assets du build masqués**. Ce qui manque est le
  certificat : un `.dmg` notarié demande un Developer ID Apple, un `.msi` signé un certificat
  Authenticode. Le workflow signe si les secrets existent et produit des paquets non signés
  sinon, en le disant plutôt qu'en échouant. C'est au propriétaire du dépôt de fournir les
  certificats, pas au dépôt de les contenir.
- **Le temps réel — présence et duels.** C'est tout ce qui reste, et c'est délibéré : ça se
  conçoit avant de s'écrire. `docs/RESEAU-TEMPS-REEL.md` établit les faits pour cette
  conception, et corrige au passage une affirmation fausse que je répétais : le journal de
  partie n'est PAS un format de rejeu. Il enregistre des conséquences (`pipe`, `score`,
  `death`), pas des appuis sur des boutons — il authentifie un score, il ne rejoue pas une
  partie. Ce qui est acquis, en revanche, et c'est la partie difficile : les **huit** jeux ont
  un test de déterminisme, sans quoi aucun duel n'est possible quel que soit le réseau — et
  `tests/test_replay.c` mesure désormais la propriété exacte dont un duel dépendrait, qu'aucun
  test ne couvrait : **une suite de masques de boutons, un par pas fixe, suffit-elle à
  reproduire une partie ?** Oui, 8/8, état comparé au bit près et pas seulement le score. Utile
  sans aucun duel : un jeu qui lirait une horloge, un `rand()` non semé ou un état résiduel
  entre deux parties serait signalé là.

---

## Ce que la reconstruction a appris

Quarante-trois défauts trouvés en chemin, tous instructifs.

**Deux moitiés d'un même projet peuvent être justes chacune et fausses ensemble.**
Le client et le serveur du classement ont été écrits, relus et testés séparément,
et tous leurs tests passaient. Mis bout à bout, ils ne pouvaient rien s'échanger :
l'un demandait « flappy », l'autre ne connaissait que `flappy-easy` ; l'un lisait
la graine dans un `float` là où l'autre en envoyait 63 bits ; l'un postait une
enveloppe là où l'autre refuse tout champ inconnu. Trois défauts, aucun visible
d'un seul côté. La leçon n'est pas « écrire plus de tests unitaires » — ils
étaient là et ils étaient verts — mais **faire parler les deux moitiés au moins
une fois pour de vrai**, contre la vraie base, avant de dire que ça marche.

**Un ratio oublié dans une table rend un jeu injouable sans rien casser.**
L'ellipse du missile ennemi de base de Shooter s'écrit
`{0, 0, 12.5*RATIO_SIZE_MISSILE_3, 12.5*RATIO_SIZE_MISSILE_3}` — et le ratio
vaut **0,4**. Recopier 12,5 en oubliant le 0,4 fait des balles deux fois et
demie trop grosses. Le jeu tourne, il est seulement impossible, et aucune
adresse ne le rattrape : dans un couloir de six cent quarante pixels sous un tir
en rafale, la marge de passage disparaît. C'est le genre de défaut qu'on
n'attribue jamais à la bonne cause — on croit avoir mal réglé la difficulté.

**Un labyrinthe relu à l'œil ment.** Le premier plan de Pac-Man avait ses vingt
et une lignes de la bonne longueur et toutes ses pastilles atteignables — et
l'enclos des fantômes **fermé de tous les côtés**. Les quatre fantômes naissaient
dans une poche murée, dont l'un carrément DANS un mur, et n'en sortaient jamais.
Sur une capture, ça ressemblait à un Pac-Man tranquille. Le contrôle de
connexité, qui tient en un parcours en largeur, l'a dit en une seconde ; il est
maintenant dans `tests/test_pacman.c`, avec la longueur des lignes et la
symétrie.

**Et des fantômes trop rapides ne font pas un jeu difficile, ils font une
exécution.** À 90 % de la vitesse du joueur, le joueur automatique tenait
treize secondes et marquait quarante points en une minute. La borne de 1980 les
met à 75 %, et c'est cette marge qui FAIT la poursuite. Le chiffre est
maintenant un test : il exige une survie moyenne d'au moins huit secondes.

**Comparer deux angles bruts de part et d'autre d'un tour donne 6,28 radians de
rotation là où il n'y en a aucune.** `BASE_ANGLE` vaut 3π/2 et le vaisseau
normalise son cap dans (−π, π] dès la première image : l'écart mesuré n'était
pas une rotation, c'était la normalisation. Le test de rampe s'y est fait
prendre — il annonçait une rotation quarante fois trop rapide. L'angle de départ
est maintenant normalisé à la source, et les écarts se mesurent modulo 2π.

**Un test qui mesure le silence d'une partie finie ne mesure rien.** Le test des
munitions laissait le vaisseau immobile au milieu du champ d'astéroïdes pendant
trente secondes : il mourait, `tick` sortait aussitôt, et le test constatait que
plus rien ne changeait. Il vide maintenant le terrain — il porte sur les
munitions, pas sur la survie — et vérifie explicitement que la partie tourne
encore.

**Une pièce entièrement au-dessus du plateau tenait toujours, donc la partie de
Tetris ne pouvait pas finir.** Une case au-dessus de la ligne 0 est acceptée par
construction — c'est ainsi qu'une pièce arrive. Le premier jet faisait apparaître
la pièce au-dessus et la descendait « jusqu'à ce qu'elle tienne » : sur un
plateau plein elle restait suspendue dans le vide, indéfiniment. Le joueur
automatique a survécu quinze minutes d'affilée sans que rien ne le signale, et
c'est le test « toute partie finie annonce sa fin » qui l'a dit. La pièce entre
maintenant à une position DÉFINIE — sa première ligne pleine sur la ligne 0,
centrée sur ses colonnes occupées — ce qui rend la question décidable : ou elle y
tient, ou la partie est finie.

**Et le test qui l'a trouvé m'a aussi pris en flagrant délit d'affirmation
fausse.** J'avais écrit, dans trois fichiers, qu'« une pièce géante n'est pas la
pièce normale doublée ». La mesure dit l'inverse : les 56 le sont. Ce qui ne
l'est pas, c'est le PIVOT — dans 42 cas sur 56. La conclusion tenait, la raison
était fausse, et c'est la raison qui compte : dériver les géantes donnerait la
bonne forme et la mauvaise rotation.

**Une capture headless n'était pas reproductible.** `--game=` tirait sa graine de
l'horloge, donc la même commande rendait 16 600 points d'un build et 244 100 du
suivant — ce qui a d'abord ressemblé à un défaut de calcul. Sans écran, la graine
est maintenant fixe, la même que celle de `--play-at`. Une image qu'on ne peut
pas refaire ne prouve rien.

**« Les pièces sont trop sombres » et « les textures mal choisies » étaient le
MÊME défaut, et il n'était pas dans l'éclairage.** Cinq passes successives
avaient réglé la lumière à l'œil sur des captures, chacune baissant une source
pour corriger une brûlure locale ; personne n'avait mesuré le cumul. La mesure,
une fois faite, était sans appel : sur huit points de vue, la médiane de
luminance allait de **4** à 105 sur 255, et six vues sur huit avaient plus de
**40 % de pixels quasi noirs**.

La cause n'était pas le nombre de lampes. C'était la **réflectance des
matériaux**, mesurée sur les sources de 2020 : la moquette à **0,029**, le
plafond et l'estrade à **0,002**, les poutres à 0,008. Du bitume frais réfléchit
0,04. Le plafond de cette salle était plus noir que n'importe quel matériau de
construction existant — et c'est la plus grande surface du décor.

Ce n'est pas une faute de l'auteur d'origine : **son moteur n'éclairait rien**.
`SDL_RenderCopy` affichait la texture telle quelle, donc l'ombre devait être
PEINTE dedans pour qu'une salle tamisée ressemble à une salle tamisée. Un moteur
PBR reprend cette texture et l'éclaire : la pénombre est appliquée deux fois, et
aucune quantité de lumière ne rattrape une réflectance de deux pour mille.

La preuve par l'exception était sous les yeux depuis le début : **les toilettes
étaient la seule pièce lisible de la salle** (médiane 105 contre 8 pour la borne
de classement). Leur faïence est à 0,872, parce que l'auteur ne l'avait pas
assombrie.

`texgen` sait maintenant **dé-cuire** une texture : passage en linéaire, recentrage
sur une réflectance visée, compression du contraste vers cette moyenne, et une
seconde passe qui corrige l'écart introduit par la compression — sans elle,
`--albedo=0.55` en rendait 0,39, l'inégalité de Jensen et non une imprécision
numérique. Dix-neuf textures de 2020 sont déclarées avec leur cible physique dans
`assets/CMakeLists.txt` ; les cartes CC0 de Poly Haven, qui sont d'authentiques
couleurs de base, n'y touchent pas, et les écrans, marquees et affiches non plus —
ce sont des images qu'on regarde, pas des surfaces qu'on éclaire.

Résultat mesuré, médiane de luminance par point de vue, avant → après :
allée 39 → **74**, bar 28 → **49**, billard 10 → **26**, entrée 21 → **28**,
classement 8 → **23**, borne 12 → **17**, orbite 4 → **18**. La proportion de
pixels quasi noirs tombe d'un tiers à moitié partout.

**Monter la qualité rendait la salle plus SOMBRE.** Mesuré au même cadrage :
bas 53, moyen 63, **haut 40**, ultra 73. `high` allume les ombres lancées mais
pas l'illumination globale : on retire le direct sans rendre l'indirect, ce qui
est plus faux que de n'avoir aucune ombre. L'ambiante constante — qui EST ce
stand-in — monte donc là où les ombres apparaissent. `high` passe de 40 à 55 sans
changer d'un dixième de point la proportion de pixels brûlés.

**Huit pour cent du sol du hall ne recevait rien.** La couverture, calculée
luminaire par luminaire en 1/d² borné par la portée, montrait trois taches nettes :
la bande centrale-est, le coin sud-est et le passage vers les toilettes. Trois
plafonniers de plus les ferment ; il reste 0,2 %.

**Et la mesure est maintenant dans le binaire.** Chaque capture journalise sa
moyenne, sa médiane et ses proportions d'extrêmes. « La salle est trop sombre »
est un jugement ; « la médiane vaut 8 sur 255 » est un fait, et c'est le seul des
deux qu'une régression ne peut pas contourner.

**Un jeu qui compte par PLAGES ne rentre pas dans une interface qui compte par UNITÉS.** Le
Démineur ouvre une cascade : un seul appui dévoile jusqu'à trois cents cases. Or
`ns_game_events` rend UN gain par image — ce qui suffisait à Flappy (un tuyau) et à Snake (un
fruit), donc la question ne s'était jamais posée. Résultat : le score affiché montait de cinq
fois cent, et le journal disait « une case ». Comme le serveur ne croit pas le score mais le
RECALCULE à partir du journal, une partie affichée à 1 500 points aurait été classée à 5 — sans
qu'aucun message ne le signale, l'envoi étant « au mieux ». Corrigé des deux côtés : « cell »
porte sa quantité et le barème du serveur devient proportionnel, ce qui a l'avantage de faire
porter la limite de fréquence sur les COUPS joués — que le joueur choisit — au lieu de la taille
des plages ouvertes, qu'il subit.

**Deux conséquences du même défaut, trouvées dans la foulée.** La victoire écrasait le gain de
la cascade qui l'avait produite, puisque les deux se disputaient le même drapeau. Et surtout :
**gagner ne finissait pas la partie**. `room/main.c` scelle le journal, enregistre le meilleur
score et met la partie en file d'attente sur le seul événement `die`, que seule la mort levait —
une partie gagnée disparaissait donc entièrement, meilleur score local compris. La file
d'événements se vide maintenant AVANT que la fin soit annoncée, parce qu'un gain publié après
`finish_run` n'existe pas.

**L'écran de fin annonçait « MEILLEUR 0 » alors que le journal imprimait la bonne valeur.**
`set_best` n'était appelée qu'à la relance, pour reporter le meilleur d'une partie sur la
suivante ; la PREMIÈRE partie d'une session affichait donc zéro, tous jeux confondus, alors que
`ns_scores` connaissait la valeur. Le meilleur vient maintenant du classement local à chaque
démarrage de partie, aux quatre endroits qui en démarrent une.

**Sept bornes sur dix-neuf affichaient la partie d'une autre.** `roomgen` nomme les matériaux
par jeu — `ecran_snake` — et deux bornes du même jeu partageaient donc le même. Or le moteur
allume un écran vivant en surchargeant un MATÉRIAU : jouer sur la borne Snake normale faisait
apparaître la partie sur la borne Snake hard, à l'autre bout de la salle. Le défaut datait d'A4
et ne pouvait pas se voir tant qu'un seul jeu était porté — il fallait deux bornes du même jeu
dans le même cadre pour le rencontrer. La dalle d'une borne a maintenant son propre matériau,
cloné de celui qu'elle déclare : même image d'attente, surface distincte.

**Un éventail depuis le centroïde ne triangule pas un polygone creux.** Les bouchons de
`geo_profile_extrude` étaient un éventail depuis le centroïde — juste pour un profil convexe,
faux pour tout le reste : sur un profil concave le centroïde peut tomber HORS du polygone, et les
triangles se recouvrent en restant coplanaires. Ça ne se voit presque pas à l'image, la silhouette
restant juste ; mais chaque pixel du flanc est peint plusieurs fois. Avec la silhouette en gradins
d'une borne, répétée dix-neuf fois, c'était de la surcharge de remplissage pure. Remplacé par une
découpe d'oreilles, avec repli sur l'éventail quand le polygone n'est pas simple. Le contrôle qui
l'attrape est le volume signé, et il tient maintenant dans `tests/test_shapes.c` sur un « L » —
dont le centroïde est démontrablement hors du solide.

**Et la moitié de l'image qui devenait noire n'était pas le rendu : c'était la caméra.**
`--play-at` plante le joueur sur `player_anchor`, dérivée de l'ÉCRAN. Le profil en gradins fait
reculer l'écran de 32 cm quand le panneau n'en recule que 12 : le joueur se retrouvait le nez
dans la tôle, et un flanc de borne non éclairé remplissait la moitié du cadre. J'ai d'abord
soupçonné le rasteriseur logiciel, puis le nombre de sommets, puis les matériaux — trois fausses
pistes, toutes écartées par une mesure (la même vue à trois qualités, puis à géométrie
précédente). L'ancre dérive maintenant du PANNEAU, qui est ce qu'on doit atteindre, et le tangage
de la capture est calculé depuis l'écran déclaré au lieu d'être une constante mesurée une fois.

**Trois bogues dans un chemin qu'on croyait fini, tous trouvés en le généralisant.** En
extrayant l'interface commune des mini-jeux (`games/games.h`), il a fallu nommer les événements
qu'un jeu émet — et c'est là que ça s'est vu. (1) Le serveur Go **refuse sèchement** un événement
dont le nom n'est pas dans sa table ; le client émettait « flap », « score » et « death » quand la
table de Flappy n'accepte que « pipe ». Toute partie soumise était rejetée en bloc, et personne ne
le voyait : l'envoi est « au mieux, jamais bloquant », donc un refus ne remonte nulle part. (2) La
table du serveur se contredisait : elle limitait la fréquence de « flap » — donc l'attendait — tout
en refusant tout événement absent de son barème. (3) `flappy_tick` remettait ses drapeaux
d'événement à zéro EN TÊTE, alors que `flappy_flap` est appelée depuis le gestionnaire
d'événements, donc avant la boucle de pas fixe : le battement était effacé avant d'être lu. Le son
du battement n'a jamais été joué, l'index droit n'a jamais tapé sur le bouton, et « flap » n'est
jamais entré dans le journal. Trois tests neufs les tiennent maintenant, deux en C et un en Go,
parce que le vocabulaire est écrit dans les deux langages et que rien d'autre ne les tient
d'accord.

**Un preset qui ne construit pas ne dit rien, et personne ne s'en aperçoit.** Le preset
`linux-x64-asan` ne LIAIT pas : `ns_test_render` était la seule cible de test à ne pas appeler
`nineteen_apply_sanitizers`, alors qu'elle lie un moteur instrumenté. Une ligne. Elle a suffi à
ce que le tableau ASan reste incomplet sans que rien ne l'annonce — on construisait des cibles à
l'unité, jamais l'ensemble. Une fois la ligne ajoutée, UBSan a signalé du premier coup un
**dépassement d'entier signé dans le hachage de soudure des sommets** (`tools/geo_mesh.c`), écrit
deux fois : `(gx + dx) * 73856093` en `int` déborde dès 5 000 cellules de grille, soit un demi-
mètre. En pratique il bouclait et la soudure marchait — la salle générée est d'ailleurs restée
**identique octet pour octet** après correction. Mais le dépassement signé est un comportement
indéfini, que le compilateur a le droit d'exploiter : ça tient jusqu'au jour où une optimisation
le casse, et ce jour-là le symptôme serait une salle trouée sans message. Deux fuites de notre
côté sont tombées dans la même passe : la cible de rendu de la borne de classement, jamais
détruite depuis B10, et la texture blanche 1x1 du lot de sprites — `ns_texture_white` fabrique une
texture neuve à chaque appel malgré son nom. 3 919 octets à la sortie sont devenus 271, tous dans
l'énumération ALSA de SDL sur un conteneur sans carte son.

**Un test qui ne mesure que ce qu'on ajoute ne voit pas ce qu'on a cassé.** Le nœud d'écho de
miniaudio a d'abord été monté **en série** entre les bus et la sortie, sur une lecture erronée de
son API : son `dry` ressemble à un passage direct, et c'est en réalité le gain d'entrée dans la
ligne à retard — sa sortie vaut exactement `ligne × wet`. Un bus branché dessus perdait donc son
signal direct, et devenait entièrement muet à `wet = 0`, c'est-à-dire partout dans la salle. Le
test écrit en même temps ne mesurait que la **queue** après la fin du son : il passait, sur un
mixage devenu silencieux. Ce sont les deux tests d'atténuation par distance et d'occlusion, plus
anciens, qui sont tombés — et qui ont désigné le coupable. Le montage est maintenant un
départ/retour par séparateur, et le test vérifie **aussi que le son direct survit**.

**Le garde-fou d'une arène a rapporté plus qu'un débogueur.** Le chargeur de scène dupliquait le
tampon de sommets une fois par primitive, parce que le glTF partage un seul jeu d'accesseurs
entre ses 183 primitives : 96 067 sommets devenaient 17,5 millions. L'arène a refusé net une
allocation de 843 Mio au lieu de la servir, et a nommé le coupable dans le message d'erreur.

**Un écran noir peut venir de six endroits, et les regarder un par un est le seul moyen
fiable.** La passe d'éclairage échantillonnait une cible de réflexions que rien n'avait jamais
écrite. Elle contenait de la mémoire GPU arbitraire, donc des NaN, qui traversaient toute la
couleur et ressortaient en noir après le tone mapping. C'est la visualisation des cibles
intermédiaires — un G-buffer parfait, un HDR rempli de bruit — qui a isolé le problème en deux
minutes.

**Une formule de conversion « standard » peut être exactement fausse.** La rugosité des 120
matériaux était dérivée de l'exposant spéculaire par l'équivalence Blinn-Phong/GGX classique.
La distribution des valeurs du fichier — 101 matériaux à exactement 225, maximum à 900 —
révélait un export Blender, dont l'exporteur écrit `Ns = (1 - rugosité)² × 900`. L'inversion est
donc exacte, et la formule générique donnait 0,09 là où l'auteur avait mis 0,5 : tous les murs
en miroir.

**L'unité d'une intensité lumineuse n'est pas un détail.** L'atténuation en 1/d² sans
normalisation sphérique ni rayon de source donnait plusieurs centaines de fois l'exposition
correcte près d'une applique. Les hautes lumières étaient brûlées, et aucun réglage
d'exposition ne pouvait le rattraper.

**Le nom d'un fichier est une donnée.** Le MTL ne distingue pas une moquette d'un carrelage : tout
sortait à la rugosité 0,5 par défaut. Mais l'auteur avait nommé ses textures `moquette`, `bois`,
`marbre`, `carllage_toilette`, `cuir_rouge`, `pilonne_rouge`. C'est la meilleure information
disponible, et elle suffit à donner à chaque famille une rugosité et une métallicité plausibles.

**Un rayon de source est une propriété du luminaire, pas du moteur.** Suite du point sur l'unité
d'intensité, et celui qui a coûté le plus d'allers-retours. Le rayon qui borne la décroissance en 1/d² était
une constante globale de 0,22 m — l'ampoule d'une applique. Un pavé lumineux de faux plafond fait
1,20 m : traité comme une ampoule, il portait 3 600 fois plus d'énergie sur la dalle à 5 cm que sur
le sol à 3 m. Toutes les valeurs d'intensité essayées donnaient soit un plafond carbonisé, soit un
sol noir, et souvent les deux. Aucun réglage ne rattrape un rapport de 1 à 3 600 : c'était le
modèle de source qu'il fallait corriger. Le champ tenait dans le `_pad[2]` que la structure
traînait déjà, donc sans changer un seul octet de disposition.

**Un albédo se mesure avant de s'expliquer.** La salle reconstruite est restée noire une bonne
demi-heure, et l'explication cherchée était l'éclairage. Une vingtaine de lignes de C qui moyennent
les pixels ont donné la vraie : `moquette_noire.jpg` a un albédo de 10/255, soit 4 %. Le plus
grand plan de la salle était un piège à lumière. `moquette.jpg`, dans le même dossier depuis 2020,
en a 47 — et c'est en plus le motif bordeaux d'une vraie salle d'arcade.

**Une police peut mentir sur ce qu'elle sait dessiner.** `sega.ttf` déclare les codes des
caractères accentués, mais leurs glyphes sont vides : « rallumée » s'affichait « rallum e ». Il
a fallu inspecter la table `loca` de chaque police pour savoir laquelle dessine réellement quoi.
Elle rend aussi son `E` d'une façon qui se lit comme un `C` — « CLASSEMENT » devenait
« CLASSCMCNT ».

**Un build vert ne prouve que ce qu'il exécute.** Le jeu n'avait jamais démarré sur macOS. Le
build y passait depuis deux mois, parce que la CI n'y lançait que `ctest -R core`, un test sans
GPU — et l'étape s'appelait honnêtement « hors rendu ». Le binaire n'était exécuté sur aucune
plateforme sauf Linux. La cause tenait en une ligne : le moteur annonçait à SDL savoir produire
du SPIR-V, du DXIL **et** du MSL, alors que le build ne produisait que du premier. SDL choisit
son backend d'après cette annonce : il rendait donc un périphérique Metal parfaitement valide,
puis refusait les seize shaders l'un après l'autre. Une option `NINETEEN_SHADERCROSS` existait,
était forcée à ON sur Apple, et n'était lue par rien. La correction qui compte n'est pas la
traduction MSL : c'est que le masque de formats soit maintenant **calculé à partir des blobs
réellement embarqués**, ce qui rend la faute impossible plutôt que corrigée.

**Un point de vue est une donnée qui se périme.** Deux captures de référence se sont
retrouvées *à l'intérieur* d'un meuble ajouté au palier suivant — l'allée dans une borne, puis
la vue du plafond dans la borne de classement. Le symptôme est un cadre noir, et rien ne le
dit : ni le build, ni le journal. La seconde fois, j'ai d'abord cru à une régression du
palier en cours. `roomgen` connaît à la fois l'emprise des meubles et la position des caméras :
il refuse maintenant, en nommant l'objet et en donnant les deux boîtes. La marge vaut le rayon
du corps du joueur — une caméra qui frôle une borne a déjà sa face avant en plein cadre.

**Le brouillard révèle que l'éclairage est chaud.** La première image volumétrique est sortie
entièrement blanche. L'éclairage d'une surface passe par un albédo divisé par pi — 0,25 pour un
mur clair, 0,01 pour la moquette noire ; la diffusion dans l'air n'a pas ce facteur. À intensité
égale, une source éclaire donc l'air plusieurs fois plus que le mur qu'elle éclaire, et 46
sources dans un hall de 22 m noient la salle dans son propre brouillard. Le coefficient qui
corrige cela est l'albédo de diffusion du milieu ; sa valeur vient de trois captures comparées,
et c'est dit comme tel dans le code plutôt que présenté comme une constante physique.

**Une fonction sans appelant est une fonction sans preuve.** `ns_bvh_move_capsule` existait
depuis M5, avec des commentaires soignés, et n'a jamais été appelée : le joueur traversait les
murs et volait. En la branchant, trois défauts sont apparus d'un coup. Elle renvoyait la position
des *pieds* alors que la caméra tient celle de l'*œil* — un mètre soixante-dix d'écart. Elle
n'avait aucune notion de hauteur de marche, donc une plinthe de trois centimètres arrêtait net.
Et son recollement au sol, appliqué sans regarder le signe du déplacement vertical, annulait la
première fraction de seconde de tout saut : sauter n'aurait « rien fait », sans message. Aucun de
ces trois-là n'était visible à la lecture.

**Ce qui pilote une animation compte autant que sa forme.** L'oscillation de marche était d'abord
pilotée par la vitesse *souhaitée*. Un test l'a prise en défaut sans le chercher : plaqué contre un
mur, le joueur continuait de dodeliner comme s'il avançait. La corriger pour suivre le déplacement
*réellement effectué* règle le cas du mur, celui de la marche gravie, et d'avance celui des bruits
de pas — qui, branchés sur la même phase, auraient sinon résonné dans le vide.

**Traduire un shader révèle ce qu'il n'utilise pas.** En passant `raytrace.comp` en MSL,
l'outil a refusé de continuer : le set 0 avait un trou au binding 2. L'albédo du G-buffer y était
déclaré, lié à chaque image par le moteur, et jamais échantillonné — le rebond indirect se colore
avec l'albédo du matériau du BVH. L'optimiseur SPIR-V supprimait donc la déclaration. En SPIR-V
cela ne coûtait qu'une texture liée pour rien ; en MSL, un emplacement mort décale tous les
suivants. La correction remonte jusqu'aux numéros de binding des quatre tampons de stockage, qui
suivaient les textures et devaient reculer d'un cran — sur Vulkan aussi.
