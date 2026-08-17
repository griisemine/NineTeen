# Nineteen V15 — Feuille de route de modernisation

Objectif : donner une seconde vie au jeu — salle d'arcade plus vivante, moteur de rendu
éclairé (jusqu'au ray tracing), meilleurs sons, animations et hitbox propres, portabilité
totale, et un site web refait (graphisme + sécurité). Ce document est le plan d'ingénierie ;
il complète le travail déjà livré (`SECURITY_AUDIT.md`, `site web/hardened/`).

## État des lieux (V1.1.2)

- **Client** : C + SDL2 + OpenGL immédiat (fixed-function), ~19 000 lignes.
- **Salle** (`room/room.c`, 3 500 lignes) : hall d'arcade avec bornes, radios, écran de classement.
- **Mini-jeux** (15 emplacements de score) : Pac-Man, Snake, Flappy, Shooter, Tetris, Démineur,
  Asteroid, Piano… chacun un `.c` monolithique avec sa propre boucle.
- **Réseau** : `libWeb.c` (libcurl) → backend PHP.
- **Build** : `makefile` par plateforme, dépendances SDL vendored dans `include/`.

## Principes directeurs

1. **Portabilité d'abord** — un seul arbre de build (CMake), trois cibles (Windows/macOS/Linux),
   idéalement WebAssembly pour une démo jouable en ligne.
2. **Séparer moteur / contenu / jeux** — un cœur réutilisable, des jeux qui ne dupliquent plus la
   boucle, les assets et l'audio.
3. **Autorité serveur pour les scores** (cf. audit) — le rendu peut être somptueux, la confiance non.
4. **Améliorations incrémentales** — chaque étape produit un binaire jouable, pas un big-bang.

---

## 1. Moteur de rendu (lumières → RTX)

**Choix recommandé : `bgfx`** (abstraction multi-backend : Vulkan, Direct3D12, Metal, OpenGL,
WebGL) plutôt qu'un Vulkan brut. Raisons : portabilité immédiate (l'exigence n°1), shaders
uniques compilés par cible, courbe d'adoption raisonnable pour une reprise solo.

- **Alternative si l'on vise le ray tracing matériel** : Vulkan + extensions `VK_KHR_ray_tracing`
  (fallback rasterisation sur GPU sans RT). Plus puissant, nettement plus coûteux à maintenir.

**Pipeline d'éclairage cible :**
- Rendu PBR (metallic-roughness) + rendu HDR + tone mapping (ACES).
- Ombres douces (shadow maps cascadées) pour la salle.
- **Global illumination** : commencer par des probes / SSAO + réflexions en screen-space,
  puis, pour la couche « RTX », des réflexions et une GI ray-tracées là où le matériel le permet,
  avec repli propre.
- Les néons de bornes, les enseignes et les écrans deviennent de vraies **sources de lumière
  émissives** → la salle « respire ».

**Bornes & lieux plus vivants :**
- Passer du placage de textures plates à des **matériaux PBR** (albedo + normal + roughness +
  metallic + emissive) — c'est ce qui rend une borne « vivante ».
- Écrans de bornes = render-to-texture du mini-jeu en direct.
- Petites animations d'ambiance (scanlines d'écran, clignotements de néon, poussière volumétrique).

## 2. Pipeline d'assets & textures

- Format d'échange **glTF 2.0** pour la géométrie/matériaux ; textures compressées GPU
  (**KTX2 / Basis Universal**) → chargement rapide, faible VRAM, portable.
- Génération des normal/roughness maps pour les textures existantes (upscale + delighting).
- Atlas et streaming pour éviter les à-coups (le changelog V1.0.0 mentionnait déjà des freezes de
  chargement — à régler proprement par streaming asynchrone).

## 3. Audio d'ambiance

- Remplacer SDL_mixer par **miniaudio** (header-only, portable) ou **FMOD/Wwise** si l'on veut
  du middleware.
- **Audio spatialisé** : les radios de la salle deviennent des sources positionnelles 3D
  (atténuation par distance, occlusion simple) → l'ambiance suit le déplacement du joueur.
