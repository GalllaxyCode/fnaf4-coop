# Five Nights at Freddy's 4: working notes

Phase reached: 6 (shared nights played by the owner and a friend on two Macs, and on Windows per the
owner), no claim issue (owner declined), QA level `none`.
Last updated 2026-11-04.

## Identity

| | |
|---|---|
| Title | Five Nights at Freddy's 4 (2015), Scott Cawthon |
| Store | Steam, app 388090, depot 388091, manifest 3840504613812117733 |
| Main exe | `FiveNightsatFreddys4.exe`, SHA-256 `082a7ecd7369dff1e4bf433b3e8e73ee10fa200afb6509fbe47f551dd3c9c314` |
| Second exe | `HalloweenEdition.exe`, SHA-256 `cb31f6ce460beff1bb4d1a9ae7f16e346c58a743af9d01a41a10489e3df43354` |
| Engine | Clickteam Fusion 2.5 runtime, version resource 3,0,288,3 (build 288) |
| Binary | 32-bit PE, PE timestamp 2017-03-20, data in an overlay at 0x108800 (`wwww` pack header) |
| OS | Windows only on Steam. Owner runs it on macOS through Wine, see below, and reports it works on Windows |

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
- Walk-away and bed-turn rules need Shift and Ctrl up, so a friend's forced key pinned a player at a
  door, the closet or the bed until the friend let go (reproduced with the light at a door, old
  behaviour restored as a mutant). Forced keys now drop while the cursor is in `run back` with the turn
  armed, and the game's own rules carry the player off. With the host still holding, a client left a
  door lit (10-11), a door shut (21-22-11), the closet shut (29-30) and the bed lit (43-44). With the
  cursor out of the zone, a host's release moved no one (21-22-10). This replaces disarming the turn.
  A cursor left in the zone carries on to the bed after a door, as the rules read solo.
- Plushtrap: the mirror wrote the host's position and set `move`, so the client's rules then stepped
  him one stage on, every tick. Seen as game=890,588 against host=834,534 for 4 s, 159 rewrites in
  30 s, with a footstep each. Clients now move him only through the rules (`move` with the `random`
  the step needs, rule 20 for a catch) and write only if a step didn't land. Same run: 5 steps,
  0 writes after the chair fix, 1 momentary difference in about 100 samples at 120 ms.
- Reveal: the host zeroes `becoming active` in the same packet that turns the client's light on, so the
  client missed the reveal (host view=3, client view=1, twice, as a mutant). Skipping that one write
  gives view=3 on both, and a catch gives view=2 on both.
- Scares, baseline: with Last one standing the host left at once and the victim, already a ghost,
  got the 250 ms follow grace, cutting the scare at about 0.57 s of 0.97 s. With an animatronic host
  and no Last one standing the night never ended on the host, and the client was pulled back and
  killed about every 15 s. Animatronic copies never saw a scare at all.
- Scares, fixed: a shared scare runs on every survivor, ghosts too, with the overlay
  off, and each copy leaves through the game's own rule. Animatronic players were later taken out of this. Last one standing's last death is a shared
  scare, and the host's goto waits for it, at most 5 s. Runs: Versus with and without Last one
  standing with either side as host, kinds 1, 4, 5, 6, 7, 8, Default, and a full Last one standing
  night. Every copy took 0.95-1.0 s. All scares were started by test scripts.
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
- Rooms: on a real /20 Wi-Fi (10.91.0.0/20, 255.255.240.0) a client's panel stayed open 16 s with a
  friend hosting and no announcement arrived, while a join by address worked, so that network carries
  unicast and not broadcast between devices. Derived, the network itself wasn't inspected. The panel now
  sends a Probe to `port` on each address outward from its own, 16 a tick, until a room answers, then
  only known rooms and the `last_room` from the ini. Probes leave from their own port: a shared 47115
  sent the answer to the other copy's finder on one Mac. With broadcasts off on one Mac, the room
  appeared 86 ms after the panel opened, and with probing compiled out too it never did.
- The rule list keyed on the address of the frame's first group, and a new frame can land at the old
  frame's address. A host stayed at 7 rules (frame 2's) for a whole night, in 3 of 3 runs of one build, so
  every rule patch did nothing. The list is now rebuilt for 40 ticks after any frame change. The state
  log prints `rules=`.
