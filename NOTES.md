# Five Nights at Freddy's 4: working notes

Phase reached: 6 (shared night runs in the game on one Mac, two instances), no claim issue (owner declined), QA level `none`.
Last updated 2026-10-31.

## Identity

| | |
|---|---|
| Title | Five Nights at Freddy's 4 (2015), Scott Cawthon |
| Store | Steam, app 388090, depot 388091, manifest 3840504613812117733 |
| Main exe | `FiveNightsatFreddys4.exe`, SHA-256 `082a7ecd7369dff1e4bf433b3e8e73ee10fa200afb6509fbe47f551dd3c9c314` |
| Second exe | `HalloweenEdition.exe`, SHA-256 `cb31f6ce460beff1bb4d1a9ae7f16e346c58a743af9d01a41a10489e3df43354` |
| Engine | Clickteam Fusion 2.5 runtime, version resource 3,0,288,3 (build 288) |
| Binary | 32-bit PE, PE timestamp 2017-03-20, data in an overlay at 0x108800 (`wwww` pack header) |
| OS | Windows only on Steam. Owner runs it on macOS through Wine, see below |

## Triage answers

1. Exact game: see Identity. Standard and Halloween editions ship as two exes.
2. Already done: not found by the searches below. Do not read that as "nobody has".
3. Engine: Clickteam Fusion. `playbook/engine-families.md` has no entry for it.
4. Source: closed binary, but the game logic is event data in the pack, not native code,
   and decompilers for the format exist.
5. Multiplayer in the engine: the exe imports no networking extension that the first
   6 MB shows. Only a partial scan, so treat as unknown. The Steam page lists Single-player
   and Family Sharing only.
6. Shipping: a patch the player applies to their own copy. No anti-cheat seen. No online
   service in the game that we know of.

## Observations

- Steam store page features: Single-player, Family Sharing. Remote Play is not listed.
  https://store.steampowered.com/app/388090/
- No FNAF or Clickteam entry in `DIRECTORY.md`, `data/directory.yaml` or `games/`, and no
  matching issue or PR for `fnaf`, `five nights`, `freddy` or `clickteam` (all states).
- Search for FNAF 4 co-op mods in English and Russian found no mod for the retail game.
  Most hits were SEO pages repeating unverified claims, none was used.
- FNAF Online (BrianMPZ) is a separate Unity game for Windows 10/11, 0.9.6-alpha, "best
  played with 4-5 players". It is not FNAF 4 and was not downloaded.
  https://brian-mpz.itch.io/fnaf-online
- Decompilers: CTFAK 2.0 is archived (AGPL-3.0). Anaconda says it is replaced by NebulaFD
  and lists build 284-293 support, but "does not support 2.5+ games". NebulaFD is active
  (pushed 2026-09-28) with no license stated, so reuse terms are unclear.
  https://github.com/CTFAK/CTFAK2.0 https://github.com/fnmwolf/Anaconda
  https://github.com/AITYunivers/NebulaFD
- ppvaz/fnaf-solver (MIT) reports a reverse-engineered model of FNaF 1-4 and rebuilds of
  all four from their Android releases, using patched mmfparser and Chowdren. It labels
  its model results `MODEL_ONLY`. These are the repo's own claims, not checked here.
  https://github.com/ppvaz/fnaf-solver
- Launch on macOS: GPTK 1.1 (Wine 7.7, 32on64) reached audio and OpenGL setup, then died
  with `Assertion failed: (end <= pages_vprot_size << pages_vprot_shift), function
  alloc_pages_vprot, file virtual.c, line 1032`. One run. It used the owner's stable prefix
  and happened before this playbook was opened. No gameplay was reached.
- `dotnet`, `docker` and `mono` are not installed on the owner's Mac.

- Owner reports the game launches on GPTK 4.0 beta 3 (Wine 11), own test, 2026-10-31. Gameplay
  depth of that test is unknown.
- Owner reports friends also have the game running on Mac.

- Own read-only chunk walk of the Steam exe: runtime 0x302, build 288, 47 top-level chunks,
  19 frames. Every frame's event chunk (0x333D) is stored `encrypted+zlib`, so reading the
  logic needs the format's decryption, not just inflate.
- Android FNaF 4 is build 296 per the solver's docs, the Steam exe is build 288.
- NebulaFD's event reader has a separate branch for builds below 296 on Windows.

