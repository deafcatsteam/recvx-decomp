# Portage PC de Code: Veronica X

Ce dossier contient tout ce qui sert à compiler le code décompilé du jeu pour PC.
Le build PS2 d'origine (`compile.py`, qui doit rester identique octet par octet)
n'est pas touché : les quelques adaptations faites dans `src/` sont derrière
`#ifdef PLATFORM_PC` et produisent exactement le même code pour la PS2.

## Compiler

Prérequis : CMake ≥ 3.20, Python 3, GCC ou Clang avec le support 32 bits
(`gcc-multilib` sous Debian/Ubuntu), et les sous-modules :

```
git submodule update --init
cmake -S . -B build-pc
cmake --build build-pc
ctest --test-dir build-pc        # tests des maths
```

Sous Windows, utiliser MSYS2/MinGW 32 bits ou WSL pour l'instant : le code
utilise des extensions GCC (bitfields 64 bits, `__attribute__`), MSVC n'est pas
encore supporté.

### Pourquoi 32 bits ?

Le jeu stocke des pointeurs 32 bits dans ses structures et dans ses fichiers de
données (modèles, motions, scripts). En gardant des pointeurs de 4 octets, le
code décompilé fonctionne tel quel. Le passage en 64 bits est possible plus tard,
mais demandera de convertir les données au chargement.

## Organisation

| Chemin | Rôle |
|---|---|
| `include/port_prefix.h` | Inclus avant chaque fichier : `PLATFORM_PC`, libc, types 64/128 bits de l'EE |
| `include/sdk/` | En-têtes de remplacement du SDK PS2 de Sony (types, structures, prototypes) |
| `src/ninja/` | Versions C des fichiers PS2 écrits en assembleur VU0 |
| `tests/` | Tests unitaires (`test_math`) |
| `tools/gen_case_links.py` | Corrige la casse des en-têtes Dreamcast/CRI (venus de Windows) |

## Où on en est

- [x] Les 140 fichiers de jeu sans assembleur compilent avec GCC (`cvx_game`)
- [x] Les maths Ninja (`ps2_NaMath.c`, `ps2_NaMatrix.c`) réécrites en C et testées
- [ ] Les 12 autres fichiers remplacés (liste `GAME_REPLACED` dans `CMakeLists.txt`)
- [ ] Bibliothèque audio CRI ADX (`src/cri/mwlib`) dans le build
- [ ] Implémentations des fonctions du SDK Sony (d'abord vides, puis réelles)
- [ ] Édition de liens complète, puis premier lancement (`main` PC + SDL)

### Fichiers PS2 encore à remplacer

| Fichier | Contenu | Remplacement prévu |
|---|---|---|
| `ps2_NinjaCnk.c`, `ps2_Vu1Strip.c`, `ps2_Vu1Scissor2.c` | Dessin des modèles via le VU1 | Rendu PC (D3D9 / OpenGL) |
| `ps2_dummy.c`, `ps2_NaDraw2D.c`, `ps2_NaView.c` | Primitives 2D/3D, caméra, double buffer | Rendu PC |
| `ps2_loadtim2.c` | Envoi des textures au GS | Upload de textures GPU (point d'accroche des packs HD) |
| `ps2_MovieFunc.c` | Vidéos MPEG2 via l'IPU | FFmpeg |
| `njplus.c`, `face_bh.c` | Skinning et animation faciale en VU0 | Traduction C (comme les matrices) |
| `expand.c` | Décompression des données | Traduction C |
| `objitm.c` | Une fonction d'objet (`bhObj005`) | Traduction C |

Fichiers qui compilent mais parlent au matériel PS2 et devront être réécrits :
`ps2_sg_*.c` (pad, carte mémoire, disque), `ps2_snddrv.c`, `sdc*.c`,
`ps2_sfd_mw.c`, `system.c` (`bhSysCallOption`).

## Règles pour la suite

- Ne pas modifier le comportement du jeu dans `src/` : un changement PC y passe
  par `#ifdef PLATFORM_PC` en gardant la version PS2 intacte.
- Les traductions d'assembleur reproduisent le résultat PS2, y compris ses
  particularités (table de sinus, racines VU0 sur |x|, division par 0 sans NaN…),
  et l'indiquent en commentaire.
- Toute traduction de maths/géométrie vient avec un test dans `tests/`.
- Les fichiers du jeu (ISO, ELF, textures, packs HD) ne sont jamais commités.
