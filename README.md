# Paper.io 3DS — homebrew prototype

A small territory-capture game in the spirit of paper.io 2, built for a
modded/homebrew-enabled Nintendo 3DS with **devkitPro** (devkitARM +
libctru + citro2d/citro3d).

## How it plays

- The 60×40 grid map is drawn full-screen on the **top screen**; the
  **bottom screen** shows your score and the bot's score as text.
- You control the blue square with the **D-Pad**.
- Move outside your own territory and you leave a trail behind you.
- Get back into your own territory and the trail (plus everything it
  encloses) is claimed as new territory.
- Touching your **own trail**, or the **bot's trail**, kills you — you
  respawn at your base and lose the in-progress trail (your territory is
  kept).
- There's one simple red bot opponent doing the same thing, with basic
  AI that avoids obviously fatal moves.
- Press **START** to quit.

This is a prototype/foundation, not a 1:1 clone — it's meant to be a base
you can extend (see "Ideas to extend" below).

## Project layout

```
paperio3ds/
├── Makefile
├── README.md
└── source/
    └── main.c
```

## Requirements

You'll need a working **devkitPro** installation with the 3DS
development packages:

1. Install devkitPro's package manager (`dkp-pacman`) following the
   official instructions for your OS: https://devkitpro.org/wiki/Getting_Started
2. Install the 3DS toolchain and citro2d/citro3d:
   ```
   dkp-pacman -S 3ds-dev citro2d citro3d
   ```
   `3ds-dev` is a meta-package that pulls in devkitARM, libctru,
   `general-tools` (bannertool, makerom, etc.), and friends.
3. Make sure the `DEVKITARM` (and `DEVKITPRO`) environment variables are
   set — the devkitPro installer normally does this for you
   (`/opt/devkitpro/devkitARM` on Linux/macOS, similar on Windows/MSYS2).

## Building

From the `paperio3ds/` directory:

```
make
```

This produces `paperio3ds.3dsx` (and `paperio3ds.smdh`). That `.3dsx` is
all you need for the Homebrew Launcher.

If you also want a `.cia` installable file, you'll need `makerom` and a
banner/icon step added to the Makefile (the `3ds-dev` package includes
`bannertool`/`makerom`) — happy to extend the Makefile for that if you
want an installable CIA instead of a Homebrew Launcher app.

## Installing on your 3DS

Standard homebrew workflow:

1. Copy `paperio3ds.3dsx` to the SD card, into `/3ds/paperio3ds/`
   (create the folder if it doesn't exist).
2. Insert the SD card back into your 3DS.
3. Launch the **Homebrew Launcher** (however your CFW/entry point is set
   up — e.g. via Luma3DS + Rosalina, or the classic browser exploit if
   you're on an older setup).
4. Select "Paper.io 3DS" from the list.

If you'd rather push it over the network instead of swapping the SD
card, tools like **FBI**'s "Remote Install" or a 3DS FTP client
(e.g. FTPD/ftpony) let you copy the `.3dsx` straight to `/3ds/` over
Wi-Fi.

## Ideas to extend

- Camera/scrolling for a bigger world (currently the whole map fits on
  screen at once, so no camera is needed).
- Multiple bots, or smarter AI (e.g. actively trying to enclose you).
- Circle Pad analog movement instead of discrete D-Pad steps.
- A proper game-over / win screen instead of instant respawn.
- citro2d text rendering on the top screen instead of a console on the
  bottom screen, for a more "real" HUD.
- Local multiplayer over local-wireless (a fun but much bigger project).
