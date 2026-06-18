#pragma once

// True if className is a puzzle class the harness boxes, labels, and marks.
// kClassColors (PuzzleAnnotate.cpp) is the single source of truth; MarkTable
// consults this so the marked set == the annotated set.
bool IsHarnessMarkedClass(const char* className);
