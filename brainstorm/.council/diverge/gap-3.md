# Gap lens: The Omni gap — audio as percept, data product, and eval axis

> Gap-filler pass. No council lens mentioned game audio, yet the vision names **Gemini Omni**
> and Portal 2's audio is semantically load-bearing: turrets announce themselves *before*
> firing ("I see you"), doors/buttons/droppers have distinctive off-screen cues, timed
> pedestal buttons audibly tick, GLaDOS/announcer narration carries (and sometimes lies
> about) task information. Today the harness is deaf, the `.hdem` recorder writes silent
> movies, and the eval has no audio arm.

## Three facts that shape the whole axis (verified in-repo / in-engine)

1. **Waveform capture is already 80% built.** Upstream SAR's renderer
   (`src/Features/Renderer.cpp:969-1159`) hooks `SND_RecordBuffer` via AOB scan and reads
   the engine's final mixed PCM (`g_snd_p`, `g_snd_linear_count`, `g_snd_vol`), forcing
   `snd_surround_speakers 2` for clean stereo. It currently only runs during `sar_render`
   movie capture (the hook fires when the engine's `SND_IsRecording` predicate is true,
   toggled via `g_movieInfo`). Generalizing this into a harness audio tap is assembly,
   not research — and it is **tick-clocked**, not wall-clocked, so it survives
   `host_timescale` and frame-stepped operation where any OS-level tap drifts.
2. **Source is its own audio annotator.** Every game sound goes through the
   soundemitter system under a *script name* (`NPC_FloorTurret.TalkSearch`,
   `Portal.button_down`, …) and Portal 2 ships **closed captions for every voice line
   and most effect sounds** (`portal2/resource/closecaption_english.dat`, community-documented
   binary format; sources in `subtitles_english.txt`). Hooking the emit path gives
   oracle-quality "hearing" as *text* — no ASR, no audio ML, a handful of tokens per event.
3. **The mark namespace already exists.** `MarkTable` (`src/Features/Harness/MarkTable.hpp`)
   maps entities to stable integer marks that the VLM already grounds on visually. A sound
   event that carries the *emitting entity's mark* is cross-modally grounded for free:
   "[heard] turret **7**: 'I see you'" refers to the same `7` painted on the frame.

