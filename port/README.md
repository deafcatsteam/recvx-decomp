# Portage PC de Code: Veronica X

Ce dossier contient tout ce qui sert à compiler le code décompilé du jeu pour PC.
Le build PS2 d'origine (`compile.py`, qui doit rester identique octet par octet)
n'est pas touché : les adaptations faites dans `src/` et `include/ps2/` sont
derrière `#ifdef PLATFORM_PC`, et sans `PLATFORM_PC` ces fichiers sont
identiques à la version d'origine.

## Où on en est

Le jeu entier compile et s'assemble en un programme PC (`cvx_pc`, ou
`cvx_pc.exe` sous Windows) qui lit les données directement dans l'ISO du jeu.
Avec une fausse image disque, il passe toute l'initialisation du système de
fichiers et du son ; **avec ta vraie ISO, il n'a encore jamais été lancé.**
Le rendu 2D (menus, textes, images) passe par un GS logiciel qui reproduit la
carte graphique de la PS2 ; la 3D n'est pas encore dessinée.

- [x] Tout le code C du jeu compile (les fonctions en assembleur sont mises de côté)
- [x] Maths Ninja (`ps2_NaMath.c`, `ps2_NaMatrix.c`) réécrites en C et testées
- [x] Décompression `Expand`, défilement d'UV, surface de l'eau, etc. traduits en C
- [x] Lecture du disque depuis l'ISO, archives AFS (CRI ADXF), testée
- [x] Noyau, IOP, manette, horloge 60 Hz, SDK Sony : remplacements PC
- [x] Fenêtre, clavier et manette (SDL2), builds Linux et Windows
- [ ] **Premier lancement avec la vraie ISO** (à faire chez toi, voir plus bas)
- [x] Rendu 2D : GS logiciel (`src/gs/`), testé sur des paquets de test ; reste à valider sur le jeu
- [ ] Rendu 3D natif (OpenGL, puis D3D9 pour RTX Remix), voir `ROADMAP.md`
- [ ] Skinning (`npCalcSkin`), morphing (`npTransform`), visages (`_fmCnkCalc*`)
- [ ] Son : pilote IOP `TSNDDRV` (effets, musique) et flux ADX (voix, BGM)
- [ ] Vidéos (`ps2_MovieFunc.c`, MPEG2 via l'IPU) : sautées pour l'instant
- [ ] Cartes mémoire : sauvegardes dans des fichiers

## Lancer le jeu

Il faut ta propre image du disque **Resident Evil Code: Veronica X (NTSC-U,
SLUS-20184)**. Le jeu cherche `cvx.iso` dans le dossier courant, ou le chemin
donné par la variable `CVX_ISO` :

```
CVX_ISO=/chemin/vers/cvx.iso ./cvx_pc          # Linux / WSL
```

```
set CVX_ISO=C:\Jeux\cvx.iso
cvx_pc.exe
```
(Windows, invite de commandes ; ou mets simplement l'ISO renommée `cvx.iso` à côté de l'exe.)

Variables utiles :

| Variable | Effet |
|---|---|
| `CVX_ISO` | Chemin de l'ISO |
| `CVX_HEADLESS` | Pas de fenêtre (tests) |
| `CVX_NO_VSYNC` | Ne pas attendre le 60 Hz (le jeu tourne aussi vite que possible) |

Touches (manette PS2 émulée, une manette Xbox/PS branchée marche aussi) :

| Clavier | Bouton PS2 | | Clavier | Bouton PS2 |
|---|---|---|---|---|
| Flèches | Croix directionnelle | | Espace | ✕ (action) |
| W A S D | Stick gauche | | Échap | ○ (annuler) |
| Entrée | Start | | Maj gauche | □ (courir) |
| Retour arrière | Select | | E | △ |
| Ctrl gauche | R1 (viser) | | Q | L1 |
| 1 / 3 | L2 / R2 | | F11 | Plein écran |
| | | | F12 | Capture d'écran (`cvx_screenshot_000.bmp`…) |

## Récupérer l'exécutable Windows

À chaque push, GitHub Actions (`.github/workflows/pc-port.yml`) compile et
teste le port, puis publie `cvx_pc.exe` : onglet **Actions** du dépôt →
dernier run « PC port » → **Artifacts** → `cvx_pc-windows`. L'exe n'a besoin
d'aucune DLL en plus.

## Compiler soi-même

Prérequis : CMake ≥ 3.20, Python 3, un compilateur C 32 bits, et les
sous-modules Katana et CRI (le compilateur PS2 n'est pas nécessaire) :

```
git submodule update --init include/recvx-decomp-katana include/recvx-decomp-cri
cmake -S . -B build-pc
cmake --build build-pc
ctest --test-dir build-pc        # tests : maths, disque/AFS/Expand
```

- **Linux / WSL** : `sudo apt install gcc-multilib cmake python3`, et pour avoir
  une vraie fenêtre `sudo dpkg --add-architecture i386 && sudo apt update &&
  sudo apt install libsdl2-dev:i386`. Sans SDL2 32 bits installé, CMake
  télécharge et compile SDL2 lui-même, mais sans X11/Wayland (pas de fenêtre).
- **Windows** (pas encore testé, le plus simple reste l'exe de GitHub Actions) :
  MSYS2, shell « MINGW32 » :
  `pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja python`,
  puis les commandes ci-dessus (ajouter `-G Ninja`). SDL2 est téléchargé et
  compilé automatiquement.
- **Windows depuis Linux** : `-DCMAKE_TOOLCHAIN_FILE=port/cmake/mingw-w64-i686.cmake`
  (paquets `gcc-mingw-w64-i686 g++-mingw-w64-i686`).

MSVC n'est pas supporté : le code utilise des extensions GCC.

### Pourquoi 32 bits ?

Le jeu stocke des pointeurs 32 bits dans ses structures et dans ses fichiers de
données (modèles, motions, scripts). En gardant des pointeurs de 4 octets, le
code décompilé fonctionne tel quel. Le passage en 64 bits est possible plus tard,
mais demandera de convertir les données au chargement.

## Comment c'est construit

Le code du jeu tourne tel quel ; tout ce qui touchait au matériel PS2 passe par
une couche « plateforme » écrite pour le PC.

| Chemin | Rôle |
|---|---|
| `include/port_prefix.h` | Inclus avant chaque fichier du jeu : `PLATFORM_PC`, libc, `long` sur 8 octets comme sur PS2, types 128 bits |
| `include/sdk/` | En-têtes de remplacement du SDK PS2 de Sony ; les registres matériels pointent vers de la mémoire ordinaire |
| `src/gs/` | GS logiciel : mémoire vidéo au format PS2, paquets GIF, DMA, dessin |
| `src/main/` | Point d'entrée PC (`pc_main.c`) et fenêtre/entrées SDL2 (`pc_window.c`) |
| `src/platform/pc_disc.c` | Lecture de l'ISO (ISO 9660) secteur par secteur, comme le lecteur DVD |
| `src/platform/pc_iop.c` | Mémoire et RPC de l'IOP, modèle minimal du pilote son |
| `src/platform/pc_kernel.c` | Noyau EE : sémaphores, interruptions V-blank, cadence 60 Hz |
| `src/platform/pc_sdk.c` | DVD, GS, DMA, manette, carte mémoire |
| `src/platform/pc_vu0.c` | Fonctions vectorielles `libvu0` en C |
| `src/audio/pc_adx.c` | CRI ADXF (fichiers et archives AFS) ; ADXT (flux audio) muet |
| `src/game/pc_game_asm.c` | Fonctions du jeu en assembleur : traduites en C, ou vides en attendant le rendu |
| `src/game/pc_movie.c` | Lecteur vidéo : chaque vidéo se termine tout de suite |
| `src/ninja/` | Maths Ninja en C (remplacent les fichiers VU0) |
| `src/host/` | Services du système (temps), compilés sans les réglages du jeu |
| `tests/` | `test_math`, `test_disc` (+ `make_test_iso.py`, une ISO de test sans données du jeu) |
| `tools/gen_case_links.py` | Corrige la casse des en-têtes Dreamcast/CRI (venus de Windows) |

Dans les fichiers du jeu, une fonction en assembleur est entourée de
`#ifndef PLATFORM_PC` et sa version PC est dans `src/game/` ; un petit bloc
d'assembleur au milieu d'une fonction C est remplacé sur place
(`#ifdef PLATFORM_PC` … `#else` asm `#endif`). Seuls `ps2_NaMath.c`,
`ps2_NaMatrix.c`, `ps2_MovieFunc.c` et `gcc_wrapper.c` sont remplacés en entier
(liste `GAME_REPLACED` dans `CMakeLists.txt`).

### Ce qui reste vide en attendant le rendu

`Ps2AddPrim3D*`, `Ps2AddOT`, `loadImage`, `njCnkCvVnPs2`, `njCnkCsUvh/Uvn`,
les fonctions `vu1*` de `ps2_Vu1Strip.c` et le découpage de `ps2_Vu1Scissor2.c`.
Le jeu prépare ses paquets GS normalement (dans `ps2_dummy.c`, `ps2_NinjaCnk.c`…) :
le futur moteur de rendu partira de là.

## Règles pour la suite

- Ne pas modifier le comportement du jeu dans `src/` : un changement PC y passe
  par `#ifdef PLATFORM_PC` en gardant la version PS2 intacte.
- Les traductions d'assembleur reproduisent le résultat PS2, y compris ses
  particularités (table de sinus, racines VU0 sur |x|, division par 0 sans NaN…),
  et l'indiquent en commentaire.
- Toute traduction de maths/géométrie vient avec un test dans `tests/`.
- Le code PC n'écrit pas `long long` (le mot `long` est redéfini) : utiliser
  `int64_t` / `uint64_t`, et inclure les en-têtes système dans `port_prefix.h`
  ou dans un fichier de `src/host/`.
- Les fichiers du jeu (ISO, ELF, textures, packs HD) ne sont jamais commités.
