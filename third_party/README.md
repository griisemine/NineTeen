# Dépendances tierces

Deux régimes, choisis pour que `git clone && cmake --preset …` fonctionne sans surprise.

## Vendorées (header-only, figées dans le dépôt)

Petites, stables, sans build system : les copier rend la compilation **hermétique** — pas de
réseau nécessaire, pas de version qui dérive sous les pieds dans cinq ans.

| Bibliothèque | Rôle | Licence |
|---|---|---|
| `miniaudio/miniaudio.h` | audio : mixage, spatialisation 3D, décodage WAV/MP3 | MIT-0 / domaine public |
| `stb/stb_image.h` | lecture PNG/JPEG (outils d'assets) | MIT / domaine public |
| `stb/stb_image_write.h` | écriture PNG (captures d'écran, maps générées) | MIT / domaine public |
| `stb/stb_image_resize2.h` | redimensionnement Lanczos/Mitchell (upscale des textures) | MIT / domaine public |
| `cgltf/cgltf.h` | lecture glTF 2.0 (scène de la salle) | MIT |
| `jsmn/jsmn.h` | lecture JSON (hitbox, config de scène) | MIT |

Ces fichiers ne sont **pas modifiés**. Toute adaptation passe par un `#define` de configuration
dans le `.c` qui les instancie (voir `engine/*/…_impl.c`).

## Récupérées à la configuration (CMake `FetchContent`, version épinglée)

Trop grosses pour être vendorées, et elles ont leur propre build system.

| Bibliothèque | Rôle | Épinglage |
|---|---|---|
| **SDL3** | fenêtre, entrées, **API GPU** (Vulkan / Metal / D3D12) | tag dans `cmake/Dependencies.cmake` |

Pour compiler hors-ligne ou contre une version système, voir les options
`NINETEEN_SDL3_LOCAL_DIR` et `NINETEEN_USE_SYSTEM_SDL3` dans `cmake/Dependencies.cmake`.

## Ce qui a disparu par rapport à la V1

L'ancien `legacy/include/` contenait SDL2 recopié à la main, des `.o` compilés commités, et un
`include/SDL2/` partiellement ignoré par git — impossible à reconstruire à l'identique. Tout cela
est remplacé par les deux régimes ci-dessus.