- Bus de mixage (musique / SFX / ambiance) avec réverbération de salle, ducking à l'entrée d'un jeu.
- Sons d'ambiance en boucle sans couture, transitions douces salle ↔ mini-jeu.

## 4. Animations & hitbox

- **Boucle à pas de temps fixe** (accumulateur) pour une physique déterministe — élimine les bugs
  de hitbox liés au framerate (l'historique en cite plusieurs : Flappy, Snake, Asteroid).
- Séparer **hitbox de collision** et **hitbox visuelle** ; les définir en données (par jeu), pas en
  dur, pour pouvoir les régler sans recompiler.
- Système d'animation par états (idle / run / hit) ; interpolation pour le rendu, logique sur le pas fixe.
- Débogage visuel des hitbox (overlay activable) — investissement rentable pour la reprise.

## 5. Architecture logicielle

- Extraire un **noyau moteur** : fenêtre/contexte, entrées, temps, rendu, audio, ressources, réseau.
- Interface commune de mini-jeu (`init / update(dt) / render / event / teardown`) — les jeux
  cessent de dupliquer la boucle et la gestion SDL.
- Remplacer les `system()`/dialogues shell par des API natives (cf. audit NIN-07).
- Langage : garder le C pour la portabilité, ou migrer le noyau en **C++17** (bgfx est C++),
  jeux progressivement portés.

## 6. Portabilité & build

- **CMake** unique + presets par plateforme ; dépendances via **vcpkg** ou sous-modules
  (SDL2/bgfx/miniaudio/curl), fini le vendoring manuel dans `include/`.
- Cibles : `windows-x64`, `macos-universal`, `linux-x64`, et **`wasm`** (Emscripten) pour une
  démo web.
- **CI GitHub Actions** : build matriciel 3 OS + `-fsanitize=address,undefined` sur Linux +
  tests unitaires (logique de jeu, sérialisation d'événements).
- Packaging : installeurs signés (Windows/macOS), AppImage/Flatpak (Linux), page de
  téléchargement du site pointant vers les artefacts de release.

## 7. Anti-cheat (la vraie correction de NIN-02 / NIN-08)

Le rendu ne change rien à la confiance. Modèle cible :

- Le client n'envoie **pas** un score, mais un **journal d'événements de partie** signé au sein
  d'une session (clé opaque CSPRNG, cf. `hardened/`).
- Le **serveur recalcule** le score à partir des événements et rejette les parties implausibles
  (débit d'événements, durée minimale, invariants par jeu).
- La protection mémoire côté client peut rester comme ralentisseur, sans être considérée comme
  une barrière.

## 8. Site web

- Sécurité : déjà amorcée dans `site web/hardened/` (PDO préparé, Argon2id, CSPRNG, CSP…).
- Graphisme : refonte front (design system arcade rétro, responsive, mode sombre), classement en
  temps réel via une petite API JSON (les `*_api.php` livrés), page de téléchargement reliée aux
  releases CI.

---

## Séquencement proposé

| Palier | Contenu | Livrable jouable |
|---|---|---|
| M1 | CMake + CI multi-OS, extraction du noyau, boucle à pas fixe | binaire identique, build moderne |
| M2 | Backend sécurisé en prod (hardened/) + client TLS rétabli | jeu + site sûrs |
| M3 | Rendu bgfx PBR/HDR, salle ré-éclairée, matériaux de bornes | salle « vivante » |
| M4 | Audio spatialisé, hitbox en données, overlay debug | ressenti V15 |
| M5 | Couche RTX (réflexions/GI ray-tracées + repli), anti-cheat serveur | vitrine |
| M6 | Cible WebAssembly + refonte front du site | seconde vie, jouable en ligne |

## Choix techniques — résumé

| Domaine | Reco | Pourquoi |
|---|---|---|
| Rendu | bgfx (option Vulkan RT) | portabilité + chemin vers RTX |
| Assets | glTF 2.0 + KTX2/Basis | standard, compressé GPU |
| Audio | miniaudio | header-only, portable, spatial |
| Build | CMake + vcpkg + Actions | un arbre, toutes plateformes |
| Web | démo Emscripten/WASM | jouable sans installer |
| Scores | autorité serveur | seul modèle anti-triche fiable |