- Nightmare: rule 431 leaves in the frame the scare starts, so holding it back showed Fredbear's scare
  first. It is left alone now. A client in frame 15 reports the kill with the epoch it kept, and the host
  counts a kill made there. A host that isn't the last death watches 4.5 s, then reloads the night.
- Animatronic players get no scare and stay on the map. A host playing one ends the night 1.5 s after a
  kill, and a client playing one follows the host, but not into frame 15.
- Fredbear's sound is channel 27, the `fredbear` loop started silent by rule 359. Rules 238/239 (bed)
  and 96/97 (closet) set it to 30 while lit, and only rule 360, on Ctrl released, sets it back to 0, so a
  light kept on after he left kept it playing; on clients rule 348 (off there) never muted it either.
  Rule 360 now reads F24 (0x87, `quiet_key=135` in the log), held by the mod only while the light is on
  and he is on the bed or in the closet. Real game, Versus: he reached the bed with the host's light on
  (gate open), was sent away 1.3 s later and the gate closed while the light stayed on for 12 s. The
  sound itself wasn't measured here.
- Movement cues, from the level sheet: Bonnie and Chica set `follow` AV23/AV24 (1-3) in the living room
  and AV21/AV22 (1-4) in the halls; Fredbear AV11/AV17 = 3; Foxy AV11/AV17 (1-2) in the living room, and
  plays `foxyrun1`/`foxyrun3` and `runandenter` inside rules 115, 116, 132 and 133, which a written
  position never runs. Versus played nothing for Foxy, and AI Foxy's hall and closet runs were silent on
  every client in every mode.
- Foxy now moves through those rules: the mod sets his roll (AV0) and move flag (AV1), or `what side`
  for the closet, and writes the position only if the rule didn't move him by the next tick. The host
  holds rule 111, which rerolls AV0 each second, for that tick. Real game: a player's Foxy made 3 of 3
  moves by rule on the host and the client; AI Foxy on Night 3 made 4 of 4 by rule on the client. The
  hall steps of a player's Bonnie and Chica now pick from four sounds, like the game's.
- Cutscenes (frame 12, 0.5.0): the child is `boy` (OI 253), placed on `hitbox` every frame; `screen
  follow` is the view's centre while `view who` = 2. Which scene plays comes from each save (`scene`), so
  two players got different scenes. Every rule acts on every `boy`, so a second instance in the game
  would have run one player's dialog and doors for the other. Instead each copy reads its own boy's
  pose and the others draw it in the overlay with art read from the player's own exe.
- Object layout, read from the SDK and confirmed live: rCom at 184, rcImage 220, rcOldX 260; rAni at
  +[184] (462 here): raAnimOn +16, raAnimDir +24, raAnimFrame +68; rSpr at +[188] (542), flags +32.
  The runtime's rcImage (69, 70 standing) isn't the bank handle (984, 985), so the pose is sent, and a
  table from the object's animations maps it: standing 984-985, walking 986-993 by direction 0/8/16/24.
- Image bank: chunk 0x6666, flags 0, 1281 images, per image handle, unpacked size, packed size, zlib.
  Unpacked: 32-byte header (sizes, mode, flags, hotspot, transparent colour), 24-bit BGR rows padded to
  an even width, then alpha rows padded to 4. The boy's frames are 84x155 with alpha. bank.c reads it
  in about a second on a thread at start; the decoded pixels match a separate Python reader.
- Cutscenes draw black lines on view rows 4-5 of every 8; the overlay leaves those rows of a child
  empty so the lines cross it as they cross everything else.
- Real game, two copies, client's save on scene 4, host's on 2: both played the host's scene, each saw
  the other's child at its position with its name, the client walked 150 px and the host saw it there.
  The host's save went to 3; the client's stayed at 4 once rule 2 was off on clients.
- Steam (0.4.0), built from playbook/networking-steam-spacewar.md: app 480, `steam_api.dll` loaded at run
  time through the flat API with manual callback dispatch, friends-only lobby with key `fnaf4coop` =
  SYNC_VERSION, rich presence `connect`, unreliable NoNagle messages on channel 0, sessions accepted for
  lobby members on the host and only the host on a client, sender read at offset 12 or 16 whichever is
  non-zero. Hosting opens the Wi-Fi room and the Steam lobby together, and a host's peers can be either.
  Friends' rooms come from GetFriendGamePlayed. Joins come from the room list, Join Game (337), an
  invite (333), `+connect_lobby` on the command line, or a lobby id typed in the address box.
