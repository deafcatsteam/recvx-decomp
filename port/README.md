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
Le rendu (menus, textes, images, et maintenant la 3D) passe par un GS logiciel
qui reproduit la carte graphique de la PS2. Les vidéos sont décodées avec FFmpeg.

- [x] Tout le code C du jeu compile (les fonctions en assembleur sont mises de côté)
- [x] Maths Ninja (`ps2_NaMath.c`, `ps2_NaMatrix.c`) réécrites en C et testées
- [x] Décompression `Expand`, défilement d'UV, surface de l'eau, etc. traduits en C
- [x] Lecture du disque depuis l'ISO, archives AFS (CRI ADXF), testée
- [x] Noyau, IOP, manette, horloge 60 Hz, SDK Sony : remplacements PC
- [x] Fenêtre, clavier et manettes (SDL2, branchement à chaud, vibration), builds Linux et Windows
- [ ] **Premier lancement avec la vraie ISO** (à faire chez toi, voir plus bas)
- [x] Rendu 2D : GS logiciel (`src/gs/`), testé sur des paquets de test ; reste à valider sur le jeu
- [x] Rendu 3D par le GS logiciel : sommets, éclairage, découpage traduits du VU0 (`src/game/pc_render3d.c`), testé ; reste à valider sur le jeu
- [x] Skinning (`npCalcSkin`) et morphing (`npTransform`), testés
- [x] Animation des visages (`_fmCnkCalc*`) : muscles, mâchoire, langue et yeux
- [x] Rendu par la carte graphique (OpenGL 3.3, `renderer = opengl` dans `cvx.ini`), jusqu'à 4 fois la résolution de la PS2 (sans liseré de la lettre voisine autour du texte) ; comparé pixel par pixel au rendu logiciel dans les tests ; testé sur le jeu (RTX 4070 SUPER, ×4)
- [x] Écran large 16:9 (`widescreen = yes`) : en jeu, la 3D montre plus sur les côtés sans être déformée et le texte garde sa forme ; les menus restent en 4:3 au milieu ; testé (calculs, zone visible, texte en ×4) ; reste à valider sur le jeu
- [x] 60 images par seconde en jeu (`fps60 = yes`, essai) : entre deux images du jeu, une image avec les modèles 3D (personnages, décor, caméra qui bouge) à mi-chemin ; testé (logiciel et carte graphique) ; reste à valider sur le jeu
- [ ] Rendu 3D natif (la vraie scène 3D, pour D3D9 et RTX Remix), voir `ROADMAP.md`
- [x] Musique et voix : flux ADX décodés (`src/audio/pc_adx.c`), testés sur des ADX synthétiques ; reste à valider sur le jeu
- [x] Bruitages et ambiances : remplaçant du pilote IOP `TSNDDRV` qui joue les banques Sony HD/BD et les séquences SQ (adapté de recvx-vita), testé ; reste à valider sur le jeu
- [x] Réverbération des pièces (`SdrSetRev`) : l'écho « Hall » du SPU2 refait en logiciel (`src/audio/pc_spu2rev.c`), testé ; reste à valider sur le jeu
- [x] Vidéos `.PSS` : image MPEG-2 (FFmpeg) et son, testées sur une vidéo synthétique ; reste à valider sur le jeu
- [x] Vidéos HD de remplacement (`movies/MV_000.mp4`…), affichées à leur résolution, voir plus bas
- [x] Textures HD : export de toutes les textures du disque sans jouer, et remplacement par des versions refaites (avec la carte graphique), voir plus bas ; testé sur des fichiers faits comme ceux du jeu ; reste à valider sur le jeu
- [x] Moteur de son (`src/host/pc_sound.c`) : voix mélangées sur le fil audio
- [x] Son 3D (`sound_3d`) : les bruits placés dans la pièce (pas, armes, ennemis, objets, événements) sont mis autour de toi, au casque (devant, derrière, côtés) ou sur toute la largeur des enceintes ; testé (sens gauche/droite du jeu, délai entre les oreilles) ; reste à valider sur le jeu
- [x] Carte mémoire : les sauvegardes vont dans le dossier `saves` à côté de l'exe, testé ; reste à valider sur le jeu
- [x] Fichier de réglages `cvx.ini` (plein écran, taille de fenêtre, touches, chemins, son, vibration), testé
- [x] Fenêtre de réglages avant le jeu (choix de l'ISO, image, son, manette, touches), enregistrée dans `cvx.ini` ; reste à valider sur Windows

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
(Windows, invite de commandes ; ou mets simplement l'ISO renommée `cvx.iso` à côté de l'exe,
ou choisis-la dans la fenêtre de réglages, voir plus bas.)

## Fenêtre de réglages

Avant le jeu, une fenêtre s'ouvre avec quatre pages :

- **Général** : l'image du disque (bouton *Parcourir…*, ou glisse le `.iso`
  sur la fenêtre), plein écran, taille de la fenêtre, et si cette fenêtre
  doit s'ouvrir au démarrage ;
