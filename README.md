# FNAF 4 Co-op

Shared-night co-op for the Steam version of Five Nights at Freddy's 4, on macOS through Game Porting Toolkit. Everyone stands in the same room and faces the same animatronics. Everyone needs their own copy of the game. This repo holds no game files.

| | |
|---|---|
| Game | Steam app 388090, manifest 3840504613812117733, Fusion build 288 |
| Tested on | macOS, two copies on one Mac, GPTK 4.0 beta 3 on Wine 11, driven by test scripts |
| Not tested | Two machines, more than two copies, human play, Windows |

This is an early build. It has been played by test bots, not yet by people, so expect bugs. Bug reports with `fnaf4coop.log` from each player are very welcome.

## Game modes

The host picks a mode in the CO-OP panel. In every mode, everyone stands in the same room and faces the same animatronics.

| Mode | What changes |
|---|---|
| **Default** | The normal game, shared. One death ends the night for everyone. |
| **Extended** | Animatronics get tougher with each player. The team shares one flashlight battery. Holding a door tires your arms, and sharing a door makes it last twice as long. One team revive per night: when someone is taken, a friend has 12 seconds to light the spot where it happened. |
| **True cooperative** | Each player picks a side. EYES have the flashlight but can't hear breathing. EARS hear it but have no flashlight. In the Plushtrap and BB minigames, EARS hold the light with a blank screen, and EYES tell them when. |
| **Versus** | Each player picks Survivor, Bonnie, Chica or Foxy. Animatronic players move on a map of the house, replacing that character's AI. |

**Last one standing** can be switched on for any mode. A caught player is out and watches as a ghost while the night continues, and it ends once no survivors are left.

A held door shuts for everyone, jumpscares are shared with everyone in the room, animatronic players and ghosts included, and each player's name floats over where they are. Standing with a friend at a door they hold or light, you share it, and moving the mouse down to walk away still works.

## Download

Get `fnaf4-coop-*.zip` from [Releases](../../releases). Every player needs the same release. The game is not included, you need your own copy on Steam.

## Install

1. **Get the Windows version of the game.** FNAF 4 has no Mac build, so Steam won't install it normally on a Mac. With Steam open, go to `steam://open/console` in a browser, then run this in Steam's console:

   ```
   download_depot 388090 388091
   ```

   When it finishes, the game is in `~/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamapps/content/app_388090/depot_388091`. Move it somewhere safe, because that folder is inside Steam's own app files.
2. **Get a Wine runtime that runs it.** Tested with Game Porting Toolkit 4.0 beta 3 on Wine 11. GPTK 1.1 on Wine 7.7 crashes on this game.
3. **Add the mod.** Put `winmm.dll` from the zip next to `FiveNightsatFreddys4.exe`. It also works for `HalloweenEdition.exe`.
4. **Launch with this environment variable**, which makes Wine load the mod. In a launcher, add it to the app's environment settings. From a terminal:

   ```
   WINEDLLOVERRIDES=winmm=n,b wine FiveNightsatFreddys4.exe
   ```

A **CO-OP** button appears on the title screen when the mod is running.

On Windows, putting `winmm.dll` next to the exe should be enough, but that is untested.

## Building from source

`./build.sh` builds `dist/winmm.dll` and runs the tests. It needs `brew install mingw-w64`.

## Playing on the same Wi-Fi

1. Everyone clicks **CO-OP** on the title screen and sets a different name, in Latin letters.
2. One player clicks **Host a room**, closes the panel, and picks New Game or Continue when everyone is in.
3. Everyone else clicks the room in the list. If it doesn't appear, type the address the host's panel shows into **Join by address**.

If rooms never appear, check that:
- everyone is on the same Wi-Fi, and any VPN is off
- macOS allows Local Network access in System Settings, Privacy & Security
- the network isn't a guest network, which often stops devices seeing each other

Problems are logged to `fnaf4coop.log` next to the exe. Over the internet, the host must be reachable on UDP 47114, for example over Tailscale, and friends join by address.

## Known issues

- A client takes a second or two longer to load a night than the host.
- Names show Latin letters only, others appear as `?`. Two players with the same name hide each other.
- The door panel in the room is drawn by the mod, not game art, and only shows after turning to the door.
- Fredbear's laughs, ambience and the phone are random on each copy.
- If the host dies with revive or Last one standing on, the night pauses for everyone while the host's game reloads.
- Story minigames between nights follow the host's screen, but what happens in them is not shared.

## How it works

`winmm.dll` forwards every call to the system DLL. The exe imports it and calls `timeGetTime` from its main loop, which gives the mod a tick on the game thread. From there it reads the runtime's objects by name and edits its event rules in memory: clients switch off the rules that move threats, and the host switches off the rules that recompute doors from local input alone. See `NOTES.md` for offsets and sources.

- `sync.c`: shared-night protocol, no game or network access
- `test_sync.c`, `test_menu.c`: fake-world tests with loss, reordering and duplicates, run by `build.sh`
- `game.c`: Fusion runtime access and event rule patching
- `rules.c`: which rules run where, threat mirroring, input merging, shared scares
- `input.c`: key state as the game sees it
- `overlay.c`: name bubbles and door panels
- `lobby.c`: rooms found on the local network
- `menu.c`: the CO-OP button and panel
- `mod.c`: DLL entry, UDP, window hook, test script
- `run-test.sh`: two real instances in test mode, from isolated folders beside this repo
- `tools/dump`: headless event-sheet dumper built on NebulaFD, which is not included