- Steam, tested with a stand-in in the mod (fake accounts 76561198860265728 + slot, lobbies as ini files
  in a shared folder, messages over localhost, a stranger's messages dropped until accepted, lobby
  creation reported twice). Real game, two copies, Wi-Fi discovery off on both: a client found the
  host's Steam room and joined, a night started, a death was shared both ways; Join Game joined with no
  clicks once on the title; a typed lobby id joined; the host closing the room reached the client and
  removed the lobby; a lobby at version 5 was refused with its own message; a Wi-Fi friend joined a host
  whose room was also on Steam. With no DLL and no stand-in there were no Steam lines and no files written.
- Chica: rule 65 fires when the player reaches AV0 17 with Ctrl held and Chica in the hall, and it can
  pass 17 inside one 50 ms tick, so the mod saw 16-41. The scare only counted after a seen 17, because
  the bed turn also uses 41. The host kept her in the hall and the hide replayed about every 0.55 s, with
  17 lasting one frame, until she reached the door. From a real log: 16-41 at 00:17:25.3, kill 2.75 s
  later. With the old check restored, 41 held 4.8 s and she stayed. Now the `Chica hide` object decides:
  one 0.76 s hide, and the host moves her home.
- A Nightmare night is a match: What Night reads `shadow`=1 and sets Night 7, and the host holds other
  AI at 0 unless a player controls them. The win frame saves Night + 1, and at Night 7 writes `beat7` and
  heads for the Plushtrap frame, so each copy turns off rule 384, sets Night to its own file's night - 1
  at hour 6 and returns to the title after 9 s. The first version kept the night in memory, and a
  client's save went from 2 to 1 because the host's `shadow` had already arrived. Each copy now reads
  `fn4` itself. With host 4 and client 2, both saves were unchanged after a win.
- Fredbear and Nightmare are one object, `Fredbear`. `shadow` above 0 makes it Nightmare: its art, shorter
  timers (hall 10 s against 15, closet or bed 11 s against 20) and its own scare frame. Only Extras
  sets `shadow`. Game over, the title and the win frame clear it. Fredbear AI above 0 stops Freddy (rule 432).
- `shadow`=1 fires rule 405, which makes the night Night 7 with AI 15, so the host turns off 405-408
  before setting it. Observed: night stayed 2 and AI 1, and a later night read shadow=0, ai=0.
- Rule 431 leaves for frame 15 in the same frame as the scare, before a tick can report the death.
  In Versus it waits 250 ms. Frame 15 ends in Extras at 5 s, so Versus goes to game over at 4.5 s.
- Rules 451-455, his hall timer, ran on the host alone, which can't die as an animatronic. Clients run
  them in Versus.
- Versus, real game, two copies, natural kills with no injection: Fredbear in a hall killed the host at
  about 15 s, Nightmare in a hall killed a client and a host at about 10 s, Nightmare on the bed killed a
  client at 11.9 s. Nightmare's scare filled the screen on both copies, the host's included. A shut door
  sent Fredbear away with no death. A survivor's light showed Nightmare in the hall. Fredbear picked by
  one player and Nightmare by the other shared one Nightmare.

- Owner reports the mod works on Windows, own test, 2026-11-02. Windows version and which modes were
  played are unknown. No Windows run was made from here.
- Human play on two Macs, from the owner's logs (2026-11-01, v0.3.0 to v0.3.1, Wi-Fi 10.91.0.0/20): the
  owner joined a friend's room at 10.91.2.79, by address on v0.3.0 and by clicking it in the room list on
  v0.3.1. Versus with Last one standing and True cooperative nights were played, a Nightmare kill
  was shared (`shared scare 8`), and the Chica loop above was hit in True cooperative.
- On v0.3.1 the room list showed the friend's room 4.0 s after the panel opened. The ini held
  `last_room`=10.91.2.79, so either the probe to it or the sweep found it; the log doesn't say which.

- Jumpscare volume (2026-11-03, read from the Steam exe and mmfs2.dll from the pack). The exe calls MMFS2.dll
  through a delay-load table (rva 0xAA140, 310 imports, by ordinal only). Speaker action routines sit in the
  action table at 0x4AF880, entries 388 on (action n at 388 + n): 11 at 0x409500 and 12 at 0x409600 read the
  channel, then call 0x42CF70, which calls ordinal 1077, PlaySndW(manager, app, number, window, flags, loops,
  channel), through slot 0x4B2760. Action 17 at 0x409970 calls ordinal 809, SetSndChannelVolume(app, channel,
  volume), through slot 0x4B27AC. In MMFS2 (0x100120C0) it clamps 0-100, stores the volume at app + 28 * channel
  + 0x34 and applies main * volume / 100 to the sound playing there. No frame has a sound condition.
