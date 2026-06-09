# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this fork is

Upstream **SourceAutoRecord (SAR)** is a 32-bit C++ plugin (`sar.so` / `sar.dll`) for Portal 2 and Source-engine mods, used by speedrunners. This fork keeps all of that and adds a **reinforcement-learning stack** on top: the C++ side exposes the running game as a gRPC environment (the "Harness"), and the Python side (`py/`) is a JAX PPO + ViT agent plus demo/rollout tooling. Most active work lives in `src/Features/Harness/` and `py/` — the rest of `src/` is largely upstream SAR.

## Build & tooling

The plugin is **32-bit** (`-m32`). It links a self-contained 32-bit gRPC/protobuf/abseil runtime installed at `/opt/p2-grpc32` (build instructions in `src/Features/Harness/README.md`). The whole runtime is statically shipped inside `sar.so`.

```sh
make                 # build sar.so (Linux). Needs g++ multilib + /opt/p2-grpc32
make clean
make proto           # regenerate BOTH C++ and Python protobuf stubs from harness.proto
make proto_cpp       # C++ stubs only  -> src/Features/Harness/*.pb.cpp
make proto_py        # Python stubs only -> py/p2harness/harness_pb2*.py
./format.sh          # clang-format (google) on Harness src + ruff format on py/
```

- `config.mk` (git-ignored) overrides Make paths locally; don't commit configured paths.
- Windows builds use `SourceAutoRecord.sln` / MSVC v142; this repo is developed on Linux.
- Python env is managed by **uv** (`pyproject.toml`, `uv.lock`, `requires-python >=3.14`). Run scripts with `uv run python py/...`. Lint/format is **ruff**, Google docstring convention, single quotes, one import per line.
- After editing `harness.proto` you must run `make proto` and rebuild — the C++ and Python stubs are checked in and must stay in sync.

## Running the RL system

The Python trainer launches its own headless game instances; you do not start the game by hand.

```sh
./rl_challenge_1.sh                  # full PPO run: train_rl_challenge.py with map + target_pos
./launch_multiple.sh                 # spawn N gamescope game instances manually (debugging)
uv run python py/vis/visualize.py X.rollout   # NiceGUI browser viewer for a rollout
uv run python py/render_demos.py ...          # batch-render .dem demos -> rollouts via Harness
```

Each game instance is keyed by `+sar_harness_instance N`: it listens on gRPC port `50000+N` and uses a per-instance shared-memory framebuffer suffix `_N`. `py/game_launcher.py` owns the gamescope/steam-runtime launch incantation and the per-instance port math (`tv_port 47000+N`, etc.). Running the game needs `gamescope`.

> Per user instruction: do **not** kick off long/expensive runs (training loops, full renders, game launches) from the agent terminal — describe the command and let the user run it. `make`, `make proto`, `./format.sh`, and lint are fine to run.

## Architecture: the C++ <-> Python bridge

This is the part that requires reading multiple files to understand.

**Harness (gRPC server, in the game process).** `src/Features/Harness/Harness.{hpp,cpp}` registers a SAR `Feature` (added in `src/SAR.cpp` as `AddFeature<Harness>(&harness)`) and runs a gRPC server. `Portal2HarnessImpl.cpp` implements the service defined in `harness.proto`:
- `Observe` / `Act` / `Reset` — single-step RPCs.
- `AgentLoop` — bidirectional stream that is the hot path for RL (one `ActionRequest` in, one `GameState` out per step).
- `RenderDemo` — play a `.dem` end-to-end and emit a rollout.

Game state (position, velocity, camera, health, plus a delta-compressed `EntitySnapshot`) flows out; actions (movement keys, portal fire, analog mouse delta, `num_ticks`) flow in. Tick synchronization between an `Act()` call and the game's `PRE_TICK` is done with the mutex/condvar/atomics in `Harness.hpp` (`ticksRemaining`, `tickCV`, `harnessControlActive`).

**Pixels.** Framebuffers are *not* sent over gRPC. The server copies them into POSIX shared memory (`HarnessShm`), and the Python client maps that SHM (`py/p2harness/harness.py`). `copy_pixels_to_shm` on `AgentMessage` is opt-in per tick because the screen read is expensive (~1ms, dispatched to the engine main thread) — visual RL requests pixels every N ticks, not every tick.