- **Image** : dessin par le processeur ou par la carte graphique, finesse
  (×1 à ×4), lissage ;
- **Son et manette** : son, vibration, et les manettes branchées ;
- **Touches** : clique sur une touche puis appuie sur la nouvelle (clic
  droit : aucune touche), *Touches d'origine* pour tout remettre.

*Jouer* lance le jeu, *Quitter* ferme tout ; dans les deux cas, les réglages
sont enregistrés dans `cvx.ini`. Tout se fait aussi à la manette (croix
directionnelle, A, LB / RB pour les pages, Start pour jouer).

Si tu désactives la fenêtre, le jeu démarre directement ; pour la revoir,
garde **Maj** enfoncée en lançant le jeu (ou lance `cvx_pc --config`). Elle
s'ouvre toujours quand l'image du disque est introuvable.

## Réglages (`cvx.ini`)

Au premier lancement, le jeu crée un fichier `cvx.ini` à côté de l'exe, avec
tous les réglages désactivés (une ligne qui commence par `;` ne compte pas).
La fenêtre de réglages y écrit ce que tu changes ; tu peux aussi l'ouvrir
avec le Bloc-notes, enlever le `;` devant ce que tu veux changer, et relancer
le jeu :

| Réglage | Effet |
|---|---|
| `iso = C:\Jeux\cvx.iso` | Chemin de l'ISO (par défaut `cvx.iso` à côté de l'exe) |
| `saves = saves` | Dossier des sauvegardes |
| `movies = movies` | Dossier des vidéos HD de remplacement |
| `fullscreen = yes` | Démarrer en plein écran (F11 change toujours) |
| `window = 1280x960` | Taille de la fenêtre au démarrage |
| `filter = smooth` | Image lissée quand elle est agrandie (`sharp`, par défaut : pixels nets) |
| `fps60 = yes` | 60 images par seconde en jeu au lieu de 30 (essai) : entre deux images du jeu, une image où les personnages, le décor et la caméra sont à mi-chemin. Les effets (particules, flammes) et les textes restent à 30. Rien n'est inventé quand la caméra change de plan, ni dans les menus et les vidéos. Demande environ deux fois plus de travail à la carte graphique ; `cvx_log.txt` dit toutes les 30 secondes combien d'images en ont profité (lignes `fps60:`) |
| `widescreen = yes` | Écran large 16:9 en jeu (pièces, scènes en 3D, portes) : on voit plus sur les côtés. Les menus, les vidéos et l'écran titre, dessinés pour le 4:3, restent en 4:3 au milieu. Fenêtre par défaut : 1600x900 |
| `renderer = opengl` | Dessin par la carte graphique au lieu du processeur (il faut OpenGL 3.3, présent sur toute carte depuis 2010 ; sinon le jeu reprend le dessin par le processeur et le dit dans `cvx_log.txt`) |
| `upscale = 2` | Avec `renderer = opengl` : résolution 2, 3 ou 4 fois celle de la PS2 (1 par défaut, l'image exacte de la PS2) |
| `textures = dump` | Avec `renderer = opengl` : `hd` (par défaut) remplace les textures par celles du dossier `textures/replace` s'il y en a ; `dump` exporte en plus celles du jeu dans `textures/dump` ; `original` garde celles de la PS2. Voir « Textures en HD » |
| `sound = no` | Pas de son |
| `sound_3d = headphones` | Son 3D en jeu : `headphones` (casque : on entend si un bruit vient de devant, de derrière ou d'un côté), `speakers` (enceintes : les bruits vont d'un bout à l'autre), `off` (par défaut, comme la PS2) |
| `vibration = no` | Pas de vibration de la manette |
| `launcher = no` | Pas de fenêtre de réglages au démarrage (Maj enfoncée en lançant le jeu : elle s'ouvre quand même) |
| `key_cross = Space` | Touches du clavier, une ligne par bouton (`key_up`, `key_stick_left`, `key_l1`, `key_start`, `key_fast_forward`…, toutes listées dans le fichier). Plusieurs touches : `Space, Return` ; rien après le `=` : aucune touche |

Les variables du tableau suivant peuvent aussi y être écrites telles quelles
(`CVX_GS_THREADS = 4`). Une variable réglée à la main l'emporte sur le
fichier. Un réglage mal écrit est signalé au début de `cvx_log.txt` (lignes
`config:`). Pour revenir aux réglages d'origine, supprime `cvx.ini`.

Variables utiles :

| Variable | Effet |
|---|---|
| `CVX_ISO` | Chemin de l'ISO |
| `CVX_HEADLESS` | Pas de fenêtre (tests) |
| `CVX_NO_VSYNC` | Ne pas attendre le 60 Hz (le jeu tourne aussi vite que possible) |
| `CVX_QUIET` | Pas de ligne d'état chaque seconde dans la console |
| `CVX_NO_LOG` | Ne pas écrire `cvx_log.txt` |
| `CVX_GS_THREADS` | Nombre de cœurs pour le dessin (par défaut : tous, 8 au plus) |
| `CVX_GS_NO_BATCH` | Dessine chaque triangle à part au lieu de les regrouper (plus lent ; pour vérifier si un défaut d'image vient du dessin en parallèle) |
| `CVX_NO_AUDIO` | Pas de son |
| `CVX_MOVIES` | Dossier des vidéos de remplacement (par défaut `movies`, puis `MOVIE`) |
| `CVX_SAVES` | Dossier de la carte mémoire (par défaut `saves`) |
| `CVX_TEXTURES` | Dossier des textures exportées et de remplacement (par défaut `textures`) |

**Sauvegardes** : la carte mémoire de la fente 1 est le dossier `saves` à
côté de l'exe (créé à la première sauvegarde), avec les fichiers du jeu
comme sur une carte PS2 : `saves/BASLUS-20184/SAVEDATA-00`… Pour garder
tes parties, copie ce dossier. Chaque fichier est remplacé d'un coup quand
le jeu sauvegarde, donc un plantage pendant la sauvegarde ne l'abîme pas.

Tout ce qui s'affiche dans la console est aussi écrit dans `cvx_log.txt`, à
côté de l'exe. En cas de plantage, la console indique où était le jeu et
reste ouverte jusqu'à un appui sur Entrée : envoie ce fichier pour signaler
un problème.

Touches par défaut (manette PS2 émulée ; elles se changent dans `cvx.ini` ;
pour les vraies manettes, voir plus bas) :

| Clavier | Bouton PS2 | | Clavier | Bouton PS2 |
|---|---|---|---|---|
| Flèches | Croix directionnelle | | Espace | ✕ (action) |
| W A S D | Stick gauche | | Échap | ○ (annuler) |
| Entrée | Start | | Maj gauche | □ (courir) |
| Retour arrière | Select | | E | △ |
| Ctrl gauche | R1 (viser) | | Q | L1 |
| 1 / 3 | L2 / R2 | | F11 | Plein écran |
| Tab (maintenu) | Avance rapide | | F12 | Capture d'écran (`cvx_screenshot_000.bmp`…, à la résolution de la carte graphique avec `renderer = opengl`) |
| Entrée, Retour arrière ou Échap | Passer une vidéo (Start, Select ou ○ à la manette) | | | |
| | | | F9 | Relit les textures de `textures/replace` (pour voir tout de suite celles que tu viens de refaire) |
| | | | F10 | Enregistre une image pour le débogage graphique (`cvx_gsdump_000.bin`, environ 5 Mo ; la rejouer avec `gs_replay`, ou `CVX_REPLAY_SCALE=4 gs_replay …` pour la dessiner par la carte graphique en ×4) |

Manettes : Xbox, PlayStation (DualShock 4, DualSense), Switch Pro et la
plupart des autres marchent, branchées avant ou pendant le jeu, en USB ou en
Bluetooth. On peut en débrancher une et en brancher une autre à tout moment ;
si plusieurs sont branchées, toutes contrôlent le personnage. Le bouton du
bas (A sur Xbox, ✕ sur PlayStation, B sur Switch) est toujours ✕, et les
gâchettes sont L2/R2. La vibration du jeu passe sur la manette (si l'option
du jeu est activée). Une manette que SDL ne connaît pas est lue avec la
disposition des manettes USB génériques ; si ses boutons sont mélangés, mets
un fichier `gamecontrollerdb.txt`
([liste communautaire](https://github.com/mdqinc/SDL_GameControllerDB)) à
côté de l'exe. `cvx_log.txt` indique chaque manette branchée (lignes `pad:`).

## Vidéos en HD (remplacement)

Une vidéo du jeu peut être remplacée par une version agrandie : mets un
fichier du même nom dans un dossier `movies` ou `MOVIE` à côté de l'exe,
par exemple `MOVIE/MV_000.mkv` pour `MOVIE/MV_000.PSS` (les autres fichiers
du dossier, comme des `.mpg` ou `.PSS`, sont ignorés). Le portage l'affiche à sa
propre résolution (jusqu'à 3840×2160) à la place de l'image d'origine. Le
**son** reste celui du `.PSS` d'origine, ainsi que le rythme et la fin de la
vidéo : la version HD n'a donc pas besoin de son, mais elle doit garder
**la même durée et le même nombre d'images par seconde** (29,97).

Formats lus : MP4, MKV, WebM, MOV, AVI ; vidéo H.264, H.265/HEVC, VP9, VP8,
MPEG-4, ProRes, FFV1. La console indique `movie: replacement ...` quand un
fichier est trouvé. Ces fichiers sont à toi : ils ne vont jamais sur GitHub.

Fabriquer les vidéos agrandies, gratuitement, avec **Video2X** (le plus
simple) :

1. Copie le dossier `MOVIE` de l'ISO ailleurs (par exemple `MOVIE_HD`).
   Dans cette copie, renomme les `.PSS` en `.mpg` pour que les logiciels de
   vidéo les reconnaissent : clique dans la barre d'adresse de l'explorateur,
   tape `cmd`, Entrée, puis `ren *.PSS *.mpg`. (Le son des `.PSS` n'est pas
   lu par ces logiciels : c'est normal, il n'est pas utile ici.)
2. Installe Video2X (gratuit, https://github.com/k4yt3x/video2x, page
   *Releases*, l'installateur Windows). Il utilise la carte graphique.
3. Ajoute les fichiers `.mpg`, choisis **Real-ESRGAN** avec le modèle
   **realesrgan-plus** (×4, fait pour les images réalistes), sortie en MP4.
   **N'active pas l'interpolation d'images** (RIFE) : le nombre d'images par
   seconde doit rester le même.
4. Renomme chaque résultat comme l'original (`MV_000.mkv`, …) et mets-les
   dans `MOVIE` (ou `movies`) à côté de `cvx_pc.exe`. Video2X ajoute
   `.realesrgan` au nom : pour l'enlever d'un coup, tape `powershell` dans
   la barre d'adresse du dossier, Entrée, puis :

   ```
   Get-ChildItem *.realesrgan.* | Rename-Item -NewName { $_.Name -replace '\.realesrgan','' }
   ```

Commence par une petite vidéo (`MV_004`, 5 Mo) pour voir le résultat et le
temps que ça prend. Avec **ComfyUI**, c'est aussi possible (nœuds *Load
Video* et *Video Combine* de VideoHelperSuite, *Upscale Image (using
Model)* avec un modèle ×4) ; il faut alors régler la sortie à 29,97 images
par seconde et traiter les longues vidéos par morceaux (ComfyUI garde
toutes les images en mémoire).

## Textures en HD

Les textures du jeu (murs, sols, personnages, objets, menus) sont petites :
souvent 128×128 pixels ou moins. Le portage peut les **exporter** en images
PNG, que tu agrandis (avec une IA, ou à la main), puis il les **remplace**
en jeu par tes versions. Il faut le dessin par la **carte graphique**
(`renderer = opengl`) ; les textures refaites sont lissées avec des
*mipmaps* et un filtrage anisotrope (×16), donc nettes de près et sans
scintillement de loin.

Chaque texture a un code de 16 caractères calculé d'après son image (par
exemple `8810e52ac9c5563d`), qui lui sert de nom. Une texture refaite peut
avoir n'importe quelle taille (en gardant les proportions) et n'importe quel
nom, du moment qu'il **finit par ce code** avant `.png` ; elle peut être dans
des sous-dossiers de `textures/replace`.

### 1. Exporter les textures

**Toutes d'un coup, sans jouer** : dans la fenêtre de réglages, onglet
**Image**, bouton **Extraire les textures de l'ISO** (ou `cvx_pc.exe
--extract-textures`). Le portage lit tous les fichiers du disque (pièces,
personnages, ennemis, objets, menus, documents, cartes) et passe chaque
texture par le code du jeu lui-même, comme s'il la chargeait en jeu, pour
lui donner le code qu'elle aura en jeu. Elles arrivent dans
`textures/dump`, sous un nom comme `128x64_8810e52ac9c5563d.png` (sa
taille, puis son code), chacune une seule fois. Compte quelques minutes ;
`cvx_log.txt` dit combien il en a trouvé dans chaque fichier (lignes
`textures:`).

Quelques textures ne peuvent être connues qu'en jeu : celles dont le jeu
change les couleurs en jouant (fondus de palette). Pour celles-là, mets
*Textures* = **HD + exporter** (`textures = dump` dans `cvx.ini`) : en
jouant, chaque texture affichée qui n'est pas encore dans `textures/dump`
y est ajoutée. Ce n'est utile que si tu remarques en jeu une texture
restée d'origine.

Ne sont pas exportées : les images que le jeu dessine lui-même (ombres,
reflets, flous, fondus) et les vidéos (voir « Vidéos en HD »).

### 2. Trier

Toutes ne méritent pas le même traitement. Crée trois dossiers à côté :

- **à agrandir par l'IA** : décors, personnages, objets, ennemis (le gros
  du travail) ;
- **à refaire à la main** : celles avec du **texte** (documents, affiches,
  menus, lettres de la police) : l'IA déforme les lettres. Le plus propre
  est de les redessiner dans GIMP (gratuit) ou Photoshop, ou d'agrandir par
  l'IA puis de retaper le texte par-dessus ;
- **à laisser** : les toutes petites (8×8, 16×16), les couleurs unies, les
  dégradés, les halos de lumière et de fumée. Les agrandir n'apporte rien et
  peut créer des défauts : ne les mets simplement pas dans `replace`.

### 3. Agrandir avec l'IA

Le meilleur outil gratuit pour ça est **chaiNNer**
(https://github.com/chaiNNer-org/chaiNNer, page *Releases*, l'installateur
Windows ; au premier lancement, il propose d'installer PyTorch : accepte,
il utilise la carte graphique). Il garde la **transparence** (grilles,
feuillages, cheveux) et traite un dossier entier d'un coup en gardant les
noms.

1. Télécharge un ou deux modèles ×4 sur https://openmodeldb.info (fichiers
   `.pth` ou `.safetensors`). Bons points de départ pour ce jeu (textures
   réalistes, en basse définition) : **4x-UltraSharp** (le plus courant,
   net) et **4x_NMKD-Siax_200k** (plus doux, moins d'artefacts). Il y en a
   d'autres faits pour les textures de jeux : cherche `texture` sur le site.
2. Dans chaiNNer, fais la chaîne : **Load Images** (le dossier « à
   agrandir ») → **Load Model** + **Upscale Image** → **Save Image** (format
   PNG, dossier de sortie, nom = celui d'origine).
3. **Essaie d'abord sur une dizaine de textures** d'une même pièce avec
   chaque modèle, mets-les dans `textures/replace`, regarde en jeu (F9 les
   recharge sans quitter), et garde le modèle qui te plaît. Puis lance tout.

Conseils pour le meilleur résultat :

- **×4** suffit : au-delà, c'est plus lourd sans être plus beau à l'écran.
  Le jeu garde jusqu'à 1,5 Go de textures refaites en mémoire de la carte
  graphique et libère celles qui ne servent plus.
- Les textures des PS2 ont souvent un léger quadrillage de points
  (*tramage*) ou des pixels flous. Si l'IA les agrandit au lieu de les
  effacer, passe d'abord un modèle de nettoyage **1x** (cherche `dither` ou
  `denoise` sur openmodeldb), puis le modèle ×4.
- Les murs et sols se **répètent** : l'IA ne le sait pas, et une ligne peut
  apparaître à chaque raccord. Si tu en vois une, refais cette texture en
  ajoutant un bord répété avant l'agrandissement et en le coupant après
  (dans chaiNNer, nœud **Pad** en mode *Wrap* avant, **Crop** après, de 4
  fois la taille du bord).
- Garde l'ambiance : le jeu est sombre et un peu granuleux. Un modèle qui
  « lisse » trop donne un aspect plastique ; compare toujours en jeu.
- Garde **les mêmes proportions** que l'original (128×64 → 512×256).

### 4. Installer

Mets les PNG refaits dans `textures/replace` (à côté de `cvx_pc.exe`), avec
des sous-dossiers si tu veux (`textures/replace/manoir/…`), en gardant le
code à la fin du nom. Mets *Textures* = **HD** (ou laisse **HD +
exporter** pour continuer à exporter). Au lancement, `cvx_log.txt` dit
combien de remplacements sont trouvés (ligne `textures: … replacements`) ;
F9 en jeu relit le dossier.

Si une texture a deux codes (exportée deux fois presque pareille), refais
les deux ou copie la même image sous les deux noms. Les packs de textures
faits pour PCSX2 n'utilisent pas les mêmes codes : ils ne marchent pas tels
quels. Tes textures et les exportées sont à toi : elles ne vont jamais sur
GitHub.

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
| `src/platform/pc_iop.c` | Mémoire et RPC de l'IOP ; les requêtes du pilote son vont à `src/audio/pc_snddrv.c` |
| `src/platform/pc_kernel.c` | Noyau EE : sémaphores, interruptions V-blank, cadence 60 Hz |
| `src/platform/pc_sdk.c` | DVD, GS, DMA, manette, carte mémoire |
| `src/platform/pc_vu0.c` | Fonctions vectorielles `libvu0` en C |
| `src/audio/pc_adx.c` | CRI ADXF (fichiers et archives AFS) ; ADXT : décodage des flux ADX (musique, voix) |
| `src/audio/pc_snddrv.c` | Remplaçant du pilote son `TSNDDRV` : banques, bruitages, séquences, état renvoyé au jeu |
| `src/audio/pc_hsyn.c`, `pc_hseq.c` | Synthétiseur (banques Sony HD/BD, ADPCM, enveloppes du SPU2) et lecteur de séquences SQ, adaptés de [recvx-vita](https://github.com/shoui520/recvx-vita) (licence MIT) |
| `src/audio/pc_spu2rev.c` | Réverbération du SPU2 (préréglage « Hall » que le jeu règle pièce par pièce), d'après la description de psx-spx |
| `src/game/pc_game_asm.c` | Fonctions du jeu en assembleur : traduites en C, ou vides en attendant le rendu |
| `src/game/pc_movie.c` | Lecteur vidéo : lit le `.PSS` sur le disque, sépare image et son |
| `src/game/pc_render3d.c` | 3D : couleurs des sommets, découpage, paquets GS (code VU0 traduit) |
| `src/host/pc_video.c` | Décodage MPEG-2 avec FFmpeg (LGPL, téléchargé et compilé par CMake avec le strict nécessaire), et lecture des vidéos de remplacement |
| `src/host/pc_audio.c` | Sortie son : mélange du moteur de son et de la file des vidéos, jouée par la fenêtre (SDL) |
| `src/host/pc_memcard.c` | Carte mémoire dans un dossier (`saves`) |
| `src/host/pc_sound.c` | Moteur de son : voix (flux ou sources), rééchantillonnage, volume, panoramique, position 3D |
| `src/ninja/` | Maths Ninja en C (remplacent les fichiers VU0) |
| `src/host/` | Services du système (temps), compilés sans les réglages du jeu |
| `tests/` | `test_math`, `test_disc` (+ `make_test_iso.py`, une ISO de test sans données du jeu), `test_3d`, `test_movie` (+ `make_test_pss.py`, une vidéo synthétique), `test_sound` (+ `make_test_adx.py`, des ADX synthétiques), `test_snddrv` (une banque Sony fabriquée par le test) |
| `tools/gen_case_links.py` | Corrige la casse des en-têtes Dreamcast/CRI (venus de Windows) |

Dans les fichiers du jeu, une fonction en assembleur est entourée de
`#ifndef PLATFORM_PC` et sa version PC est dans `src/game/` ; un petit bloc
d'assembleur au milieu d'une fonction C est remplacé sur place
(`#ifdef PLATFORM_PC` … `#else` asm `#endif`). Seuls `ps2_NaMath.c`,
`ps2_NaMatrix.c`, `ps2_MovieFunc.c` et `gcc_wrapper.c` sont remplacés en entier
(liste `GAME_REPLACED` dans `CMakeLists.txt`).

### Comment la 3D est dessinée

Sur PS2, les modèles « chunk » passent par le VU0 (sommets, lumières), puis par
un microprogramme VU1 pour la plupart des modes de dessin. Le port fait tout
sur le processeur, avec les versions C que le jeu a de ces fonctions de bandes
de triangles (`ps2_Vu1Strip.c`, que la PS2 utilise pour certains modes) ;
elles produisent les mêmes primitives GS. Les sommets en espace caméra, les
matrices et les lumières passent tous par `njCnkCvVnPs2` et
`src/game/pc_render3d.c` : c'est là que le futur rendu par la carte graphique
(et RTX Remix) récupérera la vraie scène 3D.

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