- The number a play passes is the runtime's own, per frame, not the bank handle the event dump shows. Seen
  live: sound 1 is crickets in the night, distortion in frame 15, title in frame 1; sound 2 is ambience in
  frame 2. Build 288 keeps each sample's name in the action's parameter (size, code 6, number, flags, name),
  so the mod looks the number up by name in the frame that is playing it.
- Jumpscare sounds: scream2 (rule 202, every night kill, channel 16; Extras 42-46 on 3), screamshort (254,
  Foxy's closet, 16; cutscenes 99-100 on 6), scream3 (Plushtrap and BB rule 45 on 4; Extras 47-48 on 3),
  distortion (frame 15 on 1, looped). Same names and bank handles in HalloweenEdition.exe, which adds garble and
  musicbox. Night kills play then set channel 16 to 100; the minigames set channel 4 first.
- Real game, two copies, test scripts, Night 5: setting 40 and 30 on the host, a forced Bonnie kill played
  scream2 with channel 16 stored at 40 and 30, and the game's own set to 100 was held there. Setting 0 on the
  client silenced the shared scream2 and distortion after a goto to frame 15, and both scares still ran to the
  usual frame change (about 1 s, and 5 s for frame 15). A run with crickets cached as sound 1 then frame 15's
  sound 1 still read distortion. A real click on the slider at its middle saved jumpscare_volume=50, and a
  launch with 35 in the ini started at 35. The sound itself was not listened to or measured, only the volume
  MMFS2 stored. Not run: Foxy's closet scare, Plushtrap, BB, the Extras gallery, cutscene scares, Halloween.

- Owner report, 2026-11-04, Windows, v0.5.3: the Steam overlay doesn't open, and friends can't join over
  Steam. No logs yet, so which step fails is unknown.
- A 2016 Steam discussion for FNAF 4 reports the overlay doesn't work in the unmodded game.
  https://steamcommunity.com/app/388090/discussions/0/154642321534516217
- mmfs2.dll from the pack imports DDRAW.dll and names mmf2d3d8.dll and mmf2d3d9.dll, and neither of those is
  in the pack. Valve lists the overlay's supported APIs as DirectX 7 to 12, OpenGL, Metal and Vulkan, and says
  it doesn't work in software-rendered games. https://partner.steamgames.com/doc/features/overlay
- The three exports 0.5.4 adds (GetFriendPersonaState, InviteUserToLobby, RequestLobbyData) are in Steamworks
  1.57's steam_api64.dll (read from another game's copy) and in the pinned 32-bit steam_api.dll
  (Steamworks.NET Standalone 2025.164.1, zip digest 9412348c…537d matches GitHub's, DLL SHA-256 fdafb7c6…5da
  as pinned), checked 2026-11-04. From 0.5.4 the release zip carries that DLL.
- 0.5.4 (built, unit tests only, not run in the game): the host's panel has INVITE STEAM FRIENDS, a list of
  online friends that sends InviteUserToLobby, and the lobby id. The panel's top line says STEAM ON or why it is
  off. Friends' rooms no longer need the friend to show as app 480; a lobby carrying the `fnaf4coop` key counts
  (RequestLobbyData for the rest). A session from someone not yet in the host's lobby is held for 15 s and
  accepted once they appear, instead of refused for good; sends carry AutoRestartBrokenSession. More Steam log
  lines: launch app id, overlay renderer loaded, lobby members entering and leaving, LobbyEnter's answer, the
  first message, send results, session failures. Menu tests 12628 checks, 0 failures; disabling the invite
  button made 8 of them fail.

- Jumpscares switch (0.5.5, 2026-11-04). Every night death creates a scare object and leaves for game over when
  its animation ends: `jumpscare` (rules 200/201 and others, ends 203-206), `freddy bedroom attack` (288),
  `cupcake attack` (300), `bonnie bedroom attack` (301), `fredbear scare` (370; 431 sends Nightmare to frame 15).
  `foxy bite` (254) is Foxy's closet scare, not a death; 255 removes it. These six are used only in frame 3.
  Rule 224 is "always" and shows `jumpscare` among others. Fusion's object actions: 24 destroy, 26 hide, 27 show.