**Python client.** `py/p2harness/P2Harness` wraps every RPC and runs `AgentLoop` on a background thread with queues. `py/rl_challenge_env.py` (`Portal2Env`) wraps that as a Gymnasium env (224×224 vision + position obs, sparse/progress reward). `py/train_rl_challenge.py` is the orchestrator; the algorithm lives in `py/rl/` (`config.py`, `model.py` ViT+ActorCritic, `ppo.py`, `rollout.py`, `inference_server.py`, `async_worker.py`, `checkpoint.py`). The ViT is frozen and rollouts store 768-dim embeddings, not raw pixels, to save memory.

## Data formats (game data flows three ways)

- **`.dem`** — native Source demo files (upstream SAR records/plays these).
- **`.hdem`** — "harness demo", a custom binary sidecar (`src/Features/Harness/HdemFormat.hpp`; magic `"HDEM"`, versioned, typed entity fields). Written by `HdemRecorder.cpp` alongside a `.dem`, read by `HdemReader.cpp`. It captures per-tick entity snapshots that the native demo doesn't. Python mirror: `py/hdem_reader.py`, `py/hdem_dump.py`.
- **`.rollout`** — length-delimited protobuf (`RolloutHeader` + `RolloutStep`, defined in `harness.proto`) that the RL/visualizer stack consumes. Produced by `RolloutRecorder.cpp` / `RenderDemo`, or from an `.hdem` via `py/hdem_to_rollout.py`. Validate with `py/validate_rollout.py`.

`EntitySnapshotter.cpp` walks the server entity list each tick and is the source of the entity-state data shared by `Observe`, the `.hdem` recorder, and rollouts. It is performance-sensitive (see `brainstorm/entity_snapshotter_redesign.md`, `brainstorm/phase4_fixing_slowness_and_crashes.md`).

## Upstream SAR structure (mostly unchanged)

- `src/Features/` — self-contained `Feature` subclasses (HUD, timer, renderer, demo tools…). `src/Modules/` — wrappers over engine interfaces (`Engine`, `Server`, `Client`, `Console`…). `src/Games/` — per-title support (offsets, versions, categories) for Portal 2, Aperture Tag, INFRA, etc.
- Engine integration is via interface lookup, signature/AOB scanning, and function hooking. `docs/contributing.md` documents the patterns (Interfaces, Offsets, Hooking, Features, HUD elements, console commands, game support).
- Console commands/variables are the user-facing API; reference list in `docs/cvars.md`. New ones are declared with the `Variable`/`Command` helpers (`src/Variable.hpp`, `src/Command.hpp`).

## Conventions

- C++: C++17, Google clang-format (`.clang-format`), formatted via `format.sh` (only Harness sources — never the generated `*.pb.cpp`/`*.grpc.pb.*`).
- Design notes for non-trivial Harness/RL work are kept as markdown in `brainstorm/`; read the relevant one before changing entity snapshotting, hdem, or the rollout visualizer.

## LLM percept/act harness (annotated-env reasoning eval)

> **Start with [`brainstorm/ROADMAP.md`](brainstorm/ROADMAP.md)** — the single source of truth for project state, milestones, and design decisions; it indexes every other `brainstorm/` doc. Read it before any harness/RL/percept work.

A frozen-LLM ReAct eval on annotated chambers, designed in `brainstorm/llm_percept_act_grammar.md` + `llm_percept_act_phased_plan.md` (Track A in-engine annotation A1–A5 is built; `HarnessAnnotate.cpp`).

- **v0 element scope = stock Portal 2 Puzzle Maker (PeTI) elements** an average researcher can place out of the box — see `brainstorm/puzzlemaker_elements.md`. Custom/BEEmod/Hammer elements (e.g. Sendificate) are **P1**, out of v0.
- Per-class **status fields** (button pressed / door open / catcher powered / turret alive) are mapped via the `sar_harness_dump_fields` recon command; protocol + results in `brainstorm/status_field_recon.md`.
