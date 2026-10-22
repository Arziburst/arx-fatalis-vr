# Arx Fatalis VR — Traps

Things that already cost us time: misleading symptoms, stale state, instruments that lied. Read this
before debugging something that "makes no sense". **Add an entry in the same PR as the work that hit
the trap**; never delete entries (mark them *resolved* if the cause is gone). Newest first.

Format: **symptom** — what is really going on — what to do.

---

## 2026-10-20 — The playground goblin flees instead of fighting when its life is below 8 - a damage roll decides which (#532)

**Symptom:** `melee-damage.xrs` failed now and then (main runs 39128985121 and 39328879039, about 1 run in 17) at
group 7's `@assert playground.npcFighting eq false` (true) right after the mode lever, while the same tree passed the
merge gate. **Cause:** the goblin class script (`npc/goblin_base`, the plain goblin: life 12, `§cowardice 8`) runs
`IF (^LIFE < §cowardice) GOTO FLEE` in `ATTACK_PLAYER`, which the mode lever's `attack_player` reaches. Group 6's
thrust usually took the goblin below 8 (a hit of 4.3 or 6.5), so the lever made it flee - not fighting, the check
passed. When the engine's armour roll dealt nothing (`melee.hit ... damage=0.0 aim=1.00 ... kind=thrust` in run
39328879039) the goblin kept 12, the lever started a real fight (`BEHAVIOR -f MOVE_TO`, `SETTARGET -a PLAYER`) and the
check saw it. Whether a playground NPC fights or flees from `attack_player` depends on its life at that moment.
A second window in the same group: `playground fight` against the resting dagger - its touch ends the fight within a
few frames, so `@until playground.npcFighting eq true` on a slow status read could miss it. **Do:** set the life a
step depends on (respawn, then `playground hit <n>` - `damageNpc` with no armour roll - and wait on `npcLife`) instead
of inheriting it from earlier hits; keep touches off (`melee noswing none`) until a fight set for a touch shows.
`scripts/dev/melee-damage-stress.ps1 -FullLife` / `-SlowFrames 30` force both cases.

## 2026-10-20 — A refresh-rate record is short of its frames (`turn72.frames` 62 < 64) under load, though every other frame was on time (#553)

