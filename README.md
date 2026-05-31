# Black Pearl Engine

## LEGO Pirates of the Caribbean - Mod Menu

**Black Pearl Engine** est un mod menu pour LEGO Pirates of the Caribbean: The Video Game, fonctionnant sous Wine/Proton (Linux).

## Architecture

Le projet utilise deux approches d'injection:

### 1. DINPUT8 Proxy (Recommandé)
- `DINPUT8.dll` - Proxy qui intercepte les appels DirectInput
- Hook inline sur `EndScene` via création d'un device temporaire
- Pas de modification de la vtable du device (évite les crashs DXVK)

### 2. D3D9 Proxy (Alternative)
- `d3d9.dll` - Proxy qui intercepte `Direct3DCreate9`
- Vtable swap sur `EndScene` avec copie étendue (512 entrées)

## Compilation

```bash
# DINPUT8 Proxy (recommandé)
i686-w64-mingw32-g++ -shared -O2 -s -o DINPUT8.dll src/dllmain.c \
    -ld3d9 -ld3dx9 -lwinmm -static-libgcc -static-libstdc++ -Wl,--kill-at

# D3D9 Proxy (alternatif)
i686-w64-mingw32-g++ -shared -O2 -s -o d3d9.dll src/d3d9_proxy.c \
    -ld3d9 -ld3dx9 -lwinmm -static-libgcc -static-libstdc++
```

## Installation

1. Copier `DINPUT8.dll` dans le dossier du jeu
2. Configurer Heroic:
   ```
   WINEDLLOVERRIDES="dinput8=n,b"
   ```
3. Lancer le jeu

## Contrôles

- **F1** - Ouvrir/Fermer le menu
- **F2** - Mode debug (affichage des strings du jeu)
- **Flèches** - Navigation
- **Entrée** - Activer/Désactiver un cheat
- **Tab** - Changer d'onglet

## Fonctionnalités

- **Health**: Invincibilité, cœurs extra, régénération
- **Studs**: Studs infinis, aimant, multiplicateur
- **Build**: Construction rapide
- **Detect**: Détecteur de minikits/powerbricks
- **Fun**: Super vitesse, super saut, moon jump, noclip, freeze temps
- **Unlock**: Débloquer personnages, niveaux, briques rouges
- **Visual**: FPS, coordonnées, wireframe, debug strings

## Cheats découverts dans le binaire

Les patterns suivants ont été identifiés dans `LEGOPirates.exe`:
- Table d'entités: `0x00C8F400`
- Santé: `0x00418CA8`
- Studs: `0x0046D570`
- Gravité: `0x0042B3D7`

## Dépendances

- MinGW-w64 (i686)
- d3d9.h, d3dx9.h
- Windows SDK (pour les constantes D3D)

## Auteur

Black Pearl Engine Team

## License

MIT
