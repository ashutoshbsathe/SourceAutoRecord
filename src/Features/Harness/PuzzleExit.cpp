#include "PuzzleExit.hpp"

#include <strings.h>  // strcasecmp

#include <atomic>
#include <cstring>

#include "Event.hpp"
#include "Harness.hpp"

namespace {

std::atomic<bool> g_complete{false};
std::atomic<int> g_mask{0};

enum ExitSignal {
  SIG_PTI_LEVEL_END = 1 << 0,  // @relay_pti_level_end.Trigger
  SIG_TRANSITION = 1 << 1,     // @transition_from_map.Trigger
  SIG_READY = 1 << 2,          // RunScriptCode(ReadyForTransition)
  SIG_LEVELEND = 1 << 3,       // OnLevelEnd
  SIG_CHANGELEVEL = 1 << 4,    // ChangeLevel / ChangeLevelPostFade
  SIG_EXIT_AIRLOCK = 1 << 5,   // @exit_airlock_door.Open -- solved AND walked out
                               // into the exit corridor (relay_leaving_level),
                               // before the worldportal ride to the elevator.
};

}  // namespace

// Input names matched case-insensitively, entity names exactly. Latch-once.
void PuzzleExit::OnInput(const char* entName, const char* className,
                         const char* inputName, const char* param) {
  (void)className;
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
  else if (!strcasecmp(inputName, "Open") &&
           !strcmp(entName, "@exit_airlock_door"))
    sig = SIG_EXIT_AIRLOCK;
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

ON_EVENT(SESSION_START) { PuzzleExit::Reset(); }
