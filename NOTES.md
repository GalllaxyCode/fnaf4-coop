# Five Nights at Freddy's 4: working notes

Phase reached: 6 (first milestone runs in the game on one Mac, two instances), no claim issue (owner declined), QA level `none`.
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

- Candidate first milestone: linked nights. Each player runs their own night, the host's
  `hour` drives everyone and any player's `gameover` ends it for all. Needs two values only.
- Candidate second milestone: shared night, one host simulation, with animatronics mirrored
  and a door held by either player counting as shut.
- A Chowdren rebuild might run natively on macOS and avoid Wine.

## Unknown

- One run with the host on Night 3 and the client on Night 2: the host was still on the night
  frame about 7 s after its kill started, when the run ended. Not reproduced with matched saves.
- Anything over a real network, or on two machines. Only 127.0.0.1 has been tested.
- More than two players in the real game. The fake-world tests use four.
- Night 5 and 6 kills, which go through other Fredbear groups.
- Play by a human, with real inputs. All real-game runs were unattended test mode.
- If a client's own timer reaches hour 6 before the host's, it wins locally first.

- Whether NebulaFD reads the Steam build. Not run yet.
- How far the Android logic matches the Steam build.
- Whether Steam Remote Play Together works for it despite not being listed.
- GameBanana, ModDB, Nexus, Archive.org and Discord: not searched yet.
