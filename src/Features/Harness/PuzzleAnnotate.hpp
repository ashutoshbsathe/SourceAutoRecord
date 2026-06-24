#pragma once

// True if className is a puzzle class the harness boxes, labels, and marks.
// kClassColors (PuzzleAnnotate.cpp) is the single source of truth; MarkTable
// consults this so the marked set == the annotated set.
bool IsHarnessMarkedClass(const char* className);

// Per-entity form of IsHarnessMarkedClass: also rejects transform-bogus
// entities (a redirected laser's transient (0,0,0) re-emit segments). Every
// mark/annotate gate uses this so the marked set == the annotated set.
bool IsHarnessMarkedEntity(void* ent, const char* className);
