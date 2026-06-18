#pragma once

// Map-completion oracle. Latches a server-authoritative `chamber_complete` bit
// from an OR-set of exit-input signals seen in the always-on AcceptInput hook,
// so it fires on any map with zero per-map config -- replacing the hand-passed
// --exit/--radius hack. Design + 8-map recon:
// brainstorm/exit_detection_brainstorm.md; build plan:
// brainstorm/exit_detector_impl_plan.md.
//
// This header is intentionally dependency-free (no gRPC/Harness includes) so
// the core AcceptInput hook in Server.cpp can include it cheaply. All matching
// state lives in PuzzleExit.cpp.
namespace PuzzleExit {

// Called from AcceptInput_Hook for every entity input. Latches once on the
// first matching exit signal; a no-op unless the harness owns input.
// entName/className/ inputName are non-null (the hook strcmp's them unguarded);
// param may be empty.
void OnInput(const char* entName, const char* className, const char* inputName,
             const char* param);

// Clear the latch + mask. Call on SESSION_START so completion doesn't bleed
// into the next episode.
void Reset();

// The latched completion bit, read out in InternalObserve as
// GameState.chamber_complete.
bool Get();

// Bitmask of which signal(s) fired (the ExitSignal bits in PuzzleExit.cpp),
// surfaced as GameState.exit_signal_mask for the corpus-sweep telemetry.
int GetMask();

}  // namespace PuzzleExit