- Object creation reads "visible at start" from the object's common data: app+0x194 count, app+0x198 word index
  by OI handle, app+0x19C table of object infos, info+2 type, info+0x14 common data, flags2 at +0x2A, bit 8
  (code at 0x42b9b9 and 0x446a3a). Clearing it makes new instances hidden (sprite flags 3 against 22 seen).
- With the switch off the mod clears that bit for the six objects, makes rule 224 hide `jumpscare` instead of
  show it, turns off 431, and mutes scare sounds in frames 3 and 4. Hidden objects still animate and end.
- Real game, two copies on one Mac, test scripts, host off and client on: forced kills 1, 4, 5, 6, 8, Nightmare
  (shadow 1 then 8) on Night 5, and 7 on Night 3. Every one logged its object `appeared, hidden` on the host and
  `shown` on the client, scream2 `silenced` on the host and `played` on the client, and reached frame 4 between
  0.94 s and 1.02 s after the scream on both. In-process shots during the scare showed the room with no animatronic,
  and shots after it the game over screen. Nightmare went to frame 4, not 15. Switching back on in the same night
  restored frame 15 and distortion. Kill 7 doesn't fire on Night 5 with or without the switch (Fredbear AI 12, rule
  432). A real click on the switch saved `jumpscares=0`. Not run: a natural death, Foxy's closet bite, the game
  over sound with the switch off, Halloween edition, and the Windows build.
- The panel's text is about 9 view pixels a glyph on screen, not 6. The 0.5.4 Steam status ran into the version;
  moved and shortened, checked in a shot.

## Derived

- FNAF 4 most likely draws through DirectDraw (Fusion's Standard mode), which is why the overlay never opens. From
  the pack's module list and Valve's list above, not checked on Windows.
- Possible reasons friends couldn't join, none confirmed without logs: no or 64-bit `steam_api.dll` (the panel
  only showed `+ STEAM` when it worked, and said nothing otherwise); the host refusing a session that arrived
  before the friend's lobby entry (the stand-in rechecks every message, so it couldn't show this); a friend who
  started from Steam's Play button showing as FNAF 4, which the room list filtered out.

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

- Whether the overlay appears in FNAF 4 with a DirectDraw wrapper (dgVoodoo2, cnc-ddraw). Not tried.
- Real Steam: whether Windows Steam runs signed in under GPTK beside the game, whether `steam_api.dll`
  loads there, the callback and message offsets (from the playbook, measured on Windows), lobby
  creation, sessions, Join Game and invites between two accounts. Nothing has run against real Steam.
- Which probe found the friend's room across machines: the one to `last_room`, or the sweep. A room on
  another machine with no `last_room` saved hasn't been seen found. The sweep's coverage of a /20 was
  checked in unit tests only.
- Whether the GitHub button reaches a macOS browser. ShellExecute returned success inside Wine.
- The BB minigame in True cooperative, rescue while the host is the victim, missed rescues in the
  real game, Foxy and Chica as players, balance of every number in the modes.
- Whether a background copy ignores the mouse on a real Mac.
- Whether the overlay looks right on screen. Its pixels were checked in in-process captures only.
- More than two players at once, play between machines on networks other than the owner's, and
  between a Mac and a Windows PC.
- Cutscene children walking left, up and down: those frames come from the animation table, but only
  standing and walking right were seen live. Scenes other than 2, and players in different rooms.
- Walking away from a door a friend holds, with a real mouse. The runs here moved a per-copy test cursor.
- A natural Versus kill by Bonnie, Chica or Foxy, with a player walking them to the door. Those scares
  were injected. Fredbear's and Nightmare's natural kills were run.
- Fredbear or Nightmare in the closet in the real game, and Last one standing with a Nightmare victim
  who isn't the last, which needs three copies.
- Nights 5 and 6 in Versus, where the game drives Fredbear's AI itself, and balance of these two sides.
- In the real game, not yet run: flashlight scares (EV_SCARE_*), closet hold, remote bed view,
  remote listening, Foxy's closet run from a remote side, the BB minigame, Nights 5 and 6.
- One old run with the host on Night 3 and the client on Night 2 left the host on the night frame
  about 7 s after its kill started. Not seen since, including on matched Night 3.
- How far the Android logic matches the Steam build beyond what was read here.
