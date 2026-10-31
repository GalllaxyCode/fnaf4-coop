# FNAF 4 Co-op

Linked-nights co-op for the Steam version of Five Nights at Freddy's 4, on macOS through Game Porting Toolkit. Everyone needs their own copy of the game. This repo holds no game files.

| | |
|---|---|
| Works | Shared clock from the host, shared death, shared 6 AM win |
| Game | Steam app 388090, manifest 3840504613812117733, Fusion build 288 |
| Tested on | Two copies on one Mac over localhost, GPTK 4.0 beta 3 |
| Not tested | Two machines, a real network, more than two players, human play |

## How it plays

Each player runs their own night against their own animatronics, so nothing is shared yet except the outcome. The host's clock drives everyone. If anyone dies, everyone gets the game's own kill sequence. If the host reaches 6 AM, everyone wins. Sharing animatronics and doors is the next milestone.

## Install

Everyone needs the same night in their save, because linked clocks only apply when nights match.

1. Build `dist/winmm.dll` with `./build.sh` (needs `brew install mingw-w64`).
2. Put `winmm.dll` next to `FiveNightsatFreddys4.exe`. It also works for `HalloweenEdition.exe`.
3. Copy `fnaf4coop.ini.example` next to it as `fnaf4coop.ini`.
   - Host: `role=host`
   - Friends: `role=client` and `host=<the host's address>`
4. In Windows App Runner, pick the GPTK 4.0 runtime and add this to Extra Environment:

   ```
   WINEDLLOVERRIDES=winmm=n,b
   ```

The host must be reachable on UDP 47114. Tailscale is the easiest way, or forward the port. Problems are logged to `fnaf4coop.log` next to the exe.

## Known issues

- A client whose own timer reaches 6 AM before the host's wins alone.
- Different nights are only reported in `fnaf4coop.log`, nothing on screen says so.
- Nights 5 and 6 are untested.

## How it works

`winmm.dll` forwards every call to the system DLL. The exe imports it and calls `timeGetTime` from its main loop, which gives the mod a tick on the game thread. From there it finds counters by name through the runtime's object list. See `NOTES.md` for offsets and sources.

- `sync.c`: linked-nights logic, no game or network access
- `test_sync.c`: fake-world tests with loss, reordering and duplicates, run by `build.sh`
- `mod.c`: DLL entry, object access, UDP
- `run-test.sh`: two real instances in test mode, from isolated folders beside this repo
- `tools/dump`: headless event-sheet dumper built on NebulaFD, which is not included
