# FNAF 4 Co-op

Shared-night co-op for the Steam version of Five Nights at Freddy's 4, on macOS through Game Porting Toolkit. Everyone stands in the same room and faces the same animatronics. Everyone needs their own copy of the game. This repo holds no game files.

| | |
|---|---|
| Game | Steam app 388090, manifest 3840504613812117733, Fusion build 288 |
| Tested on | macOS, Windows, GPTK 4.0 beta 3 on Wine 11 |
| Not tested | Steam online, Linux |

This is an early build so expect bugs. Bug reports with `fnaf4coop.log` from each player are very welcome.

## Game modes

The host picks a mode in the CO-OP panel. In every mode, everyone stands in the same room and faces the same animatronics.

| Mode | What changes |
|---|---|
| **Default** | The normal game, shared. One death ends the night for everyone. |
| **Extended** | Animatronics get tougher with each player. The team shares one flashlight battery. Holding a door tires your arms, and sharing a door makes it last twice as long. One team revive per night: when someone is taken, a friend has 12 seconds to light the spot where it happened. |
| **True cooperative** | Each player picks a side. EYES have the flashlight but can't hear breathing. EARS hear it but have no flashlight. In the Plushtrap and BB minigames, EARS hold the light with a blank screen, and EYES tell them when. |
| **Versus** | Each player picks Survivor, Bonnie, Chica, Foxy, Fredbear or Nightmare. Animatronic players move on a map of the house, replacing that character's AI. Fredbear and Nightmare can appear in the closet or on the bed from a hall, and Freddy rests while either is out. |

**Last one standing** can be switched on for any mode. A caught player is out and watches as a ghost while the night continues, and it ends once no survivors are left.

Fredbear and Nightmare are one body in the game, so if both are picked, those players share one Nightmare. Nightmare's timers are shorter than Fredbear's.

Picking Nightmare loads Night 7 with every other animatronic switched off, unless a player controls it, so it's a straight fight. Winning that night doesn't change anyone's save.

A held door shuts for everyone, jumpscares are shared with every survivor, ghosts included, while animatronic players stay on the map, and each player's name floats over where they are. Standing with a friend at a door they hold or light, you share it, and moving the mouse down to walk away still works.

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

## Settings

**SETTINGS** in the CO-OP panel has a **Jumpscare volume** slider from 0 to 100. It turns down every jumpscare's sound: Bonnie, Chica, Freddy, Foxy, Fredbear, Nightmare, the cupcake, Plushtrap, BB, the Extras gallery and the cutscene scares. At 0 they make no sound at all. It only changes your own game, works in solo nights too, and is saved in `fnaf4coop.ini`.

On Windows, putting `winmm.dll` next to the exe should be enough.

## Building from source

`./build.sh` builds `dist/winmm.dll` and runs the tests. It needs `brew install mingw-w64`.

## Playing on the same Wi-Fi

1. Everyone clicks **CO-OP** on the title screen and sets a different name, in Latin letters.
2. One player clicks **Host a room**, closes the panel, and picks New Game or Continue when everyone is in.
3. Everyone else clicks the room in the list. If it doesn't appear, type the address the host's panel shows into **Join by address**.

Rooms are announced by broadcast, and the panel also asks nearby addresses directly, because many networks drop broadcasts between devices. On a large network a room can take up to about 15 seconds to appear. Keep the panel open while it looks.

If rooms never appear, check that:
- everyone has the same version, shown beside CO-OP in the panel
- everyone is on the same Wi-Fi, and any VPN is off
- macOS allows Local Network access in System Settings, Privacy & Security
- the network isn't a guest network, which often stops devices seeing each other

Problems are logged to `fnaf4coop.log` next to the exe.

## Playing with friends on other networks (Steam)

Friends on different Wi-Fi networks can play through Steam, with no addresses and no router setup. Traffic goes through Valve's free relay network. **This is new and untested on real Steam so far**: it has only been tested with a stand-in that imitates Steam.

What every player needs:

1. **Steam for Windows**, installed inside the same Wine setup the game runs in, signed in and running while you play. The Mac Steam app can't be used for this.
2. **`steam_api.dll`** next to `FiveNightsatFreddys4.exe`. Use the 32-bit one, `Windows-x86/steam_api.dll` from the [Steamworks.NET Standalone release 2025.164.1](https://github.com/rlabrecque/Steamworks.NET/releases), SHA-256 `fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da`. It isn't in this mod's download.
3. The host and the friend must be **friends on Steam**.

The mod runs as Valve's free sample app 480, "Spacewar", which every Steam account can use, so Steam shows you as playing Spacewar. It writes `steam_appid.txt` with `480` next to the exe if that file is missing.

How to play:

- The host clicks **Host a room** as usual. When Steam is running, the panel says `+ STEAM` and the room is also open to Steam friends.
- Friends see it in the room list as `NAME - STEAM - JOIN`, or click **Join Game** on the host in their Steam friends list, or type the lobby id from the host's `fnaf4coop.log` into the address box.
- Friends on the same Wi-Fi can still join the same room the normal way.

Without `steam_api.dll` or a running Steam, nothing changes: the mod works as before over the local network. Over the internet without Steam, the host must be reachable on UDP 47114, for example over Tailscale, and friends join by address.

## Known issues

- A client takes a second or two longer to load a night than the host.
- Names show Latin letters only, others appear as `?`. Two players with the same name hide each other.
- The door panel in the room is drawn by the mod, not game art, and only shows after turning to the door.
- Fredbear's laughs, ambience and the phone are random on each copy.
- If the host dies with revive or Last one standing on, the night pauses for everyone while the host's game reloads.
- In the story cutscenes between nights you see each other's child walking around, but each player still talks to people and finishes the scene on their own. The scene ends for everyone when the host's ends.
- Other players' children in cutscenes are drawn over the game, so they appear in front of tables and doors.

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