One more property nobody else's percepts have: **audio is naturally egocentric.** The
mark list leaks through walls (the eval-science lens's Track B complaint); sound comes
with engine-computed attenuation (`soundlevel` dB + distance), so an honest
audible-at-player filter falls out of the physics instead of a hand-written frustum check.

---

## Ideas

### A. Percept layer (semantic first)

#### 1. Audio capability recon: what does the farm actually hear? — **S**
**What:** A half-day test matrix before anything else: per launch mode (gamescope desktop,
headless farm instance, `-nosound`, no PipeWire sink, `mat_norendering` fast-forward),
answer: (a) does server-side `EmitSound` still fire? (b) does the client mixer run /
does `SND_RecordBuffer` fire when forced? (c) what does `snd_mute_losefocus` do under
gamescope? Document as a capability table in `brainstorm/`.
**Why:** This is the load-bearing unknown of the whole axis. Expected result — and the
reason the semantic hook is v0: **server-side sound *events* fire even when no audio
device exists or `-nosound` is set; the *mixer* (waveform) does not.** Confirming that
makes the architecture decision for you.
**Effort:** S (1-2 days).
**Unlocks:** Correct ordering of ideas 2 vs 5; a "hardware requirements" line for the
fleet/throughput story (harness-platform lens).

#### 2. SoundSnapshotter: the audio analog of EntitySnapshotter — **S**
**What:** Hook the soundemitter emit path (server `CBaseEntity::EmitSound` via the
`VSoundEmitter` interface vtable, or AOB on the engine dispatch — same `Offsets` +
`Memory::Scan` + `Hook` pattern Renderer.cpp uses) and buffer per-tick events:
`{tick, sound_name, source mark (via MarkTable), origin, volume, soundlevel, pitch,
channel, flags(start/stop/looping)}`. Dedupe looping sounds into start/stop edges
(funnel hum, fizzler buzz are loops — without dedup they spam every observation).
Surface as `repeated SoundEvent sound_events` on `GameState` (delta since last
observation, like the entity snapshot). `make proto`, smoke-test case in
`py/agentloop_smoke.py` per repo convention.
**Why:** This is THE audio percept: robust to `-nosound`, costs ~zero ticks/sec, and is
the only percept that captures things that happen *between* observations — the current
design reads pixels every N ticks; a turret line fired on tick N+3 is simply gone today.
**Effort:** S (the hook-finding in the 32-bit binary is the only real work; the pattern
is documented in `docs/contributing.md`).
**Unlocks:** Ideas 3, 4, 7, 8, 13; a free replay-verification fingerprint — the
sound-event stream is a compact semi-determinism checksum for the anti-cheat /
trajectory-audit idea in the benchmark lens.

#### 3. Caption + bearing renderer: sound as ~20 text tokens — **S**
**What:** Offline (Python, checked into `py/llm_eval/`): parse the soundscript tree
(`scripts/game_sounds_*.txt`) + `closecaption_english.dat` once into a JSON lookup
`sound_name -> {caption, class}`. At percept-build time render each event as e.g.
`[heard t+12] turret 7 (right, ~6m, no LoS): "I see you"` — bearing from camera yaw vs
event origin, distance bucketed, LoS from the existing annotation tracer. Apply the
attenuation filter: events whose soundlevel/distance make them inaudible at the player
are dropped (the egocentric honesty property).
**Why:** Turns the engine's own metadata into oracle hearing for a *frozen text/vision*
model — no Omni access required, no audio tokens billed. An image is ~250+ tokens;
a heard line is ~20. Audio becomes the **cheapest** percept in the stack.
**Effort:** S.
**Unlocks:** EchoBench text arm (idea 7); the A/B that makes idea 9 a science question;
in-context audio for the O(1)-scratchpad agent (model-frontier lens) at negligible cost.

#### 4. `.hdem` v2: sound-event records before the format freezes lossy — **S**
**What:** Add a sound-event record kind to `HdemFormat.hpp` alongside the planned
per-tick `CUserCmd` records (the v2 the council already converged on): ~20 bytes per
event, a few events/tick worst case. Mirror in `py/hdem_reader.py`. Recorder taps the
same hook as idea 2.
**Why:** The exact CUserCmd argument, re-applied: **every hour of human play recorded
without sound events is lossy forever** — you cannot reconstruct which cues a human
*reacted to* from a silent recording. If the pitch says "data for Gemini Omni," the
flagship human dataset cannot be a silent movie. This must land *with* v2, not in v3.
**Effort:** S — but **schedule-critical**: it rides the v2 format break or waits for v3.
**Unlocks:** Human reaction-to-sound analysis; the Omni data annex (idea 11); audio in
the macro-segmentation / BC pipelines other lenses planned over .hdem v2.

### B. Capture layer (waveform second)

#### 5. Tick-synced waveform tap: generalize the Renderer hook — **S-M**
**What:** Factor `SND_RecordBuffer_Hook`'s PCM extraction out of the movie path into a
`HarnessAudio` tap: force the `SND_IsRecording` predicate true (Renderer already
manipulates `g_movieInfo`) without writing video, and stream `44.1kHz × 2ch × 16-bit`
(~176 KB/s — trivial next to pixels) into (a) a SHM ring buffer with tick-indexed
headers, opt-in like `copy_pixels_to_shm`, and (b) an optional `.wav` sidecar written
by `RolloutRecorder`/`RenderDemo`.
**Why:** Engine-side capture is in the *tick clock*: correct under `host_timescale`,
frame-stepping, and the world-freeze the LLM eval relies on. Any OS tap records
wall-clock silence while the world is frozen and chipmunk audio when fast-forwarded.
This is also what makes archived demos re-renderable *with sound*.
**Effort:** S-M (the audio math is done; the work is the SHM segment + lifecycle +
verifying the predicate trick outside `startmovie`).
**Unlocks:** Ideas 9, 10, 11, 12, 14; audio in the sizzle video for free.

#### 6. PipeWire per-instance tap: the one-day fallback spike — **S**
**What:** In `py/game_launcher.py`: create a per-instance null sink (`p2-audio-{N}`),
point the instance at it via `PULSE_SINK`, capture with `pw-record`. Measure drift,
underruns, and tick-alignment error against the engine tap on the same run.
**Why:** Do it once, to *kill* it with data: the expected memo is "wall-clock capture
drifts and can't survive freeze/timescale; engine tap wins." Keeping the script costs
nothing and is the fallback if the `SND_IsRecording` trick fights back on some engine
build — plus it's the only option that hears OS-level output (e.g., future voice TTS).
**Effort:** S (1 day, timeboxed).
**Unlocks:** Closes the capture-architecture question with evidence instead of taste.

### C. Eval axis

#### 7. EchoBench: audible-but-not-visible chamber suite + three-arm protocol — **M**
**What:** 8-12 hand-authored PeTI chambers where audio carries the discriminating bit:
(a) two visually identical doors out of view — only the door-open sound tells which one
the button opened; (b) off-screen dropper — the dispense sound localizes the cube;
(c) turret behind opaque glass on one of two corridors — its idle/search lines are the
only warning; (d) timed pedestal button — the audible tick is the only timer percept;
(e) fizzler hum marking the death corridor. Run three pre-registered arms over the same
chambers: **deaf** (today's percepts), **transcript** (idea 3 text events), **raw audio**
(idea 9). Headline metric: *audio advantage* = solve-rate delta vs deaf, per cue class.
**Why:** This is the eval-axis product: no embodied benchmark anywhere isolates audio as
the load-bearing modality with engine ground truth. It slots directly into the chamber
manifest / suite machinery the other lenses are already building — audio is one more
tier, not a new benchmark stack.
**Effort:** M (chamber authoring dominates; arms reuse existing eval plumbing).
**Unlocks:** A citable "do agents hear?" finding; the Omni-org pitch slide; new
contamination-resistant chamber families (audio cues are invisible to screenshot-trained
priors).

#### 8. HEARD(event): audio as macro interrupt — **S**
**What:** Extend the locomotion lens's interruptible-macro design with a sound trigger:
`go_to` aborts with `INTERRUPTED_BY_SOUND(mark, sound_class)` when a watched class
(turret deploy/search, fizzler-destroyed-cube, door state change) fires mid-march.
The SoundSnapshotter buffer is already per-tick; the macro loop just checks it.
**Why:** Audio is the only percept that arrives *during* a macro without paying the
~1ms screen-read. A turret saying "Target acquired" three ticks into a 200-tick
`go_to` should end the macro — today the agent learns about it one full step later,
from pixels, possibly while dead. This converts first-light-style step burn into
in-step evidence.
**Effort:** S on top of interruptible macros; M standalone.
**Unlocks:** Turret chambers stop being unfair to the macro altitude; the eval's
failure taxonomy gains an "informed retreat" event (recovery-metric fuel).

#### 9. The Omni arm: native ears vs engine transcripts — **M**
**What:** Slice the last N seconds of the waveform tap into a `.wav` attached to the
step prompt for audio-capable models (Gemini takes audio natively). Pre-registered A/B
on EchoBench: raw-audio arm vs transcript arm vs both. Token/cost accounting per arm
(audio tokens are not free — that's part of the result).
**Why:** A real science question with pitch value *either way*: if transcripts win,
semantic sound events are the product and "Omni-readiness" is about data, not percepts;
if native audio wins (e.g., on localization, overlapping sounds, tone), then this
harness is the only embodied eval that can feed it synchronized ears. Registered
prediction (see spiciest take): transcripts win in 2026.
**Effort:** M (client plumbing + arm runner; depends on 5).
**Unlocks:** The first frozen-Omni embodied result; calibration data for idea 14.

#### 10. Viewer audio lane + synced playback — **S**
**What:** Add a sound-event lane to the trajectory/rollout HTML viewers (events as
labeled pips on the step timeline, caption text on hover) and, where a `.wav` sidecar
exists, an `<audio>` element synced to the scrubber.
**Why:** Cheap, and it compounds: the labeling-tool plan (data-flywheel lens) can now
attribute failures to *missed audio*; the sizzle video / Twitch-stream ideas get real
game audio; debugging EchoBench runs becomes possible at all.
**Effort:** S.
**Unlocks:** "[heard]" steps in the failure museum; demo polish across every
pitch-strategy artifact.

#### 11. Sound-localization micro-probe (stateless, no game needed) — **S**
**What:** From archived waveform clips + engine ground truth: "a turret is beeping —
which direction?" graded as bearing error against the known emitter origin. Source's
stereo mix pans by listener yaw, so the signal is genuinely in the audio. Run across
audio-capable models as a stateless probe battery (the audio sibling of the
perception-VQA suite in the eval-science lens).
**Why:** Spatial hearing in multimodal models is measured *nowhere*; every clip is
auto-labeled by the engine; the probe runs offline from archives at API-only cost.
"GPT-5 can't tell left from right" is a headline finding for a week of work.
**Effort:** S (given 5).
**Unlocks:** The Omni-pitch hook; ground-truth audio-grounding data for fine-tuning.

#### 12. GLaDOS the unreliable narrator: audio instruction injection — **S-M**
**What:** The audio twin of the moonshot lens's signage prompt-injection: chambers play
scripted VO (console `play`/choreo triggers — PeTI maps already fire announcer lines)
whose narration is sometimes helpful, sometimes irrelevant, sometimes adversarially
wrong ("the cube is behind you" — it is not). Since the percept arrives via the caption
channel (idea 3), no audio model is even required for v0. Score: does the agent weight
world evidence over narration? It is *literally GLaDOS's character*, so the eval is
diegetic rather than contrived.
**Why:** Embodied prompt-injection robustness has no 3D testbed; the narration channel
is the natural attack surface and costs almost nothing once captions are percepts.
**Effort:** S-M (VO triggering in chambers is the only new machinery).
**Unlocks:** A safety-flavored result for a different audience in the same org; trap
chambers for the done-precision battery (a lying "chamber complete" announcement).

### D. Data product

#### 13. The Omni data annex: four-channel time-aligned RLDS export — **M**
**What:** Extend the planned RLDS/SIMA-span exporters so each trajectory carries
**video + audio (wav) + per-tick actions + ground-truth state + caption-grade sound-event
text**, all on one tick clock. The auto-annotation engine (data-flywheel lens) gains
audio events as span boundaries and span text ("turret spotted the player — it called
out — player retreated").
**Why:** Name the asset precisely in the pitch: YouTube-scale video has audio but no
state or actions; robot teleop has state and actions but almost never semantically
annotated audio; sim data is usually silent. **Nobody has all four time-aligned.**
Audio-conditioned robotics is an emerging line (contact-audio manipulation à la
ManiWAV); first-person audio+action data is exactly what an Omni-class embodied model
trains on. This annex is what makes "trajectories for Gemini Omni" a sentence with
content instead of a vibe.
**Effort:** M (given 4 + 5; exporter work plus span integration).
**Unlocks:** The strongest version of the data-engine slide; a falsifiable transfer
experiment (does audio-channel data move any audio-conditioned policy benchmark?).

#### 14. Audio as free action labels for the YouTube harvest — **M**
**What:** Portal-gun shots (distinct blue/orange fire sounds), jumps, footstep cadence,
cube pickup/drop, portal-traversal whoosh are loud, distinctive one-shots. Train a tiny
onset/classification detector on **infinite engine-labeled audio** (render archived
demos through the waveform tap; the sound-event stream is the label track), then run it
over the *audio* of 15 years of YouTube Portal 2 to pseudo-label tick-precise `+attack`
and traversal events — seeding and cross-checking the IDM the VPT-style harvest needs.
**Why:** Audio is the cheapest inverse-dynamics channel: a portal shot is a ~100ms
unmistakable acoustic event, far easier than inferring clicks from 30fps pixels. The
detector's training set costs zero human labels because the engine emits ground truth.
**Effort:** M for detector + alignment on rendered demos (the YouTube pipeline itself
is the L owned by the data-flywheel lens).
**Unlocks:** Higher-precision IDM labels; a verification signal for video-only labels;
another "the engine annotates for free" proof point.

#### 15. Soundscape hygiene knobs: SNR as an eval dial and dataset config — **S**
**What:** A documented cvar preset (`snd_musicvolume 0`, ambient soundscape control,
per-channel volume) exposed through the harness config: **clean mode** (events/VO only)
for transcripts and detector training, **realistic mode** (music + ambience) for
EchoBench-hard and dataset realism. Record which mode every artifact was captured in
(manifest field).
**Why:** Music and ambient loops are noise for every consumer above — but *removing*
them silently would make the dataset unrealistic and the eval easy. Making SNR an
explicit, versioned dial turns a confound into an axis (the audio analog of the
annotation-ablation battery).
**Effort:** S.
**Unlocks:** Reproducible audio configs across eval/data; a difficulty knob for
EchoBench tiers.

---

## Sequencing at one-RE + agentic-coding staffing

1. **Week 0:** idea 1 (recon) — answers events-vs-mixer per launch mode.
2. **Week 1-2:** ideas 2 + 3 (semantic hook + caption percept) and idea 4 riding the
   `.hdem` v2 break — the format deadline is the only hard ordering constraint here.
3. **Next:** idea 5 (waveform tap, reusing Renderer's hook), idea 10 (viewer lane).
4. **Then the science:** idea 7 (EchoBench) with arms from 3 and 9; ideas 8, 11, 12 as
   S-sized add-ons.
5. **Data annex (13, 14, 15)** lands when the exporter/harvest work from the
   data-flywheel lens lands; audio rides those PRs rather than duplicating them.

The axis deliberately adds **no new infrastructure category**: it is one engine hook,
one record type, one SHM segment, and chamber/eval/exporter additions to machinery
nine other lenses already justified.

## Spiciest take

**Every `.hdem` recorded today is a silent movie being pitched to an audio-first model
org — and the fix is not audio ML, because Source is its own audio annotator.** Every
load-bearing sound already carries a script name and a shipped closed caption, so
hooking `EmitSound` gives oracle hearing as ~20 text tokens per event — the cheapest
percept in the stack, and the only one that arrives *between* observations and *during*
macros without paying the screen-read tax. Build the semantic hook and get it into
`.hdem` v2 before the format freezes lossy; the raw waveform tap is already 80% built
in `Renderer.cpp`'s `SND_RecordBuffer` hook and exists for exactly two purposes: the
Omni A/B arm and the four-channel data product. Registered prediction: **engine
transcripts beat native audio for frozen models in 2026** — and that negative result
about Omni-class ears, measured in the only harness that can produce synchronized
ground-truth audio, is itself a citable finding and a sharper DeepMind pitch than
"we added sound."
