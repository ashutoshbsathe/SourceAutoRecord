#pragma once

// Latches a map-completion bit from exit signals seen in the AcceptInput hook.
// Dependency-free so Server.cpp can include it cheaply.
namespace PuzzleExit {

// Called for every entity input; latches once on a matching exit signal.
void OnInput(const char* entName, const char* className, const char* inputName,
             const char* param);

void Reset();   // clear the latch (on SESSION_START)
bool Get();     // latched completion bit
int GetMask();  // which signal(s) fired

}  // namespace PuzzleExit