- NebulaFD (a24d4d1, one-line patch to skip the app icon on non-Windows) reads the Steam
  build: 19 frames, night logic is frame 3 `level` with 480 events.
- Runtime modules in the pack: mmfs2.dll, kcini.mfx, cctrans.dll, waveflt.sft.
- Frame 3 keeps its state in named counters: `gameover`, `hour`, `Night`, `Bonnie AI`,
  `Chica AI`, `Foxy AI`, `Freddy AI`, `Fredbear AI`, `Freddy counter`, `left door shut`,
  `right door shut`, `foxy got you`, `got you`, `listening mode`, `viewing bed`,
  `viewing left hall`, `viewing right hall`, `viewing closet`.
- `follow` AV0 is the player's station. g192-g195: AV0 21 sets `left door shut`, AV0 24 sets
  `right door shut`, each cleared on any other value. g136: AV0 29 with key 17 sets
  `viewing closet`. g187/g188: AV0 10 and 17 are the two door listening stations.
- Kills set `gameover` from anywhere: g43-g45 (Foxy on a run), g200/g201 (flashing a hall with
  Bonnie or Chica in the kill zone), and others.

- Exe imports WINMM.dll (timeBeginPeriod, joyGetDevCapsW, joyGetPosEx, timeGetTime,
  timeEndPeriod). Both exes have byte-identical .text and the same PE timestamp 0x58CFD2A9.
- Exe globals: CRunApp at RVA 0xB39CC, frame at 0xB39D0, RunHeader at 0xB39D4 (about 1000 uses).
  Modloader prior art uses the same layout: https://github.com/CTFAK/CTFAK/tree/master/CTFAK-Modloader
- rhObjectList is RunHeader+0x1278 with 8-byte entries, confirmed in code. rhOiList is +0x58.
- objInfoList is 146 bytes (`imul reg, reg, 0x92`, 95 uses), not the SDK's computed 142, because
  of the HWABETA oilColList field at the end. oilObject +6 confirmed. oilName +90 is derived.
- Counter CValue: type at object+0x202, value at +0x20A, confirmed in code.
- Kill path: g282 `Freddy counter` >= 80 sets black flash AV3, g285 counts it down, g286 sets
  `gameover` and creates the scare, then the normal frame jump. g283 clears AV3 below 80, so the
  counter must be held. Win path: g384 `hour` = 6 jumps to frame handle 3.
- Sync tests cover 100 seeded runs across four network profiles up to 50% loss, 400 ms jitter.

- Counters are stored inverted: the runtime applies `not` after each read of object+0x20A (18 of
  54 references have it nearby). Raw -13 is 12, raw -2 is Night 1.
- On Steam the night clock starts at hour 12 and g381 wraps it to 1.
- `num 1` is referenced only by frame 5 `night win`, so it marks that frame.
- Sync tests: 7107 checks, 0 failures under ASan and UBSan, fake world only.
- Real game, two instances on one Mac over 127.0.0.1, GPTK 4.0 beta 3, Night 1 unless noted:
  object names read correctly; both instances reach the night unattended; host clock change
  reaches the client in 50-140 ms; client death kills host through the game's own path, host
  left the night 2.2 s later (Nights 1 and 3); both restart and relink on the next attempt;
  host at hour 6 sends both to the win frame 275 ms apart; different nights raise the warning.

- Shared night, observed in the real game (two instances, one Mac, test mode):
  event list walks to exactly 480 groups from CRunFrame+128; evgFlags 0x4000 stops a group;
  group header 16 bytes, condition 16, action 14. Clients with g-list off keep threats identical
  to the host (13 of 13 comparable samples over 110 s on Night 4, 8 distinct positions).
  A client whose save was Night 1 loaded the host's Night 4. Client door hold sets the host's
  `left door shut`. Host hold drives a client at the peek from AV0 10 to 20 through injected
  Shift. All 8 scare kinds reach the other player. A natural Freddy kill on the client reached
  the host. Host Ctrl gives the client `light on`=1 in frame 7, with trap and timer mirrored.
  Overlay exists as a 1470x956 macOS window above the game.
- Goto frame: `rhQuit`=3 and `value|0x8000` at RunHeader+0x38, value is the 0-based index.
  Current frame index is CRunApp+0x1F0. rhQuitParam is not at the SDK's offset.
