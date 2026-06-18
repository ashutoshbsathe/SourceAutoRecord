#include "PuzzleExit.hpp"

#include <strings.h>  // strcasecmp

#include <atomic>
#include <cstring>

#include "Harness.hpp"  // harness->harnessControlActive

namespace {

// The latched bit + which signals contributed. Written from the server-thread
// AcceptInput hook, read from the gRPC thread in InternalObserve -> atomic.
std::atomic<bool> g_complete{false};
std::atomic<int> g_mask{0};

// Exit-signal OR-set, in rough universality order (the §2.8 recon matrix in
// exit_detection_brainstorm.md: every one of 8 maps hit >=2 of these). These
// bit values are the contract behind GameState.exit_signal_mask.
enum ExitSignal {
  // @relay_pti_level_end.Trigger -- every PeTI map, standard + custom.
  SIG_PTI_LEVEL_END = 1 << 0,
  // @transition_from_map.Trigger -- standard PeTI + campaign (SAR also matches
  // this at Server.cpp:640).
  SIG_TRANSITION = 1 << 1,
  // RunScriptCode(ReadyForTransition) -- all departure-elevator maps.
  SIG_READY = 1 << 2,
  // OnLevelEnd -- entName is blank here, so match on the input name only.
  SIG_LEVELEND = 1 << 3,
  // ChangeLevel / ChangeLevelPostFade -- custom + campaign; absent on std PeTI.
  SIG_CHANGELEVEL = 1 << 4,
};

}  // namespace

// Input names are matched case-insensitively: campaign fires `Changelevel`,
// custom PeTI fires `ChangeLevel` (§2.6). Entity names use exact strcmp -- they
// are `@`-prefixed PeTI-compiler-stable names. Latch-once: several keys repeat-
// fire on the standalone-load hang, so the first match wins and the rest no-op.
void PuzzleExit::OnInput(const char* entName, const char* className,
                         const char* inputName, const char* param) {
  (void)className;  // reserved: a future tightening could gate OnLevelEnd on
                    // portal_stats_controller
  if (!harness || !harness->harnessControlActive.load()) return;
  if (g_complete.load()) return;

  int sig = 0;
  if (!strcasecmp(inputName, "Trigger") &&
      !strcmp(entName, "@relay_pti_level_end"))
    sig = SIG_PTI_LEVEL_END;
  else if (!strcasecmp(inputName, "Trigger") &&
           !strcmp(entName, "@transition_from_map"))
    sig = SIG_TRANSITION;
  else if (!strcasecmp(inputName, "RunScriptCode") && param &&
           strstr(param, "ReadyForTransition"))
    sig = SIG_READY;
  else if (!strcasecmp(inputName, "OnLevelEnd"))
    sig = SIG_LEVELEND;
  else if (!strcasecmp(inputName, "ChangeLevel") ||
           !strcasecmp(inputName, "ChangeLevelPostFade"))
    sig = SIG_CHANGELEVEL;
  if (!sig) return;

  g_mask.fetch_or(sig);
  g_complete.store(true);
}

void PuzzleExit::Reset() {
  g_complete.store(false);
  g_mask.store(0);
}

bool PuzzleExit::Get() { return g_complete.load(); }

int PuzzleExit::GetMask() { return g_mask.load(); }