**Symptom:** `refresh-rate.xrs` failed a `motion.records.<x>.frames ge N` guard now and then, beside other games or in
the merge gate (`turn90.frames` 76-80 < 81, `turn72.frames` 62 < 64), and passed alone. `arx-perf.log` of the gate
(CI run 39359033513) had one frame of **173.8 ms, `cause=channel`, channel=159.6 ms, missed=11** inside turn72's
window: 73 - 11 = 62. **Cause:** the test channel wrote `status.json` (temporary file, rename, `Logger::flush`) and
`ack.txt` on the main thread, every 100 ms and after every batch. The #23 fix made the rename retry every millisecond
instead of sleeping 100 ms, but a rename still waits as long as something holds the file: a scanner's or an indexer's
oplock on `status.json` under I/O load breaks only when its holder lets go. The simulated headset, paced to the display
clock, skips the display slots a long frame missed, as a headset does - so the record really ran short of frames, and
the guard was right. The routine writes cost 7.6-12.9 ms of a frame too (ten such hitches in that run).
**Do:** the channel builds the status on the frame and hands it to its writer thread (`ChannelWriter`,
src/core/ChannelWriter.h, host test `ChannelWriterTest`), which writes the file, renames it, flushes the log and
writes the ack after it - the status before the ack (#23) holds on that thread. **The second half of the trap:** a
sequence reads `status.json` right after a log line it waited for (`@log`, then `@assert`), and the main-thread write
never showed a line newer than the status on disk - nothing was logged between building, writing and flushing. The
first writer-thread version flushed `arx.log` after the status landed while the frame went on logging, and the full
suite failed `scene-switches` (`@log vr\.scene state=start` matched, `scene.active` read false: the command's event
was logged in the same frame, after the status was built). So each status carries the count of log lines at its build
(`LogCounter::lines`), the writer flushes only when no line came since, else the next frame builds a fresh status
that flushes it. The ack does not wait for that fresh status: a second try that held the ack for it failed
`melee-damage` (`playground fight` acked with the state of a frame later, when the goblin's script had dropped the
fight already) - a test reads its batch's own state right after the ack. A frames guard that fails is not to be
loosened: look for an `H` line with `cause=channel` in the record's window, and for `[channel] slow frame part:` in
`arx.log`, which names what is left of the channel on the frame (commands, the status build, the records) when it
takes 20 ms or more. Reproduce with `scripts/dev/refresh-rate-stress.ps1 -SlowStatusMs 150` (an oplock on each
`status.json` replacement): red every time with the write on the frame, green with the writer thread.

## 2026-10-19 — `abort()` / `std::terminate()` leave no crash report, the process ends with 0xE06D7363 or a fail-fast 0xC0000409 (#545)

**Symptom:** the channel's `abort` ended the game with exit code 0xE06D7363 (a C++ exception) and `terminate` with
0xC0000409 (`STATUS_STACK_BUFFER_OVERRUN`), nothing from the crash handler in `arx.log`, the dump helper's folder said
`kind=terminated`. The `crash` command (an access violation) was reported fine. **Cause:** two faults inside the crash
handler itself, on the path a signal or `terminate()` takes (no exception pointers), which #132 had never exercised:
(1) `CrashInfo` is `#pragma pack(1)` and lives in shared memory, so its `contextRecord` is not 16-byte aligned, and
`RtlCaptureContext` writing straight into it faults (`movaps`); (2) `SignalHandler` passed `_pxcptinfoptrs` for every
signal, but the C runtime fills it only for the signals it raises from an exception (SIGSEGV, SIGFPE, SIGILL) - for
`abort()` it is a stale pointer into a stack that is gone. The nested fault re-entered `handleCrash`, which took its
plain `std::mutex` a second time on the same thread (`system_error` → 0xE06D7363) or, via `terminate()`, ended in the
runtime's fail-fast. **Do:** capture the context into an aligned local and copy it; pass no pointers for SIGABRT /
SIGINT / SIGTERM; a recursive mutex and a re-entry guard in `handleCrash`. When a crash path ends with a code that is
not the crash's own, suspect the handler before the game: the flight recorder's `note` events (`crash handler: ...`)
in the report's `arx-recorder.txt` show how far it got.

## 2026-10-19 — A check started with `Start-ThreadJob` "ends without launching the game", yet its smoke test passes (#545)

**Symptom:** every case of `flight-recorder-check.ps1` failed with "the game was launched (smoke-test exit 0)" while
the smoke run under it ran its full time and passed. **Cause:** `Wait-ArxGameLaunched` treated any job state but
`Running` as "the job ended" - and a thread job is `NotStarted` for a moment right after `Start-ThreadJob` (a
`Start-Job` child is `Running` at once, which is why every older check was fine). **Do:** `NotStarted` is "on its
way", not "ended" (`scripts/dev/test-lib.ps1`); a check that sees this symptom has not driven the game at all, so
nothing it asserts about the run means anything.

## 2026-10-18 — A 118 ms hitch of cause `channel` in the middle of a pacing measurement: `status.json` is renamed with a Sleep(100) (#23)

**Symptom:** `refresh-rate.xrs` failed one run in two beside another game with a number that made no sense
(`turn72.turned` -58.6 for -60, `walk72.walked` 141 for 129, a jump's air time off by 12 ms): `arx-perf.log` showed
one frame of **117-119 ms, 8.5 slots at 72 Hz, `cause=channel`** inside the span, always about that long.
**Cause:** the test channel writes `status.json` as a temporary file and renames it over the old one; the engine's
`fs::rename` (FilesystemWindows.cpp) retries a file a reader holds open with `Sleep(100)` per try, and the runner
reads `status.json` all the time - the game's main thread then sleeps 100 ms in the middle of the frame. A hitch that
long moves what a stick held for 500 ms did (the long frame takes the whole interval at the held value), and the
Euler step of the walk. **Do:** the channel replaces the file itself (`core::replaceFile`, src/core/FileReplace.h, host test
`FileReplaceTest`): a write a test waits for tries every millisecond until the reader lets go (2 s at most, then it
stays pending and every frame tries once more - a waited-for write is never dropped), the 250 ms timer's write gives up
when a reader holds the file and the next one comes. After it, five runs beside a paced third game: 5 of 5 (before: 3 of 5). When a
pacing number looks wrong, look in `arx-perf.log` for an `H` line at that time before the code. *Since #553 the whole
write is off the frame, on the channel's writer thread (the 2026-10-20 entry): a held file delays the file, not a frame.*

## 2026-10-18 — `@assert` right after an acknowledged command reads the state of the command before (#23)

**Symptom:** the merge gate (full suite, two lanes, the CI runner busy) failed `scene-switches` at `@assert scene.view eq
observer` with `scene.view (first)`, 7 s into the run, right after `scene view observer` was acknowledged (`seq=2`).
Green on main and in 10 of 10 local runs. **Cause:** the game wrote `ack.txt` first and `status.json` after it (status
build, temporary file, rename); the runner polls the ack every 10 ms and reads `status.json` at once - when the PC is
loaded, the status of the batch is a few milliseconds late and the read gets the file of the batch before. It was
first thought to be a dropped status write (the 20 ms patience of the first fix): the gate's `arx.log` has no
"could not write" line, so it was not - and a dropped or delayed write would have been read stale the same way.
**Do:** the batch's end writes the status first and the ack after it (CommandChannel.cpp, `runBatch`): an ack always
finds the status of its batch. When an `@assert` after a command reads the old value, look at that order and at
`status.json`'s `cmdSeq` before the code under test.
## 2026-10-18 — A jump's air time varied by up to 60 ms between runs: the "fixed timestep" loop stepped by all the time that had added up (#23, #537)

**Symptom:** `jump<rate>.jumpSeconds` differed by up to 57 ms between two blocks of one run beside a busy third game
(0.289 ... 0.346 s), and by 63 ms on the quiet CI runner (0.299 at 90 Hz, 0.362 at 120), though every apex was 130.
**Cause:** `ARX_PLAYER_Manage_Movement`'s loop looked like a fixed step (`while(DeltaTime > FIXED_TIMESTEP)`) but
passed the WHOLE pile to each iteration and took 25 off it - a step of 25-35 ms depending on the rate's phase, and
of a hitch's whole length after one (its own TODO said so: the forward push was applied in the first iteration
only). An Euler fall with a variable step, and a per-step friction, follow the frames. And the remainder was
integrated twice: 25 ms of frames became 29-33 ms of physics, so the jump everyone knows fell 1.2-1.33 times
quicker than `WORLD_GRAVITY` says - a true fixed step of 25 ms made the jump 10 % longer in the air (0.34 s above
50 cm for 0.31) until that was put back as a time scale. **Do:** the jumping player moves in fixed 25 ms steps from
a `FixedStepClock` (src/game/JumpPhysics.h), the input re-armed for every step, each step integrating
`jump_time_scale` (1.2) times its length; the flight is a function of the step count (JumpPhysicsTest). When a
"fixed step" loop misbehaves, check what it passes to the step, not only how often it runs - and measure the old
arc before replacing the loop, the game may have been tuned to its bug.
## 2026-10-13 — The same 500 ms walk measured 77, 100, 107 and 109 units at four rates (#23)

**Symptom:** a walk timed exactly in display time gave wildly different distances per rate, and not in the
order of the rates - it looked like a rate-dependent walk. **Cause:** each measurement started with `setpos` to
the same spot. `setpos` puts the player where the engine starts a fall (`player.falling`): a per-frame trace
showed him hanging 16 units above the floor in the jump's end phase for seconds, and `falling` still set when the
walk began, so the walk started from a landing. The walk itself is the same at every rate (127.7-129.7 units once
nothing was teleported). **Do:** never teleport between measurements that compare motion; return by motion
(`stick-for` back, a turn back by time). Wait for `player.jumping eq false` and half a second after any teleport.

## 2026-10-13 — A jump's "time in the air" read 0.40 s or 1.0 s at the same rate (#23, #435)

**Symptom:** the time above 20 cm of the same jump differed by 0.6 s between runs. **Cause:** the engine moves a
jumping player in 25-35 ms steps; a step that would end below the floor is refused, so the player hangs where the
last step ended - 6 or 24 units above the floor - until the landing animation ends, then snaps down. A gauge
below that height reads the hang. **Do:** measure a jump above 50 cm (`MotionProbe::AirborneUnits`); the hang is
#435.

## 2026-10-13 — A swing peaked at 2x or 3x its speed, a slow hand made a phantom swing, on the simulated headset (#23, #352)

**Symptom:** a 9.4 m/s swing peaked at 18.89 or 28.33; a 1.2 m/s return counted as a swing. **Cause:** a frame
hitch (here: the 22-26 ms that come with each `Cursor escaped the window` warning in a background run, #198) makes
the runtime skip one or two display slots; the swing detector's median then moved over two or three slots and
was divided by one frame's step (#352). **Do:** look for `H ... missed=` lines in `arx-perf.log` at the swing's
time before suspecting the detector; #409 fixes the divisor.
## 2026-10-18 - A channel batch vanishes: no ack for 60 s, no line in arx.log, and the quit afterwards takes the lost batch's seq

**Symptom:** a sequence step (`@frames 5` in vr-remap, CI run 39120032077 attempt 1) times out "waiting for the game to
acknowledge 'waitframes 5' (last ack seq 14)" while the game renders at 90 fps, its hang watchdog says nothing, and
`arx.log` has no `[channel]` line about it; the evidence's `ack.txt` then says `seq=15 ... last=quit` - the quit got the
number the lost batch should have had, so the game never read `command.txt`.
**Cause:** the harness writes `command.txt.tmp` and renames it into place (`Move-Item`). On Windows the renamer still
holds the file open with DELETE access for a moment after the new name is visible, and an open that does not share
delete - `std::ifstream`, so `fs::read` - fails with a sharing violation then. The channel read `""`, deleted the file
(DeleteFile shares everything, so it succeeded) and acknowledged nothing. The moment is microseconds on an idle PC and
grows with every preemption of the writer's thread: it hit under three games (two CI lanes and a worker's). Seen once in
about 900 launches.
**Do:** read a file another process renames into place with `FILE_SHARE_DELETE` (the channel does, with a retry next
frame when the open fails: `readCommandFile` in `src/core/CommandChannel.cpp`). When an ack is missing, read the
evidence's `ack.txt`: a quit with the missing seq means the batch never arrived; a quit with the next seq means the ack
was written and the harness missed it.

## 2026-10-18 - A CI job's phases end SKIPPED after 900 s although nothing of the job holds a slot; a killed ci-local leaves a game for hours

**Symptom:** run 39109130415 (main): every phase SKIPPED, the lock lines name two workers' `make-test-saves.ps1` /
`smoke-test.ps1` games that took the slots as fast as CI released them. #526: a worker's `ci-local.ps1` run as a tool
command with a 10-minute timeout; the timeout killed the pwsh parent, lane 1's `arx.exe` lived on for two hours holding
a slot, and another worker's `ci-local` starved on it.
**Cause:** the lock had no priority (first come, first served, 900 s), and a game started with `Start-Process` is not a
child the OS ends with its parent: a killed launcher drops its lock handle (the slot looks free to the probe) but the
game keeps running, and `Get-ArxGamesOutsideLock` still counted it as "inside the lock" because the stale lock file
named its pid.
**Do:** #527 - CI's runner marker makes workers wait without a slot while a job runs; the games of a script are in a
job object that kills on close (`Register-ArxGameProcess`, and `Register-ArxLauncherChain` for the whole `ci-local`
chain); the next launch stops an orphan (lock file not held, launcher gone, `arx.exe` alive) and says so. Run
`ci-local.ps1` detached or in the background, never under a short tool timeout (WORKFLOW.md).

## 2026-10-18 — The unpaced simulator makes a slow hand move a swing: why idle.vrsim is a 90 Hz cap, not rate 0 (#522)

**Symptom:** with `rate 0` in `idle.vrsim` (to stop the 90 Hz display clock halving a 10-12 ms CI frame to 45 fps),
`melee-swing.xrs` failed on the owner's PC at line 30 `@assert vr.melee.swings eq 0` (read 3), before any swing.
The run's pacing line: `278.022/s on 90.001 slots/s`.
**Cause:** the sequences' ramps count frames and were written for about 90 per second. At 278 fps the "slow" set-up
move `vr-sim @+1-60 hand right pos …` (0.43 m in 60 frames, under 0.7 m/s at 90 fps) lasted 0.22 s, ~2 m/s, and the
8-frame swings (0.85 m) became 29 m/s, over the 20 m/s tracking-jump limit. An unpaced simulator gives every PC its
own speeds; the CI runner (~85-100 fps unpaced) and a fast desktop disagree.
**Do:** sequences run on `rate 90 cap` (the simulator's cap: frames at least 1/90 s apart, a late frame starts at
once, no slot skipped) - 83-90 fps on CI, 90 on a fast PC, the ramps mean what they say. Use `rate 0` only for a
script that measures what the PC draws (`msaa.vrsim`), the clock (`basic.vrsim`) only for pacing checks.

## 2026-10-18 — A background process started in one workflow step is gone in the next (#522)

**Symptom:** the CI step "Script self-tests (join)" fails with "ended without writing an exit code"; the tests' output
stops a few seconds in, in the middle of the first test. Locally the same command runs to the end.
**Cause:** the start step used `Start-Process ... -RedirectStandardOutput`: that starts the process with handle
inheritance, so it also inherits the step's own stdout/stderr pipes to the runner. The step's pwsh exits, the pipes stay
open, and after ~5 s the runner kills the step's whole process tree (`_diag\Worker_*.log`: "Need kill all child processes
trees", the start step taking 5 s instead of 1). A local run has no runner waiting on a pipe.
**Do:** a process that must outlive its step is started without redirection (`Start-Process` without `-Redirect*`,
`-WindowStyle Hidden`) and redirects its own output (`*> out.txt` inside its command). Check: the start step takes about
a second; the runner's Worker log shows no kill of the step's children.

## 2026-10-18 — A script self-test's time limit fails in CI only: the self-tests run beside the build (#522)

**Symptom:** a `scripts/dev/test-*.ps1` check with a wall-clock limit (e.g. `test-tree-fingerprint.ps1`'s fingerprint
of the real copy under 1000 ms, 175 ms on an idle runner) passes locally and fails in CI, now and then.
**Cause:** since #522 CI starts the script self-tests in the background and builds at the same time: the MSVC build
takes every core, so a timed check gets a fraction of the CPU it gets alone.
**Do:** read a timing failure of a self-test in CI as load first (re-run, or run `run-script-tests.ps1` alone); never
widen the limit to make it pass - a check that measures wall-clock time belongs in a quiet step or measures work instead.

## 2026-10-18 — `ci-smoke.ps1` dies in `New-ArxSequenceUserDir`: a file of another sequence's user dir is in use (#522)

**Symptom:** a full `ci-smoke.ps1` run stops at a sequence with `Remove-Item ...\ci-userdirs\real-input\arx-perf.log ...
being used by another process`, while another working copy's game checks run on the same PC.
**Cause:** the per-sequence user directories (#296) lived in `%LOCALAPPDATA%\ArxFatalisVR\ci-userdirs\<sequence>`, one
root for every working copy (the build, test channel and user directory carry `<folder>-<hash>`, this root did not). Two
copies running the suite delete and reuse each other's directories - one copy's fresh directory is another's running game.
**Do:** every per-copy folder gets the copy key: `Get-ArxCiUserDirRoot` (`ci-userdirs\<folder>-<hash>`). A new folder a
test run writes to is per working copy (and per lane when two lanes could use it at once) from the start.

## 2026-10-18 — A melee sequence's timing passes locally and fails on CI: the timeline counts frames (#409)

**Symptom:** `melee-recovery.xrs` passed locally (90 fps) and failed on CI at `@assert vr.melee.hits eq 1` (read 2).
The CI run's VR pacing was UNDER-SUBMITTING at 41.8 fps.
**Cause:** `vr-sim @+33-40 ...` is 33 *frames* after the batch, not 0.37 s. At 41.8 fps it is 0.8 s, past the dagger's
0.6 s recovery, so the second swing hit as it should. The simulator's timeline and `@frames` count game frames, and a
busy runner plays far below the simulator's rate.
**Do:** never put a time window between frame-scheduled events. Either make the gap as short as the test allows, so
that it stays inside the window at any rate, and assert the mechanism's own marker. Or wait on a time the game
reports (`@until vr.melee.recovery.left eq 0`) before the next step.

## 2026-10-17 — The main menu ignores the trigger while the Index controller is held firmly (#496)

**Symptom:** New game does not react to the trigger in the headset. `arx.log` has `[vr] input: use_right
(right.grip+right.trigger)` for every press, no `interact_right` and no `vr.pointer.click`; holding the controller loosely,
the same press clicks.
**Cause:** a firm, resting hold of the Index sits at 0.52 - 0.59 on `squeeze/value` (`[vr] input: right grip down
at 0.55 (threshold 0.5 / 0.3)`), over the 0.50 press the 2026-10-10 entry below chose for a light close. With the
grip down, the trigger completes the grip + trigger chord (use / act, D-042) and its own binding, the click, does
not fire: a chord consumes its second control.
**Do:** the Index grip presses at 0.70 and lets go at 0.40 (`IndexGripThreshold`, cfg `vr_grip_press` /
`vr_grip_release`): a resting hold is no grip, and a closed hand's flicker stays one hold. In the menus a hand
pointing at the panel makes no chord at all (`ControlResolver::setChords`, `src/vr/VRInput.cpp`): its trigger
clicks whatever the grip reads, and the menus' right click is A (D-053). Read the grip value in the crossing lines or `vr.input.grip` before blaming the
pointer; `menu-trigger-grip.xrs` holds the grip at 0.55 and 0.95.

---
## 2026-10-17 — GL pixel-store state is global, set it per upload: an intermittent access violation inside the AMD driver under `glTexImage2D` while a cinematic loads (#495)

**Symptom:** the owner's headset run crashed right after character creation, `atio6axx.dll` under
`OPENGL32.dll!glTexImage2D` ← `GLTexture.cpp:142` ← `CinematicTexture.cpp` while `akbaa_dream_c.cin` loaded its
images; two runs before it in the same session passed the same point. The faulting address was page-aligned
(`0x28011cd0000`): a read just past the end of a heap region. Nothing in the cinematic code had changed.
**Cause:** the GL pixel-store state. The engine sets `GL_UNPACK_ALIGNMENT` to 1 once at start-up
(`OpenGLRenderer.cpp`) and every `Image` is tightly packed (stride = width × bytes per pixel); `GLTexture::upload`
trusted that. The menu pointer's beam (`VRSystem.cpp`), the hand reticle rings and the desktop mirror's overlays
(`VRMirror.cpp`) set the alignment to 4 for their RGBA uploads and never restored it - the log shows the beam image
created in the main menu (`menu pointer: beam image 8x64`) a few lines before `Loading cinematic`. `akbaa dream.tga`
is 1390 × 1390, a 16-bit TGA loaded as RGB (3 bytes per pixel) and cut into 256-wide tiles whose last column is
110 wide: a 330-byte row, read 332 bytes apart under alignment 4, so the 110 × 256 tile (84480 bytes) was read to
84990 - 510 bytes past its buffer, a crash only when the buffer ended near a page boundary. 17 cinematic images in
12 of the 19 cinematics have such a last column (tile widths 50, 51, 66, 81, 86, 110, 139, 161, 199, 207); plenty
of other RGB textures of the game do too, so the crash could have landed anywhere after the first overlay.
**Do:** never trust global GL client state across modules: `GLTexture::upload` now sets the tight unpack state
itself before every `glTexImage2D` and logs an `[E]` with the numbers when the state it found is not the tight
one (`pixelstore::bytesRead`, `graphics/image/PixelStore.h`, host-tested) - the leak, not only the overflow: a
check that fired only when the dirty upload itself overflowed stayed green with the leak put back, because the
first texture after the beam happened to be RGBA and the reset hid the state from the cinematic tiles that
followed. Code that needs another state sets it and puts it back. A crash inside the driver under a texture upload with a page-aligned address is
an out-of-bounds read of the client buffer - compute the bytes GL reads (alignment, row length, skips) against
the image's size before suspecting the driver. `cinematics.xrs` loads all 19 cinematics after the beam was created.

## 2026-10-17 — A scripted run waits forever on a message box; logging it from the critical error dialog hangs (#501)

**Symptom:** an unattended run (a worker, CI, autopilot) never ends: `arx.exe --no-such-option` or a
critical error at startup leaves a "Critical Error" message box on screen and the script waits on it. Simply
logging the dialog where it is shown, instead of showing it, would hang the game at exit.
**Cause:** the critical error dialog is a log backend (`logger::CriticalErrorDialog`): it shows its dialog
in its destructor, which `Logger::shutdown` runs while it holds the logger's (non-recursive) mutex - a
`LogError` from there waits on that mutex forever. And `ARX_UNATTENDED` only silenced the crash processor.
**Do:** scripted launches set `ARXVR_NO_DIALOGS=1` (`Set-ArxDialogEnv -NoDialogs`, restored after the start; [TESTING.md](TESTING.md#runs-without-dialogs-501)):
every dialog becomes a `[dialog]` line and an error exits with code 1. `Startup.cpp` removes and deletes the
critical error dialog *before* `Logger::shutdown`, so its line reaches the file and stderr. Never log from a
log backend's destructor or `log()`.

## 2026-10-17 — A weapon pushed through a wall does not ring on it (#465)

**Symptom:** in `melee-damage.xrs` a fast swing along the playground's north wall left `vr.melee.worldHit` false, though
the blade's tip was 29 units past the wall's plane (status `tip` z 8429, wall z 8400).
**Cause:** the engine's strike check tests the weapon's hit spheres only, not the blade: the dagger has one, radius 15,
about 89 units ahead of the player (`melee.spheres count=1 ... at=8191.2,-305.7,8444.3/15.0`). A sphere wholly behind a
thin wall touches no polygon of it - the engine's own strike never reaches that far into a wall, a VR hand does.
**Do:** to strike a wall in a sequence, put the hit sphere across the wall's plane (read `melee.spheres`); do not
judge by the blade's tip.

## 2026-10-17 — The playground's goblin drops its fight when it is hit, so it never counts as fighting the player (#465)

**Symptom:** in `melee-damage.xrs` the playground goblin in the aggressive mode (the mode lever: `npcMode`
`aggressive`), even after a real swing hit it, did not reach `playground.npcFighting` (its fight behaviour
`BEHAVIOUR_FIGHT` *and* the player as its `targetinfo`) within 15 s - so a VR touch, which reaches only an NPC fighting
the player, never reached it. The save's goblin (`mvp-goblin-fight`, `melee-swing.xrs`) does reach it after a hit.
The same goblin set fighting with `playground fight` took one touch (`aim=0.10`), and every touch after it was refused.
**Cause:** the playground goblin's script drops the fight when it is hit: right after the first touch's hit,
`melee.touch target=goblin_base_9001 result=refused fight=no targetsPlayer=no`, and `npcFightBehaviour` false before
the next sweep (CI run 38995941095 and a local run on f4720ca27). Its own fight never starts from the mode lever or a
swing's hit for the same reason - the hit's reaction clears what `attack_player` set. Which script line does it is not
known: the class script is the game's (`npc/goblin_base`); the instance script only refuses `ouch` / `aggression` while
peaceful. The save's goblin (`mvp-goblin-fight`) keeps fighting after hits.
**Do:** to test what only fighting NPCs feel in the playground, set the fight with `playground fight` right before the
check, let nothing hit the goblin in between (a peaceful-in-fact goblin refuses touches: `melee.touch ... refused`),
and check the first hit only; `npcFightBehaviour` / `npcTargetsPlayer` show which half is missing.
**Corrected 2026-10-20 (#532):** the fight never started from the mode lever because the goblin's life was below its
cowardice, not because of a hit's reaction: see the 2026-10-20 entry. At full life the lever does start a real fight.

## 2026-10-17 — A changed cfg.ini default does not show in a test run (#465)

**Symptom:** after `vr_melee_noswing`'s default changed from `none` to `min` in the code, `melee-damage.xrs` failed at
`@assert vr.melee.params.noSwing eq min` (`none`) on a fresh build.
**Cause:** when the game saves its config it writes every key with its current value - a default it read is written as
a value. A working copy's test runs share one user dir (`userdirs\<folder>-<hash>`), so from then on cfg.ini holds the
old default as if the user had chosen it, and it wins over the new default. The same happens to a player who ran an
earlier build.
**Do:** a sequence that needs a value sets it live (a channel command such as `melee noswing min`) instead of trusting
the default; prove the default itself in a host test (`MeleeSwingTest::testContactAim` checks `DamageParams`), or run
with a fresh user dir. A key whose empty value means "the code's default" (`vr_melee_slash_<class>`) does not have
the trap.


## 2026-10-14 — The Arx arm grows a second elbow in the middle of the forearm, though the IK has only one (#466.1)

**Symptom:** with the Arx arms drawn (`arms arx`) the owner sees a second elbow and wants the arm one solid
piece up to the elbow; the research blamed the flat upper-arm rings (H1) or a few faces spanning the upper arm
and the wrist (H2), and later the upper arm's last ring past the elbow (F-308).
**Cause (measured on `human_base`, both arms, the `[vr] body: ... Arx arm` bind line):** the forearm group
`right_elbow` holds only the forearm's first third - 11 vertices, 2.5 .. 11.3 units past the elbow on a 30.5 bone.
The other two thirds are skinned to the **wrist** group: 8 vertices at 10.7 .. 20.1 past the elbow (14 of the wrist
group's vertices lie before the wrist joint, the wrist ring included). So every bend of the wrist and the wrist
band's share of the roll fold the forearm in its middle, across the 12 faces between the two groups. One of those
wrist vertices (671 right, 10.7 past the elbow) is joined straight to the upper arm's 92 and 674 by 3 faces (143,
1198, 1210): H2's web. The elbow ring itself mixes 6 upper-arm vertices (up to 3.1 past the joint) with 4 of the
forearm (2.5 .. 3.9), so even the elbow folds in two bands. H1 is not it: only one upper-arm ring is flat (mid
upper arm, rigid on one bone). Measured with `vr.body.<hand>.classic` (`arms arx`): 2 - 4 bands bent past 20
degrees at the elbow bent 57 - 111 degrees, the forearm rings' roll spread up to 55.6 degrees with the hand
rolled 180.
**Do:** read the arm by where its vertices are, not by their group (`vr/ArxArmFix.h` `diagnoseArxArm`). `arms
classic` re-skins by ring in VR only (`skinPlayerArms` after the engine's transform): one band bent, spread 0.
A crease count on a **straight** arm is 0, not 1 - pick the bent poses for `eq 1`. human_base reloads on every
equipment change; the classic parts are rebuilt in `bind`, which runs again for the new object.
## 2026-10-14 — The build fails with `git.exe ... missing and no known rule to make it` after a Git update (#377)

**Symptom:** `build-windows.ps1` stops at "Building" with
`ninja: error: 'C:/Program Files/Git/mingw64/bin/git.exe', needed by 'arx-version.rc', missing and no known rule to make it`.
The same build directory built a few minutes earlier, and `git` works in the shell.
**Cause:** Git for Windows 2.56 moved its binaries from `mingw64\bin` to `ucrt64\bin`. CMake found git once and keeps
its path in the build directory's cache (`GIT_COMMAND:FILEPATH` in `CMakeCache.txt`); the version rule depends on that
file, so ninja refuses to build. Every existing build directory has the old path; a fresh one does not.
**Do:** set `GIT_COMMAND:FILEPATH=C:/Program Files/Git/ucrt64/bin/git.exe` in that build directory's
`CMakeCache.txt` (or build with `-Clean`), then build again.

## 2026-10-14 — `input-check.ps1` / `game-input-check.ps1`: trigger and face-button presses are not counted on `--loadlevel 10` (#473)

**Symptom:** the control table check reports `use_right`, `act_right`, the chord's `jump` and `book` as 0 presses;
only the stick taps (jump, crouch, the stick click) count. Neither check runs in CI.
**Cause:** level 10 starts with a scripted scene (`scene state=start ... controlsBlocked=yes`). Since #394, A, B and
the triggers skip a playing scene and reach nothing else, so the presses the check sends in the scene's first seconds
skip it (`vr.skip what=SCENE` in arx.log) and never reach the control table.
**Do:** until #473 is fixed, do not count these two checks as evidence; the `.xrs` sequences wait for
`player.controlsBlocked eq false` first.

## 2026-10-14 — `playground-hands.xrs`: the left hand's reticle misses the pulled lever on CI, passes locally (#377)

**Symptom:** CI run 38477911299 (PR #456) failed at `@until vr.reticle.left.under eq lever_9002 5s`: the ray hit
the wall at (8378.7, -128.4, 8400), right behind the handle. The same sequence on the same commit passed 243/243
locally; both runs aimed at the same face (8378.6, -130.5, 8399.2).
**Cause:** the hands of the idle rig still rise after a `setpos` - the left hand's start was y -135.6 at the
first aim, -127.2 at the second and -125.1 a moment later on the busy runner (locally the second aim already started
at -125.1). `vr-point` aims from where the hand is in that frame, so the ray drifted 2 units up past the pulled
handle, which lies 0.8 units in front of the wall.
**Do:** after a teleport give the hands 30 frames and aim at a thin target once more before waiting on the reticle
(`playground-hands.xrs` does this for the lever).

## 2026-10-13 — A quad layer is in every simulated shot and invisible in the headset (#376)

**Symptom:** the radial menus passed their sequence on the simulated headset (drawn, `overlayPixels` in both eyes)
and could not be seen at all on the owner's Index, while the log showed them opening, hovering and running items.
**Cause:** their swapchains were made lazily, the first time a menu opened - in `updateInput`, inside the frame,
while the flat frame quad's image was acquired. SteamVR answered `xrCreateSwapchain` with
`XR_ERROR_RUNTIME_FAILURE` (one `[E] VRSwapchain` line, then the menus worked unseen); the simulated runtime made it.
Every other lazily made swapchain (beam, reticles) is made in `submitFrame` after the quad's release.
**Do:** make swapchains with the session, or at least with no image of any swapchain acquired. The simulated runtime
now refuses `xrCreateSwapchain` while another swapchain's image is acquired, with an `[E]` line, as SteamVR did; read
the run digest of a headset run for `[E] VRSwapchain` before suspecting the composition.

## 2026-10-13 — In VR the inventory will not close and melee is refused after New game or `--loadlevel` (#394.5)

**Symptom:** a new game or a `--loadlevel` start in VR: the inventory stays open (closing it reopens it), `melee.state state=off reason=inventory`; a save load is fine, and so is New game in a player's launch.
**Cause:** the engine's freelook (`TRUE_PLAYER_MOUSELOOK_ON`) starts `false`; off is cursor mode, in which `ARX_INTERFACE_Manage` reopens the inventory every frame. Only `ARX_GAME_Reset` with `!DONT_ERASE_PLAYER` (a save load, or level 10 of a player's launch through `DanaeClearLevel`) turns it on; `--loadlevel` never resets and `DANAE_StartNewQuest` sets `DONT_ERASE_PLAYER`; the pause menu turns it off. VR has no freelook control.
**Do:** ArxGame turns it on whenever the game enters `GAMEPLAY` in VR (a `vr.freelook` event). Check `player.freelook` / `player.inventoryOpen` before blaming the inventory or melee code. An event of the first loaded frame is logged before a sequence's first mark: assert it with `events.byName`, not `@log`.

## 2026-10-13 — A scripted scene's skip "refused" although the scene was skipped (#394.4)

**Symptom:** `scene.skip ... refused=yes` for a skip of the prison intro, yet the controls come back 5 s later and the hero stands at `MARKER_0112` (the intro's own end takes about 35 s).
**Cause:** `SendMsgToAllIO(key_pressed)` returns `REFUSE` when **any** of the level's scripts refuses the event; every script still gets it, and the scene's skip handlers end the scene. The engine only uses the result to decide whether the speech is cut (`REQUEST_SPEECH_SKIP`).
**Do:** judge a scene skip by what the scene does (`scene state=end`, `player.controlsBlocked`, the hero's place), not by `refused`; `result=early` (a fade, or the letterbox's first 3 s) is the real refusal.

## 2026-10-13 — A sequence that changes a setting fails on its second local run (#394.4)

**Symptom:** `first-run.xrs` passes, then fails at `@assert settings.crosshair eq true` when run again with `xrs-run.ps1`.
**Cause:** the isolated user directory of a working copy is kept between launches, and `cfg.ini` in it still has the crosshair the previous run turned off. CI starts from an empty directory.
**Do:** delete `cfg.ini` in `%LOCALAPPDATA%\ArxFatalisVR\userdirs\<copy>\` before re-running a sequence that changes a setting.

## 2026-10-13 — The level 10 intro is the original game's startup, not New game's (#394)

**Symptom:** a test of "the intro" from `--loadlevel 10`, `--skiplogo` or New game shows a different thing than a player sees; New game never plays level 10.
**Cause:** the original start is logos -> level 10 (a scripted scene that plays `introduction.cin` and ends in the main menu through the player's death, `ARX_PLAYER_Manage_Death`) -> main menu. New game loads area 1 (`DANAE_StartNewQuest`).
**Do:** test the intro from a player's launch (`@player`, `-Player`); `first-run.xrs` / `first-run-intro.xrs`.

## 2026-10-13 — A one-frame virtual Escape never ends a cinematic (#394)

**Symptom:** a simulated Escape / skip press during a `.cin` does nothing.
**Cause:** `Menu.cpp` ends a cinematic on `isKeyPressedNowUnPressed`, which needs `keysStates == 1` after the release; a key held for one update goes 1 -> 0.
**Do:** #394 uses `g_vrSkipPressed` and calls `cinematicEnd()` itself; do not "fix" a skip with a faster key press.

## 2026-10-13 — In VR a logo is cut off at the window's size (#394, #73)

**Symptom:** a publisher logo on the VR screen fills only the window's size while the interface (the quad) is larger.
**Cause:** nothing prepares a camera for the logos, so the GL viewport stays the window's while the interface is the larger quad (#73).
**Do:** `GRenderer->SetViewport(g_size)` for 2D drawn outside the camera paths.

## 2026-10-13 — The character creation screen is black but for the portrait on a cold development start (#394.3)

**Symptom:** `first-run-chargen.xrs` started at the main menu by `@args --dev-launch` fails at the window shot (`capture.topRight.verdict` black; the quad shot shows only the hero's portrait on black). The same sequence passes when the process first played the level 10 scene (`@args --skiplogo`), and `first-run.xrs` (a player's launch) passes through the same screen.
**Cause:** not found; a longer wait before the shot (600 frames) changes nothing, so it is not the fade-in. Probably something the level 10 load initialises that a cold menu start does not.
**Do:** tracked as #399; `first-run-chargen.xrs` stays on `@args --skiplogo` (90 s) until it is fixed - do not loosen the shot's check.

## 2026-10-13 — In VR the main menu sits in a corner of the desktop window and the mouse clicks invisible buttons (#334)

**Symptom:** with VR on, the desktop window showed the main menu (and the character creation) as a small picture
in one part of the window, the rest dark; the owner reached the character creation only by clicking where nothing
was drawn. The headset itself showed the menu whole.
**Cause:** the desktop mirror (#21) showed the right eye, and in the eye the menu is the floating panel (#49) 0.9 m
ahead and 0.3 m down - a trapezoid at the bottom (or wherever the panel was placed). The desktop mouse, though, is
mapped from the whole window onto the whole 2D interface (#73), so it hit widgets where the window showed none.
**Do:** nothing since #394.2: while a menu without the world is shown (`ARXmenu.mode() != Mode_InGame`) the mirror
draws the flat frame over the whole window (`[vr] desktop mirror: a menu without the world - the window shows the
2D interface`); the in-game menus (inventory, book) keep the eye. `capture.<quarter>.verdict` of a window `shot`
tells a picture that fills the window from one in a corner of it.

## 2026-10-13 — `smoke-test.ps1 -Sequence` ignores the sequence's `@args`

**Symptom:** a sequence with `@args --skiplogo` run through `smoke-test.ps1 -VrSim idle -Sequence <name>` launched
`arx.exe ... --loadlevel 10`.
**Cause:** `@sim`, `@args` and `@save` are read by `xrs-run.ps1` and `ci-smoke.ps1`; `smoke-test.ps1` takes the
arx.exe arguments from its own command line (default `--loadlevel 10`).
**Do:** run a sequence with `xrs-run.ps1 <name>`, or pass the same arguments to the smoke test after `--`
(`smoke-test.ps1 -VrSim idle -Sequence first-run-menu -UserDir <dir> -- --skiplogo`) - the way to give it a user
directory of your own.

## 2026-10-13 — The smoke test's `loadlevel 1` "hangs" on the desktop and passes in CI (#440)

**Symptom:** `smoke-test.ps1 -Seconds 3 -Commands 'loadlevel 1','loadlevel 10','drawcheck 120'` failed every time on
the owner's PC (main too) with `timed out after 10 s waiting for the game to acknowledge 'loadlevel 1'`; the log
stopped at `level.load area=1`, no crash, no `Done loading level`. The same phase passed in CI, and level 1 loaded
in 250 ms from a save.
**Cause:** not a hang - a load bound by the present. The game's window in an interactive session is in the
background (shown without focus), and the compositor throttles its presents: every `SwapBuffers` took ~30 ms
(`present=30` in every frame of `arx-perf.log`). The loading screen draws a frame when 16 ms have passed and the
progress bar moved, but it counted the 16 ms from *before* its present - with a 30 ms present every step of the
load drew and presented a frame, and the change from level 10 to level 1 (hundreds of progress steps) outlasted the
10 s ack. In CI (the runner service, session 0, no compositor) a present costs nothing. The hang watchdog (#440)
gave the stack: `ZwWaitForAlertByThreadId` < `atio6axx.dll` < `wglSwapBuffers` < `SDL2` < `LoadLevelScreen` <
`loadLevel` < `ARX_CHANGELEVEL_PopLevel` < `ARX_CHANGELEVEL_Change` < `channel::execute` - but only once the
progress beat was taken out: every step still beat, so the watchdog rightly saw no hang.
**Do:** the 16 ms are counted from the end of the present now (level 10 → 1 in ~0.9 s with 15 loading frames, the
start's level 10 load 1.7 s → 0.22 s). A load that seems to hang: read `[hang]` in `arx.log` / `hang.txt`; no report
means the main thread still makes progress - the `[channel] level N loaded in ... the loading screen drew ...`
line and `present=` in `arx-perf.log` say where the time goes. Mind: a thread-stack walk from inside the process
must never load DbgHelp or query modules while the thread is stopped (a thread stuck in the loader holds that lock),
and must give up when the thread does not stop (a driver call blocks suspension).

## 2026-10-13 — `walk-look.xrs`: the walk left on a busy PC drifts 12.6 units in z (#370)

**Symptom:** `walk-look` failed on CI (run 38339943784, PR #391, nothing related changed) at its first push:
`ASSERT FAILED: player.z (8472.643) lt 8470` - the head turned left, the stick walked -x as it should
(`walk.dirX = -1`, `yaw = 89.748`), yet z moved +12.6. Green runs end that push at x ≈ 8347, z 8460.07.
**Cause:** the push was timed by the runner, not by the distance: stick on, `@frames 40`, four status reads,
stick off. On the busy runner (18.7 frames/s, worst frame 105 ms) that took 106 frames and 4.8 s (`vr.walk`
start t=7.94, end t=12.71) instead of 67 frames and 0.75 s, so the player walked the whole way to the start
cell's wall: x stopped at 8302.06. The cell is x 8302-8798 from the start at 8540, and walking into its wall
slides the player along it in z (measured on a quiet PC by walking on: z 8460.07 -> 8463.25 at the left wall,
+11.8 at the right one; the busy run's +12.6 is the same slide).
**Do:** bound a walk by its distance, never by frames or the runner's pace: `stick-for left 0 1 100` - the game
lets the stick go 100 units on - then `@until stickFor.active eq false` and assert on `stickFor.walk` (the walk
of its last walking frame, kept after the release). Mind the walls: a push needs room for its distance plus
the coast after the release (≈ 28 units at 90 fps).

## 2026-10-13 — `ui-panel.xrs`: the head-locked HUD loses half its pixels between two shots (#370)

**Symptom:** `ui-panel` failed on CI (run 38293812665, PR #358, a busy PC) at `@assert
vr.sim.shots.hudturned.left.quadPixels ge {hudq-1000}`: `2690 ge 5709` - the HUD quad covered 6709 eye pixels
with the head ahead and 2690 a moment later with it turned, although the quad is head-locked.
**Cause:** not the turn: the save `mvp-new-game` starts with a timed notification on the HUD, the tutorial hint
"Right-click to toggle from 'CURSOR MODE' to 'MOUSELOOK MODE'" (`gui/Notification.cpp`: 2 s + 60 ms per character
of game time, about 6 s). On a quiet PC both shots fall inside it; on a busy one the frames and the runner's
round trips between the shots are slower and the pair straddles its expiry. Measured with one shot a second:
the hint on screen (`notifications = 1`) for 4 more seconds after frame 270, 8791 px with it, 2690 px without
it - the failing value exactly - and 2690 with the head turned 40°.
**Do:** before comparing HUD pixels over time, `@until notifications eq 0` (status.json, #370); a sequence that
needs a hint on screen can put one up with `herosay <text>`.

## 2026-10-13 — The game vanishes with exit code -1: no crash report, no "Clean shutdown", nothing in arx.log (#146)

**Symptom:** a VR self-test with SteamVR running died 3-4 s after `xrCreateSession` (2 of 15 runs, 2026-10-09) with
exit code -1, no crash report, no `Clean shutdown` and nothing in `arx.log` about why. SteamVR's `xrclient_arx.txt`
logged `VR_Shutdown called` before the disconnect, and `vrserver.txt` no `Sending Quit event` to the game.
**Cause:** `exit()` or `ExitProcess(-1)` inside the process - the client's DLL detach ran, so it was not a
`TerminateProcess` from outside - by a module that is not ours: on Windows `main` returns 0 or 1, the crash handler
ends with 1, and no OpenXR failure ends the process (every failed call and every session end - stopping, exiting,
loss pending, instance loss - goes on in flat mode; read in `VRSystem.cpp`). Which module was not provable: the
failing runs' logs were overwritten (before #129), and the simulated headset cannot reproduce SteamVR. The suspect
is SteamVR's client: run 1 was the first launch right after SteamVR started (SteamVR Home was being asked to quit),
and at the time the session sat in `ready` without `xrBeginSession` (#20 runs the frame loop since), which a
runtime may treat as a hung app. An `ExitProcess` also skips the game's `atexit` handlers and loses up to a second
of buffered info lines - which is why nothing was in the log.
**Do:** read the `[E] ExitTrace:0` lines at the end of `arx.log` or `stderr.txt` ([The exit hook](TESTING.md#the-exit-hook-146)):
the way out, the exit code, the last quit request or VR session end before it, and the stack - the module after
`KERNEL32.DLL` / `ucrtbase.dll` called it. None of those lines and no crash report = killed from outside. The hunt
with SteamVR is the owner's: `smoke-test.ps1 -Vr -Repeat 10 -Seconds 3 -EvidenceDir <dir>` keeps every failed run's
logs, SteamVR's included.

## 2026-10-13 — An executable's TLS callback never sees the "process is terminating" flag (#146)

**Symptom:** the exit hook's TLS callback was in `arx.exe` (`dumpbin /tls` listed it) but wrote nothing when
`ExitProcess` ended the game.
**Cause:** it returned early on `reserved == nullptr`, copying `DllMain`'s rule (`lpvReserved != NULL` = the process
terminates). The loader calls TLS callbacks with a null reserved on process detach, always.
**Do:** in a TLS callback of the executable treat every `DLL_PROCESS_DETACH` as the end of the process - an `.exe`
is never unloaded. Check such a hook with a deliberate exit (`exit-trace-check.ps1`), never by reading the code.

## 2026-10-13 — `setpos` y is the feet, not the eye: the player stood 170 units above the floor (#210)

**Symptom:** a save placed in reach of `lever_0012` loaded in `--vr-sim` with the hand reticle's ray ending on the
lever's box, yet `vr.reticle.right.under` stayed `none`; the hand rays started at y 2810 over a floor at 3105
(the eye 340 above it), and `entities` listed the player entity at y 2935 — its position is its feet.
**Cause:** the channel's `setpos` (and the new `approach` at first) call `ARX_INTERACTIVE_Teleport(player, p)`,
which sets `player.pos = p + baseOffset()`: `p` is the **feet**, the eye ends 170 units higher. Everything
written before took setpos's y for the eye (the floor minus 170), so eye coordinates put the feet in the air: the eye
340 units above the floor, out of the pick's reach of a floor object (350 units from the eye), on wall tops
(what `hand-reticle.xrs` works around for `mvp-items`). The player does not fall back down by itself.
**Do:** `setpos <x> <floor y> <z>` for a player standing on that floor; check `player.y` (the eye) and
`camera.eyeAboveFeet` in the status. For a spot at an object use `approach <entity>` (TESTING.md), which finds
the floor itself. `mvp-items` and `mvp-goblin-fight` still stand 170 up until #417 regenerates them.

## 2026-10-13 — A load with a weapon in the IK hand crashed at `VRWeapon.cpp:296` (#210)

**Symptom:** `make-test-saves.ps1 -VrSim idle` crashed (ACCESS_VIOLATION, `VRWeapon.cpp:296`) at its first
channel `loadslot`, with the sword in the right hand; a launch with `--loadslot 0` never crashed.
**Cause:** `vr::heldWeaponStatus()` (the status writer, every frame) and `placeHeldWeapon()` used the weapon
pointer kept from the last `updateHeldWeapon()`; a load frees the entities before the next update, so the
pointer dangled. The radial menu's quick load in a real game takes the same path.
**Do:** never keep an `Entity *` across frames without resolving it again - `heldEntity()` checks it against
the player's weapon slot. Regression check: the end of `reach-lever.xrs` (`loadslot 0` with the sword held,
then `vr.weapon.place eq hand`), and every `make-test-saves.ps1 -VrSim` run reloads all its saves.

## 2026-10-13 — The game crashes on quit in `FT_Done_Face` after a feature drew text (#376)

**Symptom:** a run that worked ends with exit code 1, no `Clean shutdown`, and a crash report whose stack is
`freetype.dll!FT_Done_Face()` under `xtree` / `xmemory` from `ArxGame.cpp` (the shutdown). Nothing in the log says why.
**Cause:** a `Font` taken with `FontCache::getFont` and never given back with `FontCache::releaseFont`. At the exit
`FontCache::shutdown` deletes the cache: its destructor only *asserts* that every font was released (nothing in a
Release build), frees the FreeType library, and then the leaked font's face is destroyed against the freed library.
The first radial menu kept two fonts of its own for the whole run.
**Do:** draw with the game's fonts (`hFontControls`, `hFontInGame`, … in `gui/Text.h`: the right language's glyphs,
replaced when the interface is laid out again), or release an own font before `ARX_Text_Close`.

## 2026-10-13 — Melee in the playground: the inventory reopens itself, a second equip leaves a weapon on the cursor (#323)

**Symptom:** in a playground sequence (`@playground`) `vr.melee.refused` stayed `inventory`. `ui inventory close` closed
the bag, and the next frame logged `ui.inventory state=open` again. After a weapon was equipped, equipping a second
one turned melee off with `reason="an item is dragged"`.
**Cause:** a level started with `--loadlevel` never turns on the engine's free look. Loading a save does
(`ChangeLevel.cpp`). In cursor mode the engine reopens the inventory every frame (`Interface.cpp`), #405 / #394.
A script `<item>.equip player` puts the weapon it replaces on the cursor (`item.take from=world`), not back into the bag.
**Do:** `freelook on` (test channel) before anything in a playground sequence, until #394 keeps VR gameplay in free
look. Equip one weapon per run. The game's numbers for every weapon type are in `vr.melee.recovery.games`, so there is
no need to equip each type.

## 2026-10-13 — A simulated swing splits in two or reads above its ramp's speed (#323)

**Symptom:** a `vr-sim @+1-8 hand right pos ...` ramp (0.85 m, 9.6 m/s at 90 Hz) gave three swings, the last two at
3 m/s, or peaks of 12-13 m/s instead of 9.6.
**Cause:** the simulated hand moves one step per frame, not per second. A frame that stalls (a hit's effects, a busy
PC) slows the hand for real, and the detector now measures over the real time step (#352). The game's frame clock
(`frameStart`) also jitters against the simulator's display slots, which raises the peaks.
**Do:** in sequences, count swings with `ge` and hits exactly. Prove swing counts and peaks over frame times in the
host test (`MeleeSwingTest::testSameSwingsAt45And90Hz`: 45, 90 and an uneven 90 Hz).

## 2026-10-12 — An instant `vr-sim hand ... pos` set-up move counts as a melee swing on a busy runner (#349)

**Symptom:** `melee-swing.xrs` failed on CI at its first `@assert vr.melee.swings eq 0` for runs in a row with only
docs merged; `melee.swing` reported a swing with `peak=40.88` (above the 20 m/s jump speed), `status.json` had
`jumps=0`, and the run had a 99.5 ms frame gap just before the move. The green runs before had `jumps=1`.
**Cause:** an instant move of a simulated hand is a tracking jump only when its frame is short: the detector checks a
step's raw speed against 20 m/s, so 0.43 m over an 11 ms frame (39 m/s) is dropped, but over a 22 ms frame (45 fps,
the runner shares the PC with builds and other games) it is 19.5 m/s, and over a 99 ms gap 4.3 m/s — a real swing
(reproduced on purpose with `vr-sim rate 45`). The peak's 40.88 is that move divided by the next frame's step
(the median of three lags one sample, #352 — *resolved* for the peak: since #323 the median's move is divided by
the time it spans, so the same move reads 4.3 m/s).
**Do:** in a melee sequence move the hand in a ramp only (`vr-sim @+1-60 hand right pos ...`: slow, frame by frame);
reproduce a slow runner with `vr-sim rate 45` in a scratch copy of the sequence.

## 2026-10-10 — An `.xrs` `@log` step misses a game event that is in `arx.log` (#43)

**Symptom:** `@log GameEvent:.*magic\.stance state=on hands=true` failed with "no log line matched" while
`status.json` showed the stance on and `arx.log` had the event.
**Cause:** the game event log writes a `bool` field as `yes` / `no` (`GameEventFormat.h`), while `status.json`
writes `true` / `false`.
**Do:** match `key=yes|no` in `@log`, `eq true|false` in `@assert`.

## 2026-10-10 — A sequence passes alone and fails in CI with a value it never set (#178)

**Symptom:** in CI `vr-remap-saved` found `remapped` false and `vr-settings-flat` found `jump` = `right.b`, both passing
with `xrs-run.ps1` alone. **Cause:** CI's game checks run every shipped `.xrs`, in alphabetical order, in **one**
user and config directory: `vr-remap-saved` ran before the `vr-remap` it expected, and `vr-remap` left its saved layout
(`vr-controls-index.ini`) for `vr-settings-flat`. **Do:** a sequence sets up what it needs and leaves the user
directory as it found it (here: `vr-input default` at the start and the end; `vr-input reload` instead of a second
start). Anything a sequence saves (cfg, layouts, saves) is shared with the next one.

## 2026-10-10 — A remap through the channel records a long press nobody made (#178)

**Symptom:** `vr-remap.xrs` captured `jump` as `right.b:long` after `vr-input down right.b` … `vr-input up right.b`;
the same sequence had passed an hour earlier. **Cause:** every channel step is a round trip of about 0.6 s, so a
`down` and an `up` in separate steps hold the control longer than the 0.5 s of a long press — the capture (and the
control table) correctly records a long press; on a quiet machine it may just stay under. **Do:** a short press in a
sequence is `vr-input tap` (pressed and released within one update), the second control of a short chord too; a
`down` / `up` pair only for a press that is meant to be long (with a `@wait` well above 0.5 s).

## 2026-10-10 — A check broken on purpose still passes: `-Rebuild` did not rebuild (#39)

**Symptom:** with the charge spend commented out, `smoke-test.ps1 -Rebuild -Sequence melee-swing` passed all 92 steps,
including "the charge is spent".
**Cause:** `-Rebuild` (and the launch scripts' fresh-build check) compare `built-from.txt` with `HEAD` and the
*dirty* flag only: a build of `<sha> dirty` counts as fresh for any later uncommitted change on the same commit, so
the game ran the previous build. **Do:** after editing uncommitted code, run `.\scripts\build-windows.ps1` yourself
before a launch (it always builds what changed); a break-on-purpose run is only valid after an explicit build.
**Resolved (#306):** the build stamp carries a fingerprint of the uncommitted tree (`fp=<hash>`); an edit after
the build makes the launch scripts' check stale and `-Rebuild` rebuilds.

## 2026-10-10 — `player.pos + player.baseOffset()` is not the feet: an NPC placed there floats (#39)

**Symptom:** the channel's new `place` put the goblin 170 units *above* the player's eye; it hung in the air and
`setspeed 1` did not drop it.
**Cause:** `player.pos` is the eye and y grows downwards; `baseOffset()` is `(0, -170, 0)`, so adding it goes up.
The feet are `player.basePosition()` (`pos.y - baseHeight()`). **Do:** use `basePosition()` for the floor under the
player; check a placed entity's `y` against the player's feet in `entities.txt` (the floor is the eye + 170).

## 2026-10-10 — The sneak covered more ground than the run (#33)

**Symptom:** on `mvp-new-game` a slight stick push (the stealth walk) moved the player 355 units in 1.5 s and the
full push right after it only 201; with `@frames 40` instead the sneak covered 136-151 and the run 163-197 — the
ratio changed from run to run.
**Cause:** a distance measures the room and the clock, not the walk: the second walk ran into the cell's far end,
and `@frames n` is n frames, whose length swings with the frame rate of the moment (36-90 fps in these runs).
**Do:** compare walk speeds with `player.animSpeed` (the current animation's own units per second, what the engine
moves the player by: run 192, stealth 135.6); use positions only for "it moved", from a known start.

## 2026-10-10 — A new `status.json` key is `<absent>` though the code that writes it was just changed (#116)

**Symptom:** `smoke-test.ps1 -Rebuild` ran, said nothing about building, and the sequence failed on a key
added a minute ago: `vr.wrist.along.x (<absent>)`.
**Cause:** the fresh-build check (D-014) compares `built-from.txt` with the commit only. A build of the
clean `HEAD` followed by edits still counts as fresh, so `-Rebuild` does not rebuild and the old `arx.exe` runs.
**Do:** after editing, run `.\scripts\build-windows.ps1` yourself before the launch (or commit first); it costs
a launch from the budget otherwise.
**Resolved (#306, #294):** the build stamp carries a fingerprint of the uncommitted tree (`fp=<hash>`); the
fresh-build check sees edits made after a build (clean or dirty) and `-Rebuild` rebuilds.

## 2026-10-10 — `@diff` of two still shots of the body is as large as a small HUD change (#116)

**Symptom:** on `mvp-new-game`, looking down at the arms (`--vr-sim idle`), two shots of the same pose a few
frames apart differ by **2.2** (left eye) — the change of the wrist bars from full to half was 2.1–2.8.
**Cause:** the whole picture moves: the idle animation of the body and the right arm, the torch light.
**Do:** a mean over the whole eye image cannot prove a change of a few hundred pixels; assert on the numbers
in `status.json` and judge the captures. Measure a still pair in the same pose before trusting a threshold.

## 2026-10-10 — Which grip axis is the thumb: the hand's convention moved under the wrist bars (#36, #116)

**Symptom:** wrist bars built on "thumb = grip +Y" lay along the fingers instead of across the wrist; then,
after #36 was corrected to the OpenXR spec, bars built on "thumb = grip +Z" would fill the wrong way.
**Cause:** the full body (#36, `HandFrame` in `src/vr/ArmIK.h`) defines the hand's axes on the grip: since
`234ea0cc2` the knuckles (wrist → fingers) lie on grip **−Y**, the thumb side on grip **−Z**, the palm normal
on **+X** (the OpenXR grip pose). In engine axes (y down, z forward) a grip vector (x, y, z) is (x, −y, −z)
before `PlayerArmStatus::grip` turns it into the world.
**Do:** anything attached to the hand takes its axes from `HandFrame`'s comment, not from memory; a `--vr-sim`
pose for "knuckles to the right, palm to the face, thumb up" is `hand left rot -90 90 0`, "palm down,
knuckles to the right" `hand left rot 180 0 -90`. Assert the axes in `status.json` (`vr.wrist.across`).

## 2026-10-10 — In VR an item on the floor is not picked where the cursor is, or a far one is (#37)

**Symptom:** with a mouse position projected from a 3D point in a VR run, `FlyingOverObject` finds nothing over
an item lying in the world, or something beside it. **Cause:** the engine's pick is a 2D test against each
entity's `bbox2D` and projected faces (`vertexClipPositions`). Animated entities are projected with the head
camera in the update, but a **static item** (no animation: what lies on floors and tables) is projected in
`DrawEERIEInter` while it is drawn — once per **eye**, into the eye image's pixels — so after a stereo frame
its box is the right eye's, in another pixel space than the interface the mouse lives in. *Resolved for the
screen data by #72* (picking reads the head camera's at the interface's size again). **Do:** in VR the world is
still picked by the hands' rays in 3D (`vr/VRReticle.cpp`) — a hand's ray is not a ray through the eye, so a
projected pixel lands on whatever the head sees there, not on what the hand points at; the engine's
`GetFirstInterAtPos` asks the reticle module what a world reticle's mouse position stands for
(`vr::reticlePickAt`), and an interface position never picks the world while the reticles are on. The test
channel's `pick` checks the engine's own screen pick under `vr::EnginePickOnly`.

## 2026-10-10 — The inventory opens by itself, or the player spins, when a VR hand becomes the mouse (#37)

**Symptom:** forcing the engine out of mouse look so the hand could be the mouse opened the inventory at
once; later, with the inventory open, the player kept turning (`player.yaw` 113 → 56 → 159 → 261 in two
seconds) with nobody touching anything. **Cause:** (1) in Arx the cursor mode *is* the inventory: leaving
mouse look shows it unless the inventory key inverted that (`bInverseInventory`, `ArxGame::manageKeyMouse`).
(2) In the cursor mode a mouse within 50 px of an edge of the interface turns the player (edge scrolling,
`Interface.cpp`); a world reticle's mouse position is right of the interface (`vr/HandReticle.h`), and a
panel pointer near the panel's edge is near the interface's edge. **Do:** keep the engine's mouse-look state
as it is (the hand's position is applied in the relative mode too, `Input::setVirtualMousePosition`); the
edge turning is off while a VR session renders.

## 2026-10-10 — On `mvp-items` an item can be picked from the table but not put back on it (#37)

**Symptom:** a drag started from the table (`item.take`), but the item never landed where the hand pointed:
it stayed in front of the player or was dropped on the floor; the floor in front of the player was not hit
by any ray. **Cause:** the save stands the player on the **wall top above the table's room**: the eye is at
`y` 2765, the room's floor at 3100 (y down), the table top at 3018. The engine picks within 350 units of the
player but puts down only within **300** (`updateDraggedEntity`), and nothing below the player is within 300;
the key on the floor (399 units away) is not even pickable. **Do:** put the player on the room's floor by
the table first — `setpos 10600 3100 11420 72` (as `hand-reticle.xrs` does); the note and the form are then
~1.5 m away, both for picking and for putting down. Mind the goblin in that room.

## 2026-10-10 — A head pressed into a wall is "not in geometry": `CheckBackgroundInSphere` misses the middle of a wall (#26)

**Symptom:** the simulated head is moved into the start cell's side wall — the body is blocked at the wall, the eye
shot shows the wall face filling half the view at a few centimetres — and the near-plane sphere test
(`CheckBackgroundInSphere(Sphere(head, 12.5))`) says nothing is there; once the head is past the wall the ray test
finds it at once.
**Cause:** the engine's `IsPolyInSphere` does not measure the distance to a polygon: it only tests whether the
polygon's corners, edge midpoints and a few points towards its centre lie in the sphere (it was made for the
50-unit cylinders of NPCs). A small sphere in front of the middle of a large wall polygon contains none of them.
**Do:** for anything smaller than a body, measure the distance to the polygon's triangles —
`vr::distanceToTriangle` (HeadLean.h), used by `backgroundCloserThan` in `ArxGame.cpp`; `raycastScene` is exact
for a segment.

## 2026-10-10 — A shipped `.xrs` sequence is red in CI only because of the sequence that ran before it (#296, from #178)

**Symptom:** the CI game check fails a sequence that passes alone: `vr-remap-saved` expected the file `vr-remap` saved
(it ran *after* it alphabetically), `vr-settings-flat` found the `vr-controls-index.ini` that `vr-remap` left. Rename or add
a sequence and the order — and the verdict — changes.
**Cause:** `ci-smoke.ps1` ran every sequence in the runner account's single user dir, so config, saves and ini files leaked
from one sequence into the next.
**Do:** every sequence now runs in its own fresh user dir (`New-ArxSequenceUserDir` in `xrs-lib.ps1`, passed to
`smoke-test.ps1 -UserDir`; its `@save` is copied in as before; the dir is removed after the run and its logs are in the
phase's evidence). A sequence that needs a file from another one is wrong: set the state up in its own `@save` / commands.
The first fresh-dir CI run found it at once: `walk-forward`, `walk-look` and `sneak-jump-crouch` had `@args --loadslot 0` but no
`@save`, and had only ever passed on the save an earlier sequence left in the shared dir ("no such save, 0 save(s)"). They
declare `@save mvp-new-game` now, and `test-xrs.ps1` fails a shipped sequence with `--loadslot` and no `@save`.
Checked by `test-xrs.ps1` ("fresh user dir per sequence"). Locally, `smoke-test.ps1 -Sequence` already used the isolated dir of
the working copy: run two sequences by hand with `-UserDir` pointing at different folders for the same effect.



## 2026-10-10 — The arm clips are "suppressed", yet the shoulder moves 10 units when the combat mode starts (#75)

**Symptom:** `vr.weapon.armAnimSuppressed` is `true` and every `vr.body.armAnim` event says `suppressed`, but
`vr.body.right.shoulder` jumps by ~10 units between before `combat on` and after the strike.
**Cause:** the combat mode also switches the **body's** layer 0 to the fight stance (`ANIM_FIGHT_WAIT`,
`ARX_PLAYER_Manage_Visual`), which turns the torso; only layer 1 (the arm clips) is suppressed (D-009: the body
keeps the engine's animations). **Do:** measure what the arm layer does from a settled stance — take the
shoulder after `combat on` + 60 frames (300 ms of blend), then strike, as `weapon-hold.xrs` does. Also: never
ask `entities.player()` while writing `status.json` before a level exists (the first `weapon-hold` run crashed
at start-up on it, #286) — check this module's own state first.

## 2026-10-10 — .xrs: a count of what a held input did passes locally and fails on the CI runner (#285)

**Symptom:** `snap-turn.xrs` held the stick until the 5th snap, let go and allowed one more snap; locally it
passed, on the CI runner it counted 10 to 12 (`ASSERT FAILED: vr.input.turn.snaps (12) le 9`).
**Cause:** a channel command reaches the game a poll after the runner sends it, and the runner's round trip on a
loaded machine is longer than a repeat (0.4 s), so the release came 2–4 repeats late. An upper bound on a count
after a release is a bound on the runner's speed.
**Do:** let the game measure the timing by its own clock and assert on that (`vr.input.turn.repeatInterval`);
in a sequence, bound only what the game decides, never how many times something happened before a later command.

## 2026-10-10 — The arms twist outward and wrap into spirals from the shoulder and the elbow when the wrist turns (#36, owner's headset run of PR #254)

**Symptom:** the body, the solid torso and the hands following the controllers are right in the headset
("руки работают"), but as soon as a controller rolls, the upper arm and the forearm of the model twist into
flat spirals between the shoulder and the hand (the owner's headset screenshot, kept out of the repository:
both arms reach in from the sides as twisted ribbons while the hands themselves look fine). Every number in
`status.json` is still perfect — `error` 0, the wrist on its target — because the numbers describe joints,
not the mesh between them.
**Cause:** the engine's skeleton has no twist model: a group's vertices turn rigidly with the bone's quaternion,
and the IK of #36 only *swings* each bone onto its joints (`rotationBetween` on top of the animated quaternion)
while the wrist takes the controller's full orientation. The roll about the arm's axis is therefore whatever
the animation plus the swing leave — nothing distributes the hand's roll along the forearm or keeps the
upper arm's roll tied to the elbow — and the low-poly arm, a flat-ish slab, shows every stray roll as a spiral.
A joint-position check cannot see it.
**Do:** accepted as the interim body (D-032 / #36); the arm rework is a research task (how Valheim VR builds
its arms, what Arx's model and textures allow: twist bones, roll limits, a separate VR arm rig). Until then,
judge arms in the headset or on a capture with the controllers *rolled*, not from `vr.body` alone; the
`arms-follow` sequence rolls nothing.
**Resolved by #304 (D-050):** the three arm rotations are built from the bend plane and the hand's roll is
shared along the arm (vr/ArmRoll.h); `vr.body.<hand>.wristTwist` / `elbowTwist` / `upperRoll` now say what each
band of faces carries, and the `arms-roll` sequence rolls the controller 0–180° and asserts on them. The lesson
stays: a joint-position check sees no twist.

## 2026-10-09 — The body is nowhere in a `--vr-sim` shot that looks down, although `vr.body` says both arms are posed with `error` 0 (#36)

**Symptom:** the first two runs of `arms-follow` on the default `--loadlevel 10` had every status number right
(chains found, wrists on their targets) and both eyes' pictures showed a bare floor from a standing height —
no torso, no arms, and the picture even drifted between two "still" shots.
**Cause:** level 10 is the scripted intro: its conversation camera (`speechControlledCinematic`, see the #24
entry below) is a camera somewhere in the cell, not the model's eyes, and it moves with the speech; the head
pose is applied on top of it. The model stands elsewhere, correctly posed.
**Do:** anything that must *see* the body runs on a first-person save: `smoke-test.ps1 -VrSim idle -Sequence
arms-follow -Save mvp-new-game`. Status numbers prove the pose; only a first-person save proves the picture.

## 2026-10-09 — The arm chain found from the hand is one joint short, and the hand ignores the controller's pitch (#36)

**Symptoms:** (1) walking up from the group that holds `primary_attach` gave `right_elbow > right_wrist >
right_hand` with bone lengths `30.5 + 0.0`, so the "arm" reached only 30 units and the shoulder slid 17.6 units
towards a controller held comfortably in front. (2) With the wrist bone set to the controller's orientation the
fingertips stayed at the same height whether the grip was upright or pitched 90° forward.
**Cause:** `human_base` keeps the hand as a **zero-length child group** of the wrist (`right_hand < right_wrist`,
same origin), and the engine's animations bend that hand group on the wrist (the wait animation curls it). The
wrist joint is therefore one group up, and the hand mesh belongs to the child group, whose rotation the
concatenation rebuilds from the animation after the wrist was set.
**Do:** the wrist joint is the first ancestor with a non-zero `transinit_global` (climb the zero-length bones);
the zero-length groups between it and the hand's group are made rigid with the wrist (`ArmChain::rigid`,
`vr/VRBody.cpp`). Check a chain by its lengths in the `[vr] body:` bind line (`27.6 + 30.5` on `human_base`) and
a hand by `vr.body.<hand>.tip` against `wrist` / `controller`, not by the picture alone.
## 2026-10-10 — `test-xrs.ps1` fails once in a while: "timed out waiting for the game to acknowledge" (#275)

**Symptom:** one check of 73 fails on the CI runner (a `@shot`/`@diff` step), the rerun of the same commit passes.
**Cause (most likely, not reproduced in 20 local runs):** the fake game read `command.txt` with `Get-Content` and
wrote `ack.txt` with `Set-Content`, whose errors are non-terminating — a sharing violation with the test's own
50 ms polling (or a scanner on a loaded runner) silently lost the batch or its ack, so nothing was acknowledged.
**Do:** the fake game removes `command.txt` only after reading it and retries both the read and the ack write.
When a channel check times out with the game alive, suspect a dropped file operation before a slow game.

## 2026-10-09 — The body turned 45° by the numbers, the picture did not turn (#32)

**Symptom:** on the simulated headset with `--loadlevel 10` (+ `setplayercontrols on`) a snap turn changed
`player.yaw` and `camera.yaw` in `status.json` by exactly 45°, the `vr.turn.snap` game event was logged, and the
eye shots before and after still looked down the same corridor (picture change ~4, barely over the floor);
a head turn of the simulator did turn the picture.
**Cause:** the trap of #24 again — level 10 is the intro conversation, whose camera is placed by
`speechControlledCinematic()`, not from the player's yaw, so nothing the **body** does shows in the view.
`status.json` → `camera.yaw` is the engine's `g_playerCamera`, not what the eyes render with.
**Do:** check turning, walking, anything the body does, on a first-person save: `@save mvp-new-game` +
`@args --loadslot 0` in the `.xrs` (snap turn there: picture change 16.9). Judge by the shots, not by
`camera.yaw`.

## 2026-10-10 — A channel-driven check times out with `ack.txt` holding only `quit`, though the game ran (#258)

**Symptom:** `body-check.ps1` fails with "timed out after 180 s waiting for gameplay"; no command of the check
reached the game. **Cause:** `smoke-test.ps1` first waits up to 300 s for the game lock (D-014); the check's
own timeout counted that wait as game start-up. **Do:** a check that drives the game from outside calls
`Clear-ArxLaunchMarker` before it starts the smoke test job and `Wait-ArxGameLaunched $job $dir` before any
timeout of its own — `smoke-test.ps1` writes `launched.txt` into the test folder right after the game started.
`false` means the smoke test ended without a launch (lock skip = exit 2). Covered by `test-wait-launch.ps1`.

## 2026-10-09 — In the headset the camera floats far above the body (#24, #27)

**Symptom:** looking down in the headset, the character's body is far below the eyes, although the camera
math is right and every flat or simulated check passes.
**Cause:** the head's reference (the head there = the character's eye) was taken from the first tracked
sample after the level load — and the owner starts a test at the desk: the session runs `synchronized`
with the headset lying there (the runtime still locates it), and it is put on 50–80 s later
(`vr.session state=visible` in arx.log). The worn head was ~0.8 m above the desk reference, so the camera
was 80 units above the eye.
**Do:** read `vr.head.reference` in arx.log: `kind`, `wornS` and `headAboveFloorM` say when and from what
the reference came; `status.json` → `vr.head.worn` / `referenceKind`. Since #27 the settled reference
only comes from a worn headset (D-031). To reproduce without a headset: `session idle` in a `--vr-sim`
script (`scripts/dev/vrsim/eye-height.vrsim`).

## 2026-10-10 — The 2D interface is empty in `--vr-sim` shots, or the inventory "never closes", on level 10 (#49)

**Symptom:** on `--loadlevel 10` every shot's flat frame is `transparent` and `quadPixels` is **0** — no HUD,
no inventory, even after `ui inventory open`. After `setplayercontrols on` / `cinemascope off` the
inventory opens by itself, and a `ui inventory close` is followed by `ui.inventory state=closed` and,
one frame later, `state=open` again: `player.inventoryOpen` never becomes false.
**Cause:** (1) while the level 10 start holds the player the HUD is not drawn at all (TESTING.md, the
`ui` command). (2) Released, the level's start shows the bag and opens it again after every close (seen
flat and in VR, #49; the cause in the level's script was not chased). (3) `quadPixels` of a shot counts
the eye pixels the **drawn** interface covers (alpha > 0), not the quad's area: an empty blended quad is 0.
**Do:** for anything about the HUD or the menus use the test save `mvp-new-game` (#70: controls free, the
bag closed) — `smoke-test.ps1 -VrSim idle -Sequence <name> -Save mvp-new-game`, as `ui-panel.xrs` does.

---

## 2026-10-09 — A check of "what the button did" counts one event too many, or finds it before the press (#74)

**Symptom:** a flat check of the VR buttons on level 10 sees `ui.inventory=3` after two presses of the
inventory button, and a `game.state to=MENU` that is not after the `[mark] game menu` sent right after the
menu button — "the menu never opened", although it did.
**Cause:** (1) the level 10 start shows the inventory bag: once `setplayercontrols on` / `cinemascope off`
give the controls back, a `ui.inventory state=open` is logged with no button pressed — the first press
*closes* it. (2) A `vr-input` tap is resolved at the start of the next frame and the game reacts in that
frame, so its game event can be logged before the ack of the command and before the next `mark` line.
**Do:** anchor on the button's own `[vr] input: <event> (…)` log line (it comes first in the frame, before the
game events) and check the order of the state changes after it, not their total — as
`scripts/dev/game-input-check.ps1` does.

## 2026-10-09 — A shot in the first second after a level load is black (#112)

**Symptom:** a `--vr-sim` shot (or a channel `shot`) taken right after `status.json` says `GAMEPLAY` judges
`black` on `--loadlevel 10` — the same verdict as the real black-world bug of #215 — and a shot a second later
on the same level says `world`. An eye-check run with early shots "passes" its yaw leg because the first shot
is black and the later one is not.
**Cause:** the level fades in from black for about a second after it is playable; nothing is wrong with the
render. The game state is `GAMEPLAY` from the first frame of the fade.
**Do:** shoot 3 s in: the `scripts\dev\vrsim\*.vrsim` scripts start at `@270` (90 Hz). A real black world
stays black: compare two shots seconds apart before calling it a bug.

## 2026-10-09 — A unity build sees one file's macros and helper names in every file after it (#112)

**Symptom:** a build fails with `'::xrWaitFrame': undeclared` in a file that includes `<openxr/openxr.h>`
itself, or `C2374 'anonymous-namespace'::Epsilon': redefinition` / `fov ... already has a body` in a test
file that compiled fine alone.
**Cause:** `UNITY_BUILD` (on by default, off only with `DEVELOPER`) compiles all sources of a target as one
file (`ub_*.cpp`, sorted by path). An include guard opened by an earlier file keeps its macros: once
`vr/OpenXRUtil.h` defined `XR_NO_PROTOTYPES`, a later `#include <openxr/openxr.h>` adds nothing. And every
file's anonymous namespace is the same one, so two test files with a helper `Epsilon` or `fov` collide —
`arxtest` is a unity build too.
**Do:** a file that needs the OpenXR loader's functions declares them itself from the `PFN_` types
(`src/vr/XRDispatch.cpp`); give file-local helpers file-specific names (`ImageEpsilon`, `imageFov`). `Near` and
`Far` are macros from `windows.h` on MSVC too (hit in #19's test): not identifiers in any file that sees it.

## 2026-10-09 — A scene drawn into a framebuffer object lands at the wrong place, is clipped, or loses its alpha cutouts (#19)

**Symptom:** rendering through the engine's renderer into an OpenGL framebuffer object of another
size than the window (a VR eye image, 2468×2740) puts the picture in a corner or off the image;
scissored draws (the cinematic border) cut the wrong band; `getSnapshot()` reads the window's size;
fences and foliage with alpha cutouts come out as solid black quads on the eye image although they are
fine in the window.
**Cause:** `OpenGLRenderer::SetViewport` / `SetScissor` turn the engine's top-left rectangles into
OpenGL's bottom-left ones with the **window's** height, and `getSnapshot` reads the window's size; and
with MSAA on the window the renderer draws alpha cutouts with alpha-to-coverage or sample shading
(`alpha_cutout_antialiasing`), which do nothing on a single-sample framebuffer object - the alpha test
that would discard the transparent texels is off.
**Do:** tell the renderer the bound target's size (`Renderer::setRenderTargetSize`, 0 = the window): it
re-applies the viewport and the scissor against that height, snapshots read that size, and
`SetAntialiasing(true)` is refused on a single-sample offscreen target so the alpha cutout path falls
back to the alpha test (a multisampled one names its samples too, `setRenderTargetSize(size, samples)`, #226).
`vr::beginEyeRender()` / `endEyeRender()` and the flat frame target do this; a new offscreen target must too.
With eye MSAA (`vr_msaa`, D-035) the eye is drawn into a multisampled buffer, not the swapchain image:
the image holds the picture only after `Swapchain::resolve()` in `endEyeRender` - read eye pixels after it.

## 2026-10-10 — "use" fires from just holding the Index controller (a closed hand flickers, #30)

**Symptom:** the log shows `[vr] input: use_right (right.grip)` several times (×6 in the owner's headset run)
although the player only held the controller and made one grip + trigger chord; with a game reaction (#74)
that is a right click on whatever the reticle is on.
**Cause:** the Index's `input/squeeze/value` measures how far the hand is closed around the handle. A relaxed
closed hand sits around 0.6 – 0.7, right on the 0.70 / 0.55 threshold borrowed from Dishonored VR (F-41), so
it crossed it back and forth; each dip was a release of the grip — which leads the stash chord, so its own
binding fired as a tap on release. A "firm" threshold is the wrong model too: the Index is strapped on, a
light close is the grip and an open hand is none (D-019, owner 2026-10-10) — `squeeze/force` (a hard
squeeze) was tried in round 2 and dropped before a headset run for that reason.
**Do:** the Index grip stays on `squeeze/value` with a low press and a wide gap (`IndexGripThreshold`
0.50 / 0.30); a chord leader's release is debounced (`ControlResolver::releaseDebounceMs`), so a flicker is
one hold; "use" fires when the hand opens without a trigger (D-019). Every grip crossing is logged with its
value (`[vr] input: right grip down at 0.55 (threshold 0.5 / 0.3)`), and `status.json` → `vr.input.grip`
shows it live — tune the threshold from those numbers, not by feel. *Changed 2026-10-17 (#496):* the press
is 0.70 / the release 0.40 now - a firm resting hold read 0.52 - 0.59 and turned every trigger press into the chord
(the entry above).

## 2026-10-09 — A non-Steam shortcut written into `shortcuts.vdf` disappears

**Symptom:** a script (or a hand edit) adds a shortcut to `<Steam>\userdata\<account>\config\shortcuts.vdf`,
the file has it, yet the Steam library does not show it — and after the next Steam exit the entry is gone.
**Cause:** Steam reads `shortcuts.vdf` once at start, keeps the shortcuts in memory and writes the
whole file back when they change or when it exits; an edit made while Steam runs is overwritten.
**Do:** exit Steam first (Steam > Exit, not just closing the window), write, then start Steam.
`scripts/add-steam-shortcut.ps1` refuses while `steam.exe` runs (#80).

## 2026-10-09 — A hex literal like `0x80000000` is negative in PowerShell

**Symptom:** `[uint32]0x80000000` throws "Value was either too large or too small for a UInt32";
a CRC32 written in PowerShell gives wrong values or the same error mid-loop.
**Cause:** PowerShell parses a hex literal as `int` when it fits in 32 bits as a bit pattern, so
`0x80000000` is `-2147483648` and `0xEDB88320` is negative; `-bxor`/`-shr` then work on signed values.
**Do:** suffix 32-bit constants with `L` (`0x80000000L`, `0xEDB88320L`), compute in `[long]`, and cast
to `[uint32]` only at the end (`scripts/steam-shortcuts-lib.ps1`, #80).

## 2026-10-09 — The headset test cannot start: "could not read PR", or the game starts flat with SteamVR off (#171)

**Symptom:** `test-pr.ps1 <pr> -StartSteamVR` stops at once with `API rate limit already exceeded` /
`could not read PR #<n>` although the PR's worktree and fresh build are on disk; a hand-made fallback launch
(`run-windows.ps1` in that worktree) starts the game **flat** and no SteamVR process appears.
**Cause:** the station read the PR through the GitHub API, whose quota every worker session shares; and nothing
starts SteamVR on its own — the game's OpenXR call is not a reliable way to bring it up (and D-014 makes the
scripts add `--no-vr` while `vrserver` is off anyway).
**Do:** `test-pr.ps1` now falls back to `git ls-remote origin refs/pull/<n>/head` + the local worktree (`-Branch`
when even git cannot reach GitHub), and `-StartSteamVR` starts SteamVR explicitly (`steam://rungameid/250820`,
then `vrstartup.exe`) and waits for `vrserver` + `vrcompositor`. Never test the start logic by starting SteamVR as
a worker: `scripts/dev/test-start-steamvr.ps1` proves it with stubs. A `Get-Process`-style stub for a cmdlet in a
PowerShell self-test must be a function with `[CmdletBinding()]`, or `-ErrorAction` breaks it.

**Stopping SteamVR (same task):** `CloseMainWindow` on `vrmonitor` plus `Stop-Process` on the rest took the software
away while the Valve Index stayed on — the drivers never got their own shutdown. SteamVR's status window takes URL
commands (`vrmonitor.exe` has the `quit` handler of the `vrmonitor://` protocol), so `stop-steamvr.ps1` /
`test-pr.ps1 -StopSteamVR` send `vrmonitor://quit` — the request behind "Exit SteamVR" — and only wait for the
processes to leave (60 s); they never kill. Found by reading the strings of `vrmonitor.exe` and the registry entry
`HKCU\Software\Classes\vrmonitor`, not from documentation (there is none): the headset powering down is confirmed
by the owner's run only.

---

## 2026-10-09 — The head turns, the picture does not: the camera of a conversation owns the view (#24)

**Symptom:** on the simulated headset `eye-check.ps1` failed its yaw leg on the #24 branch although the head
drove the camera — the `yaw25` shot's flat frame was **identical** to `still-a` (picture change 1.34, floor 0.81),
while the flat `head-check.ps1` on `mvp-new-game` passed.
**Cause:** `--loadlevel 10` (the default of the smoke test and of `eye-check.ps1`) plays the intro conversation:
`speechControlledCinematic()` places `g_playerCamera` itself **after** `updateFirstPersonCamera()`, so a head pose
applied there was overwritten every frame. In the headset that glues the world to the face during every
conversation, death and external view.
**Fixed:** the head is applied in `updateActiveCamera()` to whatever camera the frame renders with, and taken off
again (`restoreEngineCamera()`) before the engine updates its cameras next frame, so the engine's interpolations
never read it back. Yaw leg after the fix: picture change 4.25.
**Do:** a head-tracking check needs both a first-person save (`mvp-new-game`) and level 10 (a scripted camera);
"PASS in one" proves only that path.

## 2026-10-09 — The world is black in the headset, only the HUD shows: no level was loaded (#24)

**Symptom:** the owner's headset run of #215 (`test-pr.ps1 215`, `--skiplogo --loadslot 0`, the shared user dir)
showed a black world with the inventory bar, the health and mana orbs and the icons on top, in the headset and in
the desktop window. It looked like the new head-tracked camera was inside a wall or had broken the projection.
**Cause:** the owner's `Saved Games\Arx Libertatis\save` was **empty**. `--loadslot N` switches the game flow to
"in game" at once and loads slot N only if it exists; without it nothing was loaded and nothing was said — the
game ran with **no level at all**: the HUD is drawn, the world is not. The log shows it plainly: `game.state` never
leaves `STARTUP` (`game.state to=EXITED was=STARTUP`), no `level.loading` / `level.loaded`, and the frame loop's rate
line says `game 0.1 mean` ms (a level costs several ms). The #21 run minutes before had the same log — no level either.
**Fixed:** `arx.exe` now logs `[E] --loadslot N: no such save, M save(s) in the save game dir - starting at the main
menu instead of an empty level` and opens the main menu; `run-windows.ps1` (so `test-pr.ps1` too) refuses to start a
`--loadslot` the user dir has no save for, before the headset is involved, and names the test save to use.
**Do:** for a headset test without saves of your own, load a test save: `test-pr.ps1 <pr> -Saves mvp-new-game` (or
`run-windows.ps1 -Isolated -Save mvp-new-game`). Before suspecting rendering, check `game.state` / `level.loaded` in
the log or `state` / `area` in `status.json`: "black world" with `area` 0 is not a rendering bug.

## 2026-10-09 — "Blocked [rule]: ..." on a command that looks harmless (the safety hook)

**Symptom:** a Bash / PowerShell / Edit call is refused with `Blocked [push-main|force-push|delete-outside|
owner-write|owner-discard]: ...`, or a command is slow (~0.5 s) before it runs.
**Cause:** the `PreToolUse` safety hook (#185, [WORKFLOW.md](WORKFLOW.md#the-safety-hook-185-d-021)) denies the
five rules named in the message; the reason says what to do instead. It reads the command line, so a
command that *names* the owner's copy or a path outside the worktree is judged by what it writes or deletes
there. `cd <owner's copy>` followed by a write is `owner-write` even with relative paths. A call that
contains `push`, `reset`, `clean`, `checkout`, `rm`/`del`/`rd`, `cd` or `arx-fatalis-vr` takes the slow path
through PowerShell; that is normal. Hook errors never block (fail open) and are logged.
**Do:** follow the "instead" in the message. Read `%LOCALAPPDATA%\ArxFatalisVR\safety-hook.log` for the exact
command and rule. For a real false positive tell the coordinator (do not rewrite the command to dodge the
rule): the fix is a rule change plus a row in `scripts/dev/hooks/test-safety-guard.ps1`. A new session picks
up a changed `.claude/settings.json` only on start; a hook that "does nothing" in a session started from
a copy without the file (the owner's copy before the merge) is not loaded at all.

## 2026-10-09 — PowerShell rules code that silently matches nothing: int keys, doubles, Write-Host (#187)

**Symptom:** a lookup by issue number or wave finds nothing (a reason prints `wave 1 still has #` with the list empty), a
dictionary hands out the wrong entry, or a test harness that captures a script's output sees an empty string and "0 failing".
**Cause:** (1) `[ordered]@{}` is an `OrderedDictionary`: `$d[1]` with an **int** key is read as *position* 1, not key 1 —
use a plain `@{}` and sort afterwards. (2) `Measure-Object -Minimum` answers a **double**; `$hashtable[1.0]` does not find
the int key `1` — cast with `[int]`. (3) `Write-Host` goes to the information stream: `& script 2>&1` does not capture
it, `*>&1` does.
**Do:** cast numbers to `[int]` before using them as keys; no int keys in `[ordered]` tables; capture a script's whole output
with `*>&1`; and break the rule once on purpose to see the test fail (test-plan-next.ps1 was checked that way).

## 2026-10-09 — The CI game is another Windows account: its log, lock and build are not in your profile (#127)

**Symptom:** a CI game check failed (or passed) but `%LOCALAPPDATA%\ArxFatalisVR\userdirs\…\arx.log` and `launches.log`
show nothing about it; or a smoke test of yours and the CI game ran together although "one game at a time" holds.
**Cause:** the runner service is `NT AUTHORITY\NETWORK SERVICE` (session 0). Its `%LOCALAPPDATA%` is
`C:\Windows\ServiceProfiles\NetworkService\AppData\Local`, so CI has its own build, user directory, channel, game lock and
ledger. (Until #205 the game lock was per account too and did not span accounts; it now lives in `%ProgramData%\ArxFatalisVR\locks`,
shared by both. A working copy that has not merged `main` since still has the old launch-lib: its launches do not see a CI game
(they look in their own account only), while CI sees its `arx.exe` through WMI and waits - merge `main` into such a copy.) It also
has no Steam registry entry of the owner, hence `ARXVR_DATA_DIR`.
The game itself runs fine there: no desktop, window and OpenGL context are created anyway (measured: `GAMEPLAY` after ~1 s).
**Do:** read the evidence from the workflow artifact `game-checks` (logs, `status.json`, crash reports, digest), not from
your profile. Do not "fix" a CI failure by starting an interactive runner before the artifact says the window was the problem.

## 2026-10-09 — Backslashes vanish from Windows paths written through sed or a shell string (#186)

**Symptom:** a doc or script line reads `.scriptsdevpr-checks.ps1` instead of `.\scripts\dev\pr-checks.ps1`.
**Cause:** `sed 's#...#.\scripts\dev\x#'` in Git Bash, or any unquoted/double-quoted shell string, treats `\s`, `\d`, `\p`
as escapes and drops the backslash; the edit still "succeeds".
**Do:** edit files with the Edit/Write tools, not `sed` with Windows paths; after any scripted edit grep the changed
lines for the path (`git diff -U0 | grep pr-checks`) before committing.

## 2026-10-09 — "GraphQL limit" errors from commands that look like plain REST (#186)

**Symptom:** `API rate limit already exceeded` / `unknown owner type` from `gh issue view`, `gh pr view`, `gh pr list`,
`gh pr checks --watch`, `gh issue list` or a dev script, while `gh api repos/...` calls work and
`gh api rate_limit` shows `core` untouched; `gh api rate_limit` may even show thousands of `graphql` points left.
**Cause:** every high-level `gh` command except `gh api` with a `repos/...` path is GraphQL (`gh project ...` the
heaviest), and the GraphQL quota (hourly points plus a secondary, short-term limit) is shared by every session of the
account. A board query that fails is easy to read as "nothing there" (`next-task.ps1` said the queue was empty).
**Do:** use `gh api repos/Arziburst/arx-fatalis-vr/...` (REST) or the helpers in `scripts/dev/github-lib.ps1`
(`Get-GhIssue`, `Get-GhPull`, `Get-GhCommitChecks`, ...) and `scripts/dev/pr-checks.ps1 <pr> -Watch`; the board comes from
the 5-minute cache (`board-cache.json`) and a spent limit queues the move (`board-queue.json`, `set-status.ps1 -Flush`).
`ARXVR_FORCE_GRAPHQL_ERROR=1` makes every GraphQL call fail like a spent limit, to try the degraded paths. Also:
PowerShell 7 turns ISO date strings of `ConvertFrom-Json` into `DateTime` — the cache stores epoch seconds instead.

## 2026-10-09 — A reference project's manual and settings screen describe a feature its code no longer has

**Symptom:** research on OpenMW VR finds two accounts of the same facts. The manual says controllers "vibrate in
response to successful hits, and to being hit", and the game's VR page has a *haptics enabled* setting; it also says
the bindings screen "does NOT do any sanity checking". The code: the hit haptics are commented out (`TODO: Port to
lua`), the setting is read by nothing, no shipped script calls the haptics function; and the rebinding code refuses
a conflicting binding with a message. A config comment says the hand-offset sliders run "0 = -1 meters, 1 = 1
meters"; the code computes `(v - 0.5)` m. The `player height` setting is read into a scale that nothing applies.
**Cause:** the manual, the settings window and the config comments were written before a port or refactor (combat
code moving to Lua, the bindings screen becoming a script page) and not updated; a screen that lists a setting
proves nothing about what the code does with it.
**Do:** for every catalog row that rests on "the project does X", search the code for the *use* of the setting or the
call, not its declaration, and say which of manual, screen and code the row rests on; when they disagree the row
follows the code and names the disagreement (RESEARCH-reference-mods.md, F-44, F-167, F-171, F-174).

## 2026-10-09 — On `--loadlevel 10` the inventory never closes, `dodamage player` does nothing, and the game goes to the menu after a minute

**Symptom:** a smoke test on the default `--loadlevel 10` opens the inventory through the channel
(`ui inventory open`), but `ui inventory close` never produces `ui.inventory state=closed`;
`dodamage player 1000` leaves the player alive; a `shot` shows a letterboxed picture; and a run
longer than about a minute ends with `game.state to=MENU` at t=58 s although nobody touched the menu.
**Cause:** `--loadlevel 10` starts a **new game**, and level 10 is the scripted intro, not a playable
start: the script blocks the player's controls (`BLOCK_PLAYER_CONTROLS`, so damage to the player is
not applied) and shows cinema borders (`cinemascope`, so `renderInterface()` skips the HUD whose
update finishes the inventory's closing animation — `player.Interface` keeps `INTER_INVENTORY`). The
intro (`cinematic.skip` at t=29.55, NPC spells at t=52–56) ends in the main menu at t=58.4. Damage
from the player's own `dodamage` is also ignored: the source must be another entity.
**Do:** for anything that needs the player in control, send `setplayercontrols on` and
`cinemascope off` first, damage the player from an NPC (`human_base_0033.dodamage player 1000`), and
keep level 10 runs under 50 s — or start from a playable save (`-Saves`, the test save library #70).
The game event log (#130) shows all of it with `t=` times; read it before suspecting the feature.

## 2026-10-09 — The game crashes in `ScriptEventName::toString()` for a console command

**Symptom:** a new log line that names the script event of a failing command crashed the game
(`ACCESS_VIOLATION`, stack `Script.cpp:180` ← `ScriptEvent.cpp` ← `Console.cpp`) the moment a
console / channel command failed; the same line was fine for commands failing inside real scripts.
**Cause:** console lines run as the pseudo-event `SM_EXECUTELINE` (and jumps as `SM_DUMMY`), which
have no `"on <name>"` entry; `toString()` asserts on that (a no-op in Release) and takes
`substr(3)` of whatever came back. The engine itself only prints events through `operator<<`,
which special-cases both.
**Do:** never call `ScriptEventName::toString()` without handling `SM_EXECUTELINE` / `SM_DUMMY`
first (see `script.error` in `ScriptEvent.cpp`, #130), or print through `operator<<`.

## 2026-10-09 — A lever, a chest, a door cannot be found by coordinates (#70)

*Resolved (#210):* `approach <entity>` finds the spot in the game (TESTING.md); cause (2) below is wrong -
`setpos` takes the **feet** (the 2026-10-13 entry), so the eye was 340 units above the floor, which explains
the wall tops and the picks that found nothing.

**Symptoms:** `setpos` next to `lever_0012` / `chest_metal_0052` and a screenshot shows a bare wall, a
black screen, the player's own feet, or a goblin; the entity is nowhere in the picture. About 6 of
the 13 launches of #70 went into it before this was written down.
**Causes, all measured:** (1) an entity's `pos` — from `entities` — is its **floor** position, so
looking at "the entity" from 2 m with pitch 0 aims 1.7 m above it; `lookat` now aims 40 units above the
floor and sets the pitch. (2) *Wrong, see the 2026-10-13 entry "`setpos` y is the feet":* this entry
first took `setpos`'s `y` for the eye and placed the player with eye coordinates, which put the feet 170
units above the floor - the eye 340 above it, on wall tops, out of the pick's reach. (3) The
level is narrow corridors and cells: many points 1.5-2.5 m away on a ring around an object are
inside a wall or in another room (black or wall-only screenshots in the contact sheets of the 4
compass points around each of 7 objects); which ones are valid cannot be told without a picture. (4) The pick that decides what a click acts on is not exposed: `FlyingOverIO` is cleared in
mouse look (`ArxGame::render`: `PLAYER_MOUSELOOK_ON`), and `FlyingOverObject(screen centre)` read from
the channel found nothing from any of the 20 level markers within 700 units of the levers, chests and
portcullises either (3 hits for `note_0004` with `FlyingOverIO`, on the rings). Why is not known.
**Do:** place the player for **items and NPCs** by coordinates and a screenshot (what `mvp-items` and
`mvp-goblin-fight` do, `docs/vr/TESTING.md`); for a lever, chest or door the owner records the save by
hand in flat mode (`--loadlevel 1`, walk there, F5/save), or a task adds a way to see what the engine
picks (log `InterClick` at the centre with the distances it tests: `InterClick` needs the entity within
360 units of the **eye**, i.e. about 3.2 m along the floor, and its bounding box under the cursor). Do not
spend launches on ring or marker searches again.

## 2026-10-09 — The awakening of a new game blocks the controls for 30-40 s; a save made then is no use (#70)

**Symptom:** a save made right after `newgame` loads into a hero lying on the floor with the letterbox
on, the camera at the floor, and nothing responds (`player.y` 2837 instead of 2935).
**Cause:** the level 1 script plays the awakening; `BLOCK_PLAYER_CONTROLS` stays `true` (status.json
`player.controlsBlocked`) for 30-42 s (3 runs). It is not a `.cin`: the cinematic skip of #124 does
not apply, and no `Skipping cinematic` line is logged. A save keeps the blocked state.
**Do:** wait for `player.controlsBlocked` to be `false` before the first `save` (`@until
player.controlsBlocked False` in a scenario); never infer "playable" from `state: GAMEPLAY`.

## 2026-10-09 — `--loadslot N` is the Nth newest save, not a folder; and a console `equip` does nothing (#70)

**Symptoms:** (1) after another save was copied or made, `--loadslot 1` loads something else; (2)
`equip short_sword_0030` is acked `errors=0` and the sword is not equipped.
**Causes:** (1) the list of saves is sorted by the save file's time, **newest first**, at one-second
resolution (`SaveGameList::update`): a slot number is a position, and two saves made in the same second
swap. (2) `equip <target>` is the **item's** command (`ARX_EQUIPMENT_Equip(target, context entity)`):
typed for the player it asks to equip the player on the sword, which does nothing and says nothing.
**Do:** (1) to load a given save, make it the only one — `smoke-test.ps1 -Save <name>` /
`run-windows.ps1 -Isolated -Save <name>` do — and keep a second between saves of a scenario script;
(2) `short_sword_0030.equip player`, and check `entities short_sword_0030` says `show=6`.

## 2026-10-09 — Scripting a launch from PowerShell: three tools that quietly do the wrong thing

**Symptoms:** (1) `pwsh -File run-windows.ps1 ... -- --no-vr` fails with "parameter name '' is ambiguous";
(2) a script waiting for `status.json` to say `MENU` times out although the game just started — the
first thing it reads is `"state":"EXITED"` of the **previous** run; (3) a date written to a JSON store
with `ToString('o')` comes back from `ConvertFrom-Json` as a `DateTime` and `[DateTime]::Parse(...)` of it
throws or loses the precision.
**Cause:** (1) `pwsh -File` consumes the `--` itself; (2) `run-windows.ps1` does not clear the test
channel directory (the smoke test does); (3) PowerShell 7 turns ISO date strings into objects.
**Do:** (1) start such a launch with `pwsh -Command "& .\scripts\run-windows.ps1 ... -- --no-vr"`;
(2) delete `status.json` before launching and wait for a state *and* the new pid; (3) store epoch seconds
(`log-digest-lib.ps1` does), never date strings, in JSON read back with `ConvertFrom-Json`.

## 2026-10-09 — `vr.shownFrames` stays at 1 while `vr.frames` grows: the frame loop looks broken

**Symptom:** a VR self-test with nobody in the headset reports `frames=834 shown=1`; the log has
one `First VR frame shown` line and nothing after it, as if the layers stopped being submitted.
**Cause:** with an idle headset the session reaches `synchronized` but never `visible`, and SteamVR
sets `XrFrameState::shouldRender` only for the very first frame; the game then submits frames
without layers (as the spec asks) to keep the timing. The one shown frame proves that the whole
layer path (acquire, clear, copy, release, two layers in `xrEndFrame`) ran without an error.
**Do:** judge the frame loop by `vr.frames` (must grow) and by the absence of `[E]` lines, not by
`vr.shownFrames`; whether the picture is right is the simulated backend's (#112) or the owner's
question. A `shownFrames` that grows with an idle headset would mean the runtime changed its
behaviour, not that the game got better.

## 2026-10-09 — A press page says a closed-source mod is "MIT", or that a feature exists the code removed

**Symptom:** research on a reference mod finds two accounts of the same fact: MoguraVR's summary table
lists CircuitLord's Titanfall 2 VR as "MIT" and two pages say a player can copy their flat-game saves
into the VR profile, while the author's README says "the mods are closed source … all rights reserved"
and the installer's own test asserts that the "copy save" button does not exist.
**Cause:** summaries repeat each other and the tool's earlier versions; the installer's MIT licence is
read as the mod's, and a feature removed in v1.2.4 lives on in pages written before it. A licence
line or a feature list copied from a press page into the reference catalog would put a wrong
"copy this" or "this exists" into a decision.
**Do:** for a licence, read `LICENSE` and the README of the repository that ships the thing, and the
installer's third-party notices; for a feature of a closed mod, prefer the author's text and the
installer's code and tests over articles, and label every catalog row with its evidence (code, docs,
add-on, report). When primary and secondary sources disagree, say so in the row.

## 2026-10-09 — `next-task.ps1` says the MVP queue is empty; `set-status.ps1` fails to move a task

*Resolved for the dev scripts by #186: a board that cannot be read is the cache (with a warning) or an error, never
"empty"; `set-status.ps1` queues the move. The cause below stays true for any hand-typed `gh project ...`.*

**Symptom:** `.\scripts\dev\next-task.ps1` prints `NEXT none — the MVP queue is empty` although
`docs/vr/MVP.md` and the board are full of Todo tasks, and `.\scripts\dev\set-status.ps1` throws
`Failed to move #N to <status>`; both outputs start with `GraphQL: API rate limit exceeded for
user ID …`.
**Cause:** the board scripts read the project through `gh`'s GraphQL API, and that hourly quota is
shared by every session of the owner's account (the coordinator, every worker, `cycle-report.ps1`).
When it is spent the project query returns nothing and the scripts read "nothing" as "no tasks".
`set-status.ps1` may still have landed the move: its `#N -> <status>` line is printed before a
trailing query fails.
**Do:** read the reset time with `gh api rate_limit --jq '.resources.graphql'` and retry after it;
never conclude "queue empty" or "move failed" from an output that carries the rate-limit line; check
the board in the browser when in doubt.

## 2026-10-09 — A new PR gets no CI run: `gh pr checks` says "no checks reported" for minutes

**Symptom:** the PR is open, other PRs got their `Build (Windows)` run within a minute, but this one
has none after 6 minutes (`gh run list --branch <branch>` is empty); closing and reopening the PR
changes nothing, `gh workflow run build-windows.yml --ref <branch>` runs green but is not attached
to the PR as a check.
**Cause:** the PR **conflicts with `main`** (`gh pr view <pr> --json mergeStateStatus,mergeable` →
`DIRTY` / `CONFLICTING`). A `pull_request` workflow runs on the merge ref (`refs/pull/N/merge`), and
GitHub cannot create it for a conflicting PR, so no run is created at all — silently, nothing on
the PR page says why. Here two PRs merged into `main` while the task ran (#153, #154) and both
touched `TESTING.md` / `TRAPS.md` next to this task's lines.
**Do:** when a PR shows no checks, look at `mergeStateStatus` first. `git fetch origin`,
`git merge origin/main`, resolve, push — the run starts on the push. Check `mergeable` again before
asking why CI is quiet; `gh pr merge` would also refuse a `CONFLICTING` PR.

## 2026-10-09 — `set-status.ps1` fails with "API rate limit exceeded" although `gh api rate_limit` shows thousands left

*Resolved for the dev scripts by #186 (cached board, one mutation, queue, "secondary limit" recognised and waited for or
queued). Still true for hand-typed `gh project ...`.*

**Symptom:** `set-status.ps1 -Issue <n> -Status Backlog` prints `Issue #<n> is not on the board yet,
adding it` and then `Failed to move #<n>`; run by hand, `gh project item-list` / `field-list` answer
`GraphQL: API rate limit exceeded for user ID …`, while `gh api rate_limit` reports
`graphql.remaining` in the thousands. Repeats for a quarter of an hour, then works.
**Cause:** the Projects commands are heavy GraphQL queries (`item-list` with the whole board), and
several sessions of the same account poll the board (`cycle-report.ps1`, `next-task.ps1`, workers'
`set-status.ps1`); GitHub's secondary / points limit for the account trips before the hourly
counter does. `item-add` went through each time, so the issue **is** on the board — without a status.
**Do:** retry later (minutes, not seconds); do not add the item again by hand (`item-add` is
idempotent, but every retry is another query). Mention the un-set status in the report if the limit
does not lift before the task ends. A board script should treat this error as "try again later",
not as "not on the board".

## 2026-10-09 — A `spellcast` on the player from the console casts nothing

**Symptom:** `spellcast -fs 5 magic_sight self` through the test channel is acknowledged with
`errors=0`, no `[W]`, yet nothing happens: no spell sound, no effect, `camera.fov` in `status.json`
stays at 75.00 although Magic Sight should widen it; a second `spellcast` on the player does nothing
either. The #71 "26 spells, 0 of 240 frames differ" rows were accepted on exactly this command.
**Cause:** `TryToCastSpell` launches a script-cast spell at once only with `NOANIM` **and** `NODRAW`
(`-s` and `-m`). Otherwise it is queued in `spellcast_data` for the rune-drawing loop
(`RuneDraw.cpp`), which skips the player entity — the spell never starts, and the queued
`castingspell != SPELL_NONE` makes every later `spellcast` on the player return at the first line.
There is no error anywhere: the script command succeeded, the launch simply never comes.
**Do:** cast on the player with `-fsm` (`spellcast -fsm 5 <spell> self`) and prove the spell is active
with a number — `spells.launchedByType` / `spells.activeByType` in `status.json` (#155), or the
spell's own effect (`camera.fov` 80.65 for Magic Sight, life / mana, a `shot`) — never with the ack
alone. `scripts/dev/spell-drawcheck.ps1` re-ran the #71 spells check that way (#155).
**Even `-fsm` can launch nothing:** every spell has its own `CanLaunch`, and a refusal is silent too
(at most a fizzle sound). Found in #155: `fake_summon` refuses the player as caster, and the player's
`summon_creature` / `raise_dead` find no valid spot 3 m ahead anywhere tried on level 10. Cast those
from an NPC (`human_base_0033.spellcast -fsm 5 <spell> player`) — and check `launchedByType`.

## 2026-10-09 — A worktree build keeps the commit of its first build in the log and the window title

**Symptom:** `arx.log` says `Starting Arx Fatalis VR 0.1-dev + a7e4255` (so does `status.json`
`version` and the window title) although the worktree is several commits further; `built-from.txt`
next to `arx.exe` names the right commit.
**Cause:** `Version.cpp` is generated by a custom command whose dependencies are git's `HEAD`,
`logs/HEAD` and `packed-refs` — but in a worktree `.git` is a *file* (`gitdir: <path>`), those files
live under the main checkout's `.git/worktrees/<name>/`, and the rule depended on nothing that changes
with a commit. The owner's copy (a real `.git` directory) never showed it, every worker's copy did.
**Do:** fixed in #115 (`cmake/VersionString.cmake` resolves the gitfile and the common dir, so the
reflog is a dependency again). The banner's `[W] built-from.txt names commit … but the executable
carries version information from …` says when a build id lies: rebuild, and if it persists,
reconfigure (`build-windows.ps1 -Clean`).

## 2026-10-09 — `status.json` says `window.focused: true` for a window that never got the focus

**Symptom:** a self-test started without activation (`SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN=1`, #147)
reports `window.focused: true` in `status.json`, although the foreground window (user32
`GetForegroundWindow`) is still the terminal and the game never took the keyboard.
**Cause:** `SDL2Window::initialize()` called `onFocus(true)` unconditionally after `SDL_ShowWindow`
— an assumption from the days when a shown window was always the active one. The flag was never
read from SDL; `SDL_WINDOWEVENT_FOCUS_GAINED` fixes it only when the focus really changes later.
**Do:** fixed in #147 (`onFocus(SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS)`), so
`window.focused` is now evidence. In general: when `status.json` and an OS-level probe disagree,
suspect the engine's bookkeeping before the probe, and check where the value is *set*, not where it
is reported.

## 2026-10-09 — SteamVR starts, and the headset wakes up, from "just a test launch"

**Symptom:** the owner's Index and base stations turn on, SteamVR's window appears, although nobody
asked for VR — only a worker's test ran.
**Cause:** the OpenXR loader starts the active runtime (SteamVR) inside `xrCreateInstance`. The
engine's guard (#17) refuses when the runtime manifest names SteamVR and `vrserver.exe` is not
running, but anything that bypasses the guard starts SteamVR anyway: `xr_hello --start-runtime`,
another active runtime, a raw `arx.exe` of a build without the guard, and, worst, a worker starting
`vrstartup.exe` by hand to get VR evidence (it happened in #93 — 15 VR runs, someone closed SteamVR
two minutes later).
**Do:** launch only through the scripts: `run-windows.ps1` and `smoke-test.ps1` add `--no-vr` while
`vrserver.exe` is not running and say so; `smoke-test.ps1 -Vr` is skipped (exit 2). Never run
`vrstartup.exe`, never stop `vrmonitor` / `vrserver`; VR evidence without the owner's SteamVR is the
simulator's job (#112). Only the owner passes `-StartSteamVR` (D-014).
## 2026-10-09 — A `shot` taken right after `GAMEPLAY` is all black

**Symptom:** `smoke-test.ps1 -Commands 'shot level10'` with `--loadlevel 10` saves a 640×480 capture
in which every pixel is black (frame 15, 0.15 s after `state GAMEPLAY`), while `status.json` reports
`GAMEPLAY`, 0 errors and a normal frame rate — as if nothing were rendered.
**Cause:** the level starts behind a fade from black. A capture from frame 86 on (0.9 s) shows the
scene (58 % of sampled pixels non-black in the draw-check captures and in a `shot` at frame 517,
#71). The only code path that starts a fade is the `worldfade` script command (`fadeRequestStart`),
so this is the level script's own fade-in, not the renderer; the first `-Commands` line runs the
moment the state flips, i.e. inside that fade.
**Do:** take `shot` captures at least a second after `GAMEPLAY` — put a few `status` lines in front
of the `shot` (each ack takes ≈ 0.25 s) or wait first; judge "is anything rendered" by a non-black
pixel count over several frames, never by one early capture. An early black shot is no evidence
against the renderer. (The save verification of #70 waits 4 s after a `loadslot` before its `shot`;
the dark room of `mvp-items` stays dark at that time too — it is the level, not the fade.)

## 2026-10-09 — The obvious owner fix for #93 changed nothing: the sword was linked to a mesh the NPC no longer used

**Symptom:** the exit crash #93 reads a freed owner through `m_owner`; releasing the NPC weapon
(`_npcdata->weapon`) before the NPC is deleted looked like the fix, yet a detector in `~Entity` still
reported `human_base_0033` deleted while it owned `long_sword_0021` on every exit from level 10 —
an entity that is not the NPC's weapon, not in its inventory and, by `isEntityLinked()`, not linked.
**Cause:** the level 10 script links the sword to the NPC and then applies a full mesh tweak
(`HEAD|TORSO|LEGS`): `EERIE_MESH_TWEAK_Do()` swaps in a new mesh **without** the links and keeps the
old one as `tweaky` (restored by `TWEAK_REMOVE`). The sword keeps `show = LINKED` and its owner, but
the link exists only in `tweaky`, which `destroy()`, `UnlinkAllLinkedObjects()` and `isEntityLinked()`
never look at.
**Do:** do not trust a guess about *which* relation owns an entity — log what the owned entity looks
like (`show`, `scriptload`, linked / in inventory / weapon) and trace `setOwner()` before fixing. The
fix releases every entity whose `owner()` is the dying one, whatever the relation (#93).

## 2026-10-09 — `smoke-test.ps1 -Commands 'loadlevel N'` fails with "null-valued expression" and kills the game

**Symptom:** `=> FAIL You cannot call a method on a null-valued expression.`; the game disappears in
the middle of loading the new level, with no crash report — it looks like the level change crashed.
**Cause:** a test-tool bug, not the game: the channel's `loadlevel` runs synchronously, the level
change writes `status.json` with the new `cmdSeq` before `ack.txt` exists, `Send-ArxCommand` reads a
missing ack, throws, and the smoke test kills the game in its error handler (#142).
**Do:** fixed in #142: `status.json` reports `cmdSeq` only once the batch's `ack.txt` is written, so
`loadlevel` / `load` batches work through the channel (the old `-- --loadlevel N` workaround is no longer needed).

## 2026-10-09 — A crash report folder is dated "yesterday" and the VR self-test crashes where the flat one is clean

**Symptom:** a run at 01:52 local time leaves `crashes\2026-10-08-22-52-43-00`; looking for a report
from "today" finds none, and an isolated user directory (`--user-dir`, #123) seems to have a
different crash than the shared one.
**Cause:** crash report folders are named in **UTC** (the `crash time` inside the report too); the
isolated directory simply has its own `crashes\` folder, so reports of a worker's runs are under
`%LOCALAPPDATA%\ArxFatalisVR\userdirs\<key>\crashes\`, not in `Saved Games\Arx Libertatis`. And the
crash that showed up only in VR (`smoke-test.ps1 -Vr`, 2 of 3 exits) is #93 again: the OpenXR session
changes the heap layout enough for the dangling `m_owner` read to fault without any cinematic loaded.
**Do:** match reports by the smoke test's `new crash report: <name>` line, not by the date; look in the
user directory the run used (the smoke test prints it). Treat a `-Vr` exit crash with the
`Entity::cleanReferences` ← `DanaeClearLevel` stack as #93, not as a VR bug — and remember `-Vr` is
not a gate until #93 is fixed.
*Resolved for the exit crash: #93 is fixed (no exit crash in 13 VR exits after the fix); `-Vr` is
still not a reliable gate because of #146 (the game exits with code -1 mid-run in 2 of 15 VR runs).*

## 2026-10-09 — The exit crash #93 needs no playing cinematic — a loaded one is enough

**Symptom:** closing the window after `--loadlevel 10` crashes in `Entity::cleanReferences`
(`DanaeClearLevel`) in 2 of 5 runs although no cinematic ever played and the log shows only
`Loading cinematic "…/introduction.cin"`; quitting through the channel is clean 5 of 5.
**Cause:** the level 10 script only *preloads* the intro (`cine -p introduction`); `cine play` comes
from the real new-game flow, never from `--loadlevel 10`. A loaded `.cin` (its bitmaps) is enough to
change the heap so that the dangling `m_owner` read of #93 faults; the window-close path is more
exposed than the channel quit. The first version of #124 skipped cinematics by loading them and
ending them at once — and brought the crash back.
**Do:** a skipped cinematic must never be loaded (#124 ends it through `cinematicEnd()` without
loading the file). Test exits with `-CloseWindow` as well as with the channel quit: they fail
differently. `--intro` / `--cinematics` remain exposed to #93 until it is fixed.
*Resolved: #93 is fixed — the loaded intro with `-CloseWindow` was 10 of 10 clean after the fix. The
cinematic only changed the heap layout; the dangling owner was there on every exit.*

## 2026-10-09 — An issue body edited from PowerShell lost all its line breaks

**Symptom:** after `gh issue edit --body-file`, the issue body is one long line, and the `-replace`
that should have inserted a section into it did nothing.
**Cause:** PowerShell captures `gh issue view --json body -q .body` as an **array of lines**, not a
string: a multi-line regex never matches, `-eq` between two arrays is not a "did it change" check,
and `Set-Content -NoNewline` with an array joins the lines **without** separators.
**Do:** write issue bodies from a here-string or with `[IO.File]::WriteAllText`; to get a body as one
string use `gh … | Out-String` (or `-join "`n"`); read the issue back and check its line count before
moving on. (Hit on #124, 2026-10-09; the body was restored from the original text.)

## 2026-10-08 — Closing the game 30 s after `--loadlevel 10` crashes 4 times out of 5

**Symptom:** a scripted flat launch (`--no-vr --loadlevel 10`, close the window after 30 s) ends with
"Illegal storage access" (`Entity::cleanReferences` ← `DanaeClearLevel` ← `ArxGame::shutdownGame`)
in 4 of 5 runs, and the crash dialog stays on the screen.
**Cause:** `--loadlevel 10` starts a new game, the level 10 script plays the intro cinematic, and
closing during the cinematic is bug #93 (an entity owner is destroyed before its dependants).
**Do:** development launches now skip the intro cinematic (`--intro` plays it; #92 restores it for
players), so `--loadlevel 10` lands in GAMEPLAY after ~1 s and closing is clean (10/10 with the
channel's `quit`, measured in #99). #93 itself is still open for the `--intro` / player path.
*Resolved: #93 is fixed (an entity linked to a swapped-out tweak mesh kept a dangling owner).*

## 2026-10-08 — `arx.log` is empty or stale while the game runs

**Symptom:** a test that reads `arx.log` while the game is running sees nothing of the current run
(or only the first few KB).
**Cause:** the file logger writes through an `ofstream` that is never flushed until shutdown; a crash
closes it (so a crashed run *does* leave a log, without `Clean shutdown`).
**Do:** start the game with `--test-dir <dir>`: the test channel flushes the log every 250 ms together
with `status.json`, and `[game] state:` / `[mark] <text>` lines give a script fixed anchors. Without
the channel, read the log only after the process exited.
*Resolved: since #129 the file logger flushes a warning or error at once and everything else at
least once a second from its own flush thread (so a stuck main thread does not stop it). A
`Stop-Process` 15 s into a flat run without the channel left a complete log ending with `Done
loading level`, where the old logger left 4137 bytes cut mid-line. The channel's 250 ms flush stays
for tests that want the log fresher than a second.*

## 2026-10-08 — The crash dialog outlives the game and opens Explorer when dismissed

**Symptom:** after a crash a "Fatal Error - Arx Fatalis VR" window stays open although `arx.exe` has
exited with code 1, and closing it opens the crash report folder in Explorer.
**Cause:** the crash processor is a second `arx.exe --crashinfo=…` that shows a message box and then
*always* launches the report directory; without `arxcrashreporter` nothing closes it for a script.
**Do:** automated runs set `ARX_UNATTENDED=1` (the test channel does it for the game, `smoke-test.ps1`
for the process): the report is still written, the dialog is skipped. Since #501 every scripted launch sets
`ARXVR_NO_DIALOGS=1`, which does the same for the crash dialog and every other one. A leftover dialog is best
stopped with `Stop-Process` (closing it would open Explorer).

## 2026-10-08 — A rebuild fails with `LNK1104: cannot open file 'arx.exe'`

**Symptom:** the build fails at the final link although nothing in the code is wrong.
**Cause:** the game from that build directory is still running (a smoke test, the owner's window, a
forgotten process) and Windows keeps the executable locked. A CMake reconfigure can relink `arx.exe`
even when no source changed.
**Do:** `Get-Process arx` before building; wait for the smoke test to finish (it refuses to start
while another `arx.exe` runs, for the same reason).

## 2026-10-08 — `@(... | Measure-Object).Count` is always 1

**Symptom:** a PowerShell probe reported one `[E]` line in every log, including logs without any.
**Cause:** `Measure-Object` returns a single object; wrapping *that* in `@()` and taking `.Count`
counts the wrapper, not the matches.
**Do:** `@(Select-String …).Count` or `(… | Measure-Object).Count` — and verify a check can fail
(a log with no errors must give 0) before trusting it.

## 2026-10-08 — A board script "succeeded" but changed the wrong issues

**Symptom:** a PowerShell script printed "retitled #36" / "noted #18" for every issue, but titles were
unchanged and comments were empty or belonged to another issue.
**Cause:** `$ordered[36]` on an `[ordered]` dictionary indexes by **position**, not by key, when the
index is an integer (out of range → `$null`, in range → some other entry). Native commands (`gh`)
failing with a non-zero exit code do not stop a script even with `$ErrorActionPreference = 'Stop'`.
**Do:** iterate `[ordered]` dictionaries with `.GetEnumerator()` (`$_.Key` / `$_.Value`) or use a plain
hashtable for lookups; check `$LASTEXITCODE` after every `gh` call; verify the result afterwards
(read the issues back) instead of trusting the script's own output.

## 2026-10-08 — CI downloads hang or time out

**Symptom:** a CI run sits in "vcpkg install" for half an hour or fails with
`error: Download timed out` from `github.com`.
**Cause:** the network to GitHub from the owner's PC is flaky at times, and a vcpkg without a cache
downloads every port source again.
**Do:** CI and local builds share `C:\vcpkg-shared` (vcpkg checkout, downloads, binary cache — see
WORKFLOW.md); a one-off timeout is re-run once (`gh run rerun <id>`), not debugged.

## 2026-10-08 — A freshly built `arx.exe` does not start

**Symptom:** a build that ran minutes ago is suddenly blocked, or the game "crashes" before writing
anything.
**Cause:** Windows **Smart App Control** is enforced on the owner's PC
(`HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy\VerifiedAndReputablePolicyState = 1`) and blocks
unsigned files by reputation ("An Application Control policy has blocked this file").
**Do:** before debugging the engine, check
`Get-WinEvent -LogName Microsoft-Windows-CodeIntegrity/Operational` for event 3077 naming `arx.exe`.
Report it to the owner; never try to bypass it (turning Smart App Control off is irreversible and the
owner's call). Relevant for tester builds too (#88): unsigned builds may be blocked or warned about.

## 2026-10-08 — `arx.log` shows an old run

**Symptom:** after a launch the log looks unchanged, as if the fix did nothing.
**Cause:** `%USERPROFILE%\Saved Games\Arx Libertatis\arx.log` is written only on a **clean
shutdown**; a launch that failed (or was killed) leaves the previous run's log in place.
**Do:** compare the log's `LastWriteTime` with the launch time (or delete it first), and close the game
with `Process.CloseMainWindow()`, never `Stop-Process`.
*Resolved for the log: since #129 the previous run's log is rotated to `arx.1.log` at startup and the
current run's lines reach the disk within a second, so a killed run leaves its own log (the `Run:`
banner line names the pid and the start time; the smoke test no longer deletes `arx.log` before a run,
the history would go with it). `Stop-Process` still skips `Clean shutdown` and the channel's final
status: close the game properly whenever those are the evidence you need.*

## 2026-10-08 — `--data-dir` with the Steam path breaks the command line

**Symptom:** `arx.exe` prints `[CRITICAL] CommandLine:68 Error parsing command-line argument` to
stdout and exits with code 1 without a log.
**Cause:** `Start-Process -ArgumentList` does not quote arguments; `...\common\Arx Fatalis` has a space.
**Do:** quote it (`"--data-dir", "`"$dir`""`) and capture `-RedirectStandardOutput` /
`-RedirectStandardError`; `scripts/run-windows.ps1` already does the right thing.

## 2026-10-08 — A worktree session's branch has none of our work

**Symptom:** a task branch has no `docs/vr/`, no VR code; its PR cannot be merged into `main`.
**Cause:** `EnterWorktree` bases new branches on `origin/master` — the untouched upstream mirror —
because git sees `master` as the default branch, while our development branch is `main`.
**Do:** right after entering the worktree: `git branch -m ARXVR-<n>-<type>-<name>`,
`git reset --hard origin/main`, verify with `git merge-base HEAD origin/main`. The worktree guard
also refuses `git -C <owner copy>` and commands with runtime-computed paths: use literal paths.

## 2026-10-08 — The build pulls in Visual Studio's own vcpkg

**Symptom:** dependencies are rebuilt from source although they were built before.
**Cause:** `VsDevCmd.bat` sets `VCPKG_ROOT` to the vcpkg bundled with Visual Studio.
**Do:** `scripts/build-windows.ps1` reads `VCPKG_ROOT` before loading the MSVC environment — keep it that way.

## 2026-10-08 — CMake refuses a build directory

**Symptom:** `CMake Error: The source "…" does not match the source "…" used to generate cache`.
**Cause:** one build directory was configured from another working copy (the main clone vs. a worktree).
**Do:** build directories are per working copy (`scripts/build-paths.ps1`); do not share them by hand.

## 2026-10-08 — `rc.exe` fails with RC1203

**Symptom:** the build stops at `arx-version.rc` with `RC1203: invalid language ID`.
**Cause:** C/C++-only MSVC flags added with `add_definitions()` reach the resource compiler.
**Do:** add compiler-only flags with `add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:...>)` (fixed in #12).

## 2026-10-09 — `claude --bg` refuses a worker's worktree: "Workspace not trusted"

**Symptom:** `start-worker.ps1` fails with `Workspace not trusted. Run claude in <worktree> once and accept
the trust prompt`, even after the owner trusted the parent folder `%USERPROFILE%\arx-fatalis-vr-wt`.
**Cause:** Claude Code's workspace trust is checked for the launch folder itself; trusting a parent does
not cover a new subfolder. Every worktree is a new folder.
**Do:** launch workers in the owner's copy (trusted) and let them enter the worktree first —
`start-worker.ps1` passes `/next-task <n> worktree=<path>` (D-011). Never write trust flags into
`~/.claude.json` by hand: the owner gives trust.

## 2026-10-09 — PowerShell: `[math]::Max(0, 4.5)` is 4, and `ConvertFrom-Json` turns date strings into DateTime

**Symptom:** a rate computed with `[math]::Max(0, $double)` is rounded down to an integer (a worker count came out one too
high); `[datetime]::Parse($obj.updated)` on a JSON field gave a date a month off.
**Cause:** with an `int` literal first, PowerShell picks `Max(int,int)` and converts the double; PowerShell 7's
`ConvertFrom-Json` already returns ISO-8601 strings as `DateTime`, and parsing its culture-formatted string swaps day and month.
**Do:** write `[math]::Max(0.0, $x)`; take a field that may be a `DateTime` or a string through a helper
(`ConvertTo-Epoch` in `scripts/dev/usage-lib.ps1`).

## 2026-10-09 - A deliberate crash check files a bug issue

**Symptom:** the `crash` / `crash steal` channel check (#132) opened an `auto-reported` issue (#235).
**Cause:** the run digest files every new crash signature (#131).
**Do:** the digest skips filing for a run whose log has `[crash] deliberate crash requested`; if another deliberate fault is added, log the same marker line first.

## 2026-10-09 — .xrs: a timeout without its `s` was part of the value; `$g` and `$G` are one variable

**Symptom:** `@until state eq GAMEPLAY 60` waited 30 s and failed with "never became eq GAMEPLAY 60" while the game was in GAMEPLAY;
a PowerShell test helper with parameter `$G` and a local `$g` failed with "cannot convert Graphics to Int32".
**Cause:** the runner took everything after the operator as the value; PowerShell variable names are case-insensitive, so `$g` and `$G` are one variable.
**Do:** write timeouts as `60s` (the parser now rejects a value with a trailing bare number); never name two variables in one scope by letters that differ only in case.

## 2026-10-09 — VR: picking finds nothing although the item is in the middle of the view

**Symptom:** in a stereo frame (`--vr-sim` or the headset) the mouse hovers nothing — no highlight, `pick` finds nothing on
the whole window — while the item is plainly on screen; the item's `bbox2D` is in the thousands (`597 1066 657 1113` in a
640x480 window).
**Cause:** the scene is drawn once per eye, and drawing an entity stores its screen data (clip positions, items'
`bbox2D`) for the camera of the draw — the last eye, at the eye image's size. Mouse picking reads that data next frame
against window coordinates (#72).
**Do:** anything that keeps screen positions from a draw must be projected again with the head camera after the eyes
(`ProjectEntitiesForPicking()` after `renderStereoViews()`); check with `pick <entity>` (`targetSeenInBox`).

## 2026-10-09 — `specialfx newspell` logs `[W] unexpected newline`

**Symptom:** the script command `specialfx newspell` (the book icon's FX) works, but adds a `[W] ScriptUtils … [player:N] :
unexpected newline`, which the run digest reports as a new warning.
**Cause:** the command skips one word after `newspell`; with nothing after it, it reads the end of the line.
**Do:** send `specialfx newspell 1`.

## 2026-10-09 - Send-ArxCommand returned the previous batch's ack

**Symptom:** `test-xrs.ps1` failed only on the slow CI runner: the ack of a check's first command read `last=refuse this`, the command of the check before (#250).
**Cause:** the game writes `ack.txt` first and `status.json` later; `Send-ArxCommand` took its baseline `cmdSeq` from `status.json` and waited for it to advance, so the late status update of the *previous* batch ended the wait and the old ack was returned.
**Do:** the baseline is the newest seq of ack.txt and status.json, and the answer is `ack.txt` itself advancing (`Read-ArxAck` in `scripts/dev/test-lib.ps1`). After a send, `status.json` may still lag behind the ack - wait for the state you assert on (`@until`, `@frames`).

## 2026-10-09 — The stick walk is applied on level 10, but the player does not move

**Symptom:** `walk-forward.xrs` on `--loadlevel 10` with `setplayercontrols on`: `vr.walk state=start` / `end` are logged
and `vr.input.walk.walking` is true, yet `player.x` / `player.z` stay exactly the same.
**Cause:** level 10 is the scripted intro (see "On `--loadlevel 10` the inventory never closes" above): the movement
the controls ask for is not applied to the player there - not a locomotion bug.
**Do:** check walking on a playable save: `smoke-test.ps1 -VrSim idle -Save mvp-new-game -Sequence walk-forward` (#31).

## 2026-10-10 - Crouched in the start cell, the character never stood up again
**Symptom:** on the test save `mvp-new-game` the character crouched (button, toggle or the head, #100) and never stood up: the toggle went off (`player.crouch state=off`), `PLAYER_CROUCH` stayed set for good; on `mvp-items` the same sequence stood up (#263).
**Cause:** the engine's own stand-up check, not VR code - flat Arx Libertatis did the same. `ARX_PLAYER_Manage_Visual` checked the **whole** standing cylinder with `CheckAnythingInCylinder`, and at the awakening spot that cylinder grazes a sloped floor polygon 21 units above the feet (`background=-21.0 ... normY=-0.3`). Standing there is fine - the check runs only to stand up - but the crouched character could never pass it.
**Do:** the stand-up (and the jump from a crouch) now checks only the part of the standing cylinder above the crouched one (`standUpCylinder()` in `src/game/Player.cpp`). A refused stand-up logs one `player.standup.blocked` event with what is in the way (`describeCylinderBlocker`); read it before blaming crouch code. `stand-up.xrs` checks both directions.

## 2026-10-13 - A `--vr-sim` run drops to 20-40 fps for a few seconds with nothing happening in the game
**Symptom:** an idle `--vr-sim` run (`idle.vrsim`, no commands) shows several `[vr] rate:` windows UNDER-SUBMITTING at 20-40/s with a worst frame near 100 ms, then 90/s again; `arx-perf.log` has a burst of `H` lines whose `update`, `render` and `vrwait` all grow together, with no `load` and no uploads (#327: in one pair of runs the burst came with the log on, in the next pair with it off).
**Cause:** the PC, not the game: other workers build in parallel (the game lock serialises game runs, not compiles), and a starved CPU stretches every phase of the frame at once.
**Do:** judge pacing and frame costs only from windows at full rate, or repeat the run; a real cost of the game shows in one phase (`cause`), the same way in every run. `perf-summary.ps1` lists the hitches by cause.

## 2026-10-14 - Kultar's scene never starts although the hero walked into KULTAR_WAKE
**Symptom:** on a save in the start cell the hero walks into the zone KULTAR_WAKE (`player.z` well inside it), yet `scene.active` stays false for a minute: no letterbox, no camera, no teleport (#29).
**Cause:** the zone only sends Kultar to the bars between the cells (`MARKER_0117`, 9103 8836) and has him say his introduction; his scene (`>>dialogue`: the teleport, the cameras, the letterbox, `playerlookat`) starts from his `CHECK_DIST` timer only while `^DIST_PLAYER < 500` - the hero within 5 m of him. Walking straight ahead from the middle of the cell ends at the portcullis about 6 m away; at x 8740 and beyond, the cell's wall holds the hero at once.
**Do:** start from the test save `scene-kultar` (8700 8660, facing +z: the walk stops 4.1 m from Kultar) or `setpos 8700 3105 8840` (`scene-flat.xrs`). The zone fires once per game: a second try needs the save loaded again.

## 2026-10-14 - A console `speak -c` line ends after about 2 s although its sample is longer
**Symptom:** `<npc>.speak -c ... [kultar_letmeout]` (a 5 s sample) through the test channel ends after about 2 s - a sequence's input in that line runs past its end; the same line from the game's own script plays in full (#29).
**Cause:** without the letterbox (`cinematicBorder` off) a `speak` line is a "no text" line, and `ARX_SPEECH_AddSpeech` plays a random variant of its sample: `kultar_letmeout2` / `3` are 2.1 s. Lines with the letterbox on (a real scene) never pick a variant.
**Do:** for a line that must last, take one without variants (`kultar_cantgo`, 8.4 s; `kultar_traveler`, 22 s) or turn the letterbox on first.

## 2026-10-14 - A new game's hero stands off the level for 2 s before the prison intro
**Symptom:** the scene meter's `minAboveFloorM` is 0 for a stretch from `newgame`: the eyes in first person over no floor (#29).
**Cause:** after `GAME_READY` the intro waits 2 s (letterbox on, controls off) before its `TELEPORT -PA 180`; until then the hero is at the level's editor position, where `CheckInPoly` finds no floor.
**Do:** every scene view keeps the fade black while there is no floor under its eye; any measurement from `newgame` meets these 2 s.

## 2026-10-14 - A console `speak` line of a variant's sample name shows no subtitle
**Symptom:** `<npc>.speak -c ... [kultar_letmeout2]` plays, but `scene.subtitle.text` stays empty and `voicedLines` does not count it (#29).
**Cause:** a speech variant's text is not a section named after its sample (`kultar_letmeout2.wav`): it is the key `string2` in the line's own section `[kultar_letmeout]`. No section `[kultar_letmeout2]` exists.
**Do:** speak the line's own key (`[kultar_letmeout]`); the subtitle takes the text of the variant actually played (`string<N>`).
## 2026-10-13 - A file under data/core is in the repository but the game says "not found"
**Symptom:** `[W] VRArms: [vr] arms: vr/arms/Hand_Glove_low_R.gltf not found` (#385.3), while the file is in `data/core/vr/arms/` and the build's data dirs point at `data/core`; the host tests that load the same file pass.
**Cause:** two rules of the resource system. `src/io/resource/ResourceSetup.cpp` mounts only a fixed list of top-level folders of each data dir (`editor`, `game`, `graph`, `localisation`, `misc`, `sfx`, `speech`, and now `vr`), so a new folder is invisible until it is added there; and every resource name is lower case (`PakReader::addFiles`), so `vr/arms/Hand_Glove_low_R.gltf` must be asked for as `vr/arms/hand_glove_low_r.gltf`. Host tests read data/core straight from the source tree and see neither rule.
**Do:** a new folder under data/core goes into that list in the same change; ask for resources in lower case; prove an asset in a game run (a sequence that asserts the loaded state), not only in a host test.

## 2026-10-13 - A test or source file fails to compile with C2059 "syntax error: 'const'" on an innocent name
**Symptom:** `tests/vr/ArmMeshTest.cpp(48): error C2059: syntax error: 'const'` on a helper `void near(...)`, or C2628 "'vr::SleeveParams' followed by 'char'" on a variable named `small` (#385.1).
**Cause:** `arx` and `arxtest` are unity builds (`ub_arxtest.cpp`): `windows.h`, included earlier in the same unit by FilesystemWindows.cpp, Process.cpp or WindowsUtils.cpp, defines the macros `near` and `far` (empty) and `small` (`char`). Names in anonymous namespaces also collide across the files of one unit.
**Do:** never name anything `near`, `far` or `small`; give file-scope helpers unique names (a file prefix).
## 2026-10-13 - In the playground a hand's press lands on the panel, not on the lever it aims at
**Symptom:** `--vr-sim` in the playground (`-Playground`): the reticle is on a lever (`vr.reticle.right.entity`), grip + trigger fires `act_right`, but nothing is pulled; `vr.pointer.click hand=right` is logged instead, `vr.panel.menu` is `inventory`. `ui inventory close` closes the bag and the next frame opens it again (#38).
**Cause:** a level started with `--loadlevel` stays in the engine's cursor mode (no free look), and in cursor mode the engine keeps the inventory open: the floating panel hangs in front of the hands and takes their rays. Opening a chest or a body leaves free look again (the original's own rule).
**Do:** `freelook on; ui inventory close` before the hands act in the world, and again after a drag from a chest or a body (`playground-hands.xrs`). For the player this goes with the first-run flow (#394 / #405).

## 2026-10-13 - `vr-point` hits the wall behind a lever, or the hover passes and is gone again
**Symptom:** `vr-point right entity lever_9002`: the ray ends on the wall 5 units behind the lever (`vr.reticle.right.point` z 8400), `under` stays `none`; or `@until vr.reticle.right.entity eq lever_9002` passes and the act a few frames later finds nothing (#38).
**Cause:** two things. A lever on a wall is thin: the middle of its box is air, and a pulled lever's handle lies against the wall. And turning a simulated hand moves the start of its aim ray (it turns about the grip): the aim is computed from where the ray started before the turn, a few degrees off for a small target - the hover can catch the lever on the way and keep it for its 150 ms hold.
**Do:** `vr-point <hand> face <id>` (a face of the mesh the hand's ray reaches without the level in the way), given twice a few frames apart; wait for `under` (this frame) before `entity` (the hover).

## 2026-10-13 - An item dragged from a chest onto the bag falls back into the chest or is thrown
**Symptom:** `vr-point right container`, trigger down, `vr-point right <bag pixel>`, trigger up: `item.toinventory container=chest_...` (back in the chest) or `item.drop how=throw`, never into the bag (#38).
**Cause:** the engine puts a dragged item where its **icon** is - the cursor plus the offset the icon had when the drag started (`g_draggedIconOffset`). A simulated hand jumps: the pointer holds the press pixel inside its click dead zone (16 px), so the drag starts only on the jump, with the cursor already at the bag and an offset of hundreds of pixels; and after a short move the icon still hangs beside the cursor, left of the slot it aims at.
**Do:** move the hand past the dead zone first (`vr-point right container 30 0`, a few frames), then `vr-point right bag` - it aims so that the icon covers the first free slot. A real hand moves gradually and places the icon by eye.

## 2026-10-13 - The character creation shows only the hero's portrait on black
**Symptom:** after a development launch (`--dev-launch`) or a key during the publisher logos, New game opens a character creation with only the portrait; the background and every widget are missing, for good (no fade). After the level 10 intro (a player's launch, `--skiplogo`) the same screen is fine (#399).
**Cause:** the portrait (`Book.cpp`) prepares its own camera and then restores the one prepared before it with `RestorePreparedCamera()`, viewport included. Without a level ever loaded no camera was prepared: the restored viewport is the default `0 x 0`, and everything drawn after the first portrait - the rest of that frame, the background of the next - lands in it.
**Do:** a 2D screen that can come before any level sets its own viewport (`GRenderer->SetViewport(g_size)`, as the logos and `CharacterCreation::render()` do) and sets it again after drawing anything that restores a camera. Check such a screen from a development launch, not only after the intro.

## 2026-10-13 - In VR the desktop mouse does nothing
**Symptom:** in a VR session (the headset or `--vr-sim`) moving, clicking or scrolling the mouse over the game window changes nothing - the menu under the cursor is not clicked, the camera does not turn.
**Cause:** by design (#338, the owner): in VR the window only mirrors the headset; `Input::update()` drops the real mouse's movement, buttons and wheel while `vr::isEnabled()` (`desktopMouse` in `status.json`, `[input] desktop mouse:` lines in `arx.log`). Since #487 the keyboard is dropped too (below).
**Do:** in VR aim the hands (`vr-point`, the controllers); test the real mouse flat (`--no-vr`) with the channel's `desktop-mouse`.

## 2026-10-14 - `@key` fails in CI with "process has no window"
**Symptom:** a sequence that passes locally fails in CI at its first `@key`: `@key: process <pid> has no window` (#399, PR #458).
**Cause:** `@key` posts a keyboard message to the game's main window. The CI runner is a service in session 0, with no desktop, so the game process has no window handle to post to.
**Do:** in a sequence CI runs, press keys through the test channel: `desktop-key <name> down`, a few frames, `desktop-key <name> up` (SDL's event queue, like `desktop-mouse`).

## 2026-10-16 - A test run ignores my mouse and keyboard, and in VR the keyboard does nothing
**Symptom:** clicking, scrolling or typing into the window of a test run (`--test-dir`: the smoke test, `xrs-run.ps1`, CI, `run-windows.ps1 -TestChannel`) changes nothing, the window cannot be activated and sits in the bottom right corner; an `.xrs` `@key` does nothing any more. In VR (headset or `--vr-sim`) no key of the desktop keyboard - not even the channel's `desktop-key` - reaches the game (#487).
**Cause:** by design (#487, the owner): a test run must not take the focus or the input of whoever works at the PC. The game shows the window without activation (`WS_EX_NOACTIVATE`) in the corner and `SDL2InputBackend` drops every real mouse and keyboard event; only the events the channel injects (`desktop-mouse`, `desktop-key`, marked as injected) and the simulated controllers pass (`realInput` in `status.json`, `[input] real ... dropped (test run)` in `arx.log`). `@key` posts a window message - real input. In VR the window only mirrors the headset: the keyboard is dropped whatever its origin (`desktopKeyboard`), like the mouse (#338).
**Do:** drive a test run through the channel (`desktop-key`, `desktop-mouse`, `vr-sim`, `vr-point`); test the keyboard flat (`--no-vr`) with `desktop-key`. To try something by hand, launch without the channel (`run-windows.ps1` without `-TestChannel`).

## 2026-10-17 - A channel `sendevent custom <npc> "PARAM"` does nothing
**Symptom:** a test sends an NPC the custom event its script waits for (`sendevent custom human_base_0028 "DEAD"`): no warning in `arx.log`, the command succeeds, and the NPC does nothing - the scene it starts never comes (#29, scene-hybrid T10).
**Cause:** the game lowercases every script when it loads it (`src/script/Script.cpp`, `util::toLowercase(file->read())`): the handler compares `^$PARAM1 == "dead"`. A console or channel line is run as it is typed, so `"DEAD"` never matches. A release build has no script debug log (`LogDebug` is compiled out), so `--debug script` shows nothing.
**Do:** write a custom event's parameter in lowercase (`sendevent custom human_base_0028 "dead"`); to read a script, unpack `data2.pak` with `arxunpak` into a temp folder, never into the repo.

## 2026-10-18 - A CI run's checkout log names a merge of another pull request
**Symptom:** reading a past CI run's diff from its log, the first `HEAD is now at <sha> Merge <head> into <base>` line of the checkout step names a head that is not the run's (`gh run view <id> --json headSha`), and the changed files of `compare/<base>...<head>` belong to another pull request (#522, the impact map's replay first counted 9 of 14 failures as missed this way).
**Cause:** the self-hosted runner keeps its workspace between jobs: the checkout first prints where the workspace stood - the previous job's merge commit - and only its last `HEAD is now at` line is the merge this run built.
**Do:** take the last such line of the checkout step and check that its head is the run's `headSha` (`scripts/dev/impact-replay.ps1` does both).

## 2026-10-20 - A channel line with `;` sent from a script is refused, while the same line works in a `.xrs` file
**Symptom:** `Send-ArxCommand -Lines @('vr-sim @+0-20000ms head rot 60 10 0; vr-sim @+0-20000ms hand right pos 0.3 -0.3 -0.5')` is acknowledged with `errors=1` ("a ramp holds one command", or "expected ..." before #550), and nothing moves; the same text as a `.xrs` line runs both ramps (#550, the `replay-crash` case of `flight-recorder-check.ps1`).
**Cause:** only the `.xrs` runner splits a line at `;` into the lines of one batch (`xrs-lib.ps1`); the channel runs each line of `command.txt` as it is, so the whole text reaches `vr-sim` as one command with a `;` in it.
**Do:** from a script, give `Send-ArxCommand` one line per command (`-Lines @('vr-sim ...', 'vr-sim ...')`): they are one batch, applied in the same frame, as the `.xrs` form is.

## 2026-10-20 - The simulated headset's gameplay timeline starts 0.3 s before the game plays
**Symptom:** a simulator script's frame 0 (`timeline gameplay`) comes while the level still loads: `[vr] sim: timeline starts at frame 2` stands before `Done loading level` in `arx.log`, and a recorded clip played from the timeline's frame 0 ran 270 ms ahead of its recording (#550: 1.7 degrees of head yaw at 6 degrees per second).
**Cause:** the loading screen submits VR frames through `xrWaitFrame` from outside the game loop, and `detectGameState()` says gameplay between the load's synchronous parts, so the timeline starts on a loading screen's frame; the game loop's first playable frame - where a recording's time 0 is - comes some 25 frames later. Sequences wait that out (`@until vr.sim.timelineFrame ge 270`).
**Do:** time a recording and its replay on the clip clock (`vr::replay::clipClockUs`: the display time since the first playable frame of the game loop; the `play` command runs on it), not on the timeline; a script's own timed lines keep the timeline's clock, as every CI sequence expects.

## 2026-10-22 - A second quick click on an in-game button does nothing
**Symptom:** a button of the book (the dev variants panel's `-`, #588) takes the first trigger press and ignores a second one 10 frames later: `dev-variants.xrs` pressed `-` twice and the value moved one step.
**Cause:** the engine turns a click within the double-click time into a double click (`ArxGame.cpp`: `EERIEMouseButton` bit 4 set, bit 1 cleared), so `eeMouseDown1()` is false for it.
**Do:** a button that must count every press checks `eeMouseDown1() || eeMouseDoubleClick1()`.

## 2026-10-22 - After `loadlevel <n>` the eyes are black, and the player's position is the previous level's
**Symptom:** a channel `loadlevel 2` (no target) after `loadlevel 1` loads level 2 with no error, but the shots are `black` and `player.x/y/z` are exactly what they were in level 1; going on that way, `loadlevel 21` after level 20 crashed in level 21's init (#601).
**Cause:** a level change without a target restores the player's saved position (`ARX_CHANGELEVEL_PopLevel`, the `target.empty()` branch), the coordinates of the old level, which in the new one are often outside its geometry. Only a start with `--loadlevel` puts the player at the level's own start (the DLF's `pos_edit`).
**Do:** enter a level as the game does, at an entry marker: `loadlevel <n> <marker>`, the target of another level's exit (`levelstats report` lists them in `level-tour.txt`, #596).

## 2026-10-22 - A scripted launch with `-Save` deleted the replay start save
**Symptom:** after a `run-windows.ps1 -Isolated -Save ...`, `smoke-test.ps1 -Save ...` or `replay.ps1` run, the replay slot of a recorded headset session (`save9001`, D-060) was gone from the user directory and the recording could not be replayed (#569, 2026-10-21).
**Cause:** `Use-ArxSave` (`scripts/build-paths.ps1`) made the library save the only save by removing the whole `save` folder of the user directory, and the replay recorder keeps each session's start save in `save9000`..`save9004` inside it. The game never lists those slots, so nothing showed that they were there.
**Do:** `Use-ArxSave` removes everything in the save folder except the folders `save9000`..`save9004` (`Test-ArxReplaySaveSlot`); `Get-ArxLoadSlotProblem` does not count them for `--loadslot` either. A new script that clears or rewrites a user directory's `save` folder keeps them too, and the check is in `scripts/dev/test-use-arxsave.ps1`.

## 2026-10-22 - A level's init crashes in `FrustrumsClipSphere` (Scene.cpp), a spellcast from a script under it
**Symptom:** `loadlevel 21` (no target, after level 20) ends the game with 0xc0000005 in `FrustrumsClipSphere` <- `VisibleSphere` <- `CCreateField::Update` <- `CreateFieldSpell::Launch` <- `SpellcastCommand` <- `levelInit` (#601); with the player elsewhere the same load passes.
**Cause:** `g_rooms->frustums` (each room's view frustums) is sized and filled by `ARX_SCENE_Update()` of a frame. A level load makes `g_rooms` anew with it empty, and `levelInit` sends `GAME_READY` before the level's first frame: a persistent field cast there with `-z` (restore) fast-forwards with `Update(4s)`, which since #71 tests visibility (`VisibleSphere`). Whenever the sphere passes the distance and the old screen frustum, its room's entry is read from the empty vector. Per-frame updates are safe: `ARX_SPELLS_Update()` runs after `ARX_SCENE_Update()`.
**Do:** code that a script can reach during a level's init must not read per-frame render state (`frustums`, `visibleRooms`, `g_screenFrustum`) as if the level had drawn; `VisibleSphere` treats a room without frustums as not in view, and `getEntityVisibility` (the scripts' `^onscreen` / `^offscreen`) an entity as not in view until the frustums are sized. `level-init-targetless.xrs` covers the spell path.

## 2026-10-22 - `[W] [marker_0495:761] playanim: animation action1 not loaded` after a channel `loadlevel 1`
**Symptom:** the level tour logs the warning 2 s after entering level 1 by `loadlevel 1 marker_0228` from level 0; a start with `--loadlevel 1` or a New game does not (#602).
**Cause:** level 1's new-game intro (camera_0035, marker_0495) runs on the first entry to level 1 because `#intro_played` is still 0. Entered by a level change, `ARX_CHANGELEVEL_Pop_Player` restores the hero's animations from the changelevel save after marker_0495's `INITEND` loaded `ACTION1`, so the slot is empty when `PLAYANIM -np ACTION1` runs. Upstream engine and original data; a player never enters level 1 first by a level change.
**Do:** nothing: the line is in `scripts/dev/log-digest-allowlist.txt`. A test that needs the intro starts a New game (`newgame`) or `--loadlevel 1`.