- Wine here has no FreeType, so GDI text is empty. The overlay carries its own font.
- Game windows on another macOS Space return a frozen frame to `screencapture -l`.
  The mod's `shot` script command saves the window from inside the process instead.
- Alterable values are not inverted, only counters are.

- Title menu: rooms announce on UDP 47115. A client found and joined a room at the Mac's real
  address, both copies on one Mac. A client that joined through the panel followed
  the host into a night. Leaving a room left a later solo night with co-op rules off.
- Loading a night blocks the main thread for about 10 s, so no packets flow. A 5 s lost-host limit
  kicked a client during a load, the limit is now 25 s plus an explicit goodbye packet.
- The game draws in a child window (Mf2EditClassTh) that fills the main window (Mf2MainClassTh), so
  subclassing the main window misses real clicks. A WH_GETMESSAGE hook sees them all. Keys swallowed
  there need TranslateMessage first, or WM_CHAR is never made. Test scripts now use mouse_event and
  keybd_event, because posted messages skip this routing.
- Real paths driven by injected mouse events: a host's light at a door lights a client at the same
  door within about 220 ms, and a host's hold closes a client's door or closet with the game's
  animation. Rule 400 mirrored on a client cancelled the kill and left it pushed off the bed, so
  clients now run Foxy's kill themselves.
- The game reads the mouse from GetCursorPos, so injected mouse_event moves don't reach its zones.
  Test scripts give each copy its own cursor through the GetCursorPos hook (`cur`).
- Zones: `reset` y 464-656 arms the turn, `run back` y 682-762 fires it, both nearly full width.
- Walk-away and bed-turn rules need Shift and Ctrl up. A friend's shared key held back a client's
  pending turn, which fired on release and looked like following the host. Reproduced at a door and
  on the bed. Shared keys now disarm the turn, and a host's turn leaves a client in place.
- The night's map layer scrolls with each copy's own view: zones and threats shift by follow X - 512.
  Mirroring absolute X put threats in the wrong place when players faced different ways, in every
  build before modes. X now travels as a distance from `living room center`. Checked with players
  facing opposite ways, `bonnie danger` agrees on both copies.
- Modes, in the real game, test scripts, two copies on one Mac: difficulty scaling raised AI 2,1,1 to
  5,4,2; rescue completed in 1.5 s; battery ran out and recharged; grip released a door after 8.5 s;
  EARS got no light; EARS' Ctrl lit Plushtrap for EYES; a client played Bonnie to the door and a shut
  door sent her back; with Last one standing a dead client rejoined as a ghost in 6 s, a dead host
  reloaded and restored the night from a snapshot, and the last death ended it for everyone.
- GetAdaptersInfo works under this Wine, given a large enough buffer.

## Derived

- The solver's work targets Android builds, so per ground rule 5 nothing from it applies to
  the Steam build until checked against the hash above.
- Fusion games keep logic as event data, so a decompile may expose the night logic in a
  readable form. This is closer to "script source" than to a pure closed binary in triage
  step 4, if a decompiler reads this build.
- FNAF 4 has no avatar: one player, one room. Co-op therefore needs a designed mode, not
  puppets. Shared state looks small (animatronic positions and timers, night clock, flash
  and door input), which suits host authority.

## Guesses

- A Chowdren rebuild might run natively on macOS and avoid Wine.

## Unknown

- Whether the GitHub button reaches a macOS browser. ShellExecute returned success inside Wine.
- The BB minigame in True cooperative, rescue while the host is the victim, missed rescues in the
  real game, Foxy and Chica as players, balance of every number in the modes.
- Whether a background copy ignores the mouse on a real Mac.
- Whether the overlay looks right on screen. Its pixels were checked in in-process captures only.
- Two machines, or more than two real copies. macOS Local Network permission may block discovery
  between machines, and one Mac can't show that.
- Real macOS clicks. Injected hardware events go through Wine's routing but skip macOS itself.
- Play by a human with real mouse and keyboard. All real-game runs were test scripts.
- In the real game, not yet run: flashlight scares (EV_SCARE_*), closet hold, remote bed view,
  remote listening, Foxy's closet run from a remote side, the BB minigame, Nights 5 and 6.
- One old run with the host on Night 3 and the client on Night 2 left the host on the night frame
  about 7 s after its kill started. Not seen since, including on matched Night 3.
- How far the Android logic matches the Steam build beyond what was read here.
