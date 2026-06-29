#pragma once

// True if className is a puzzle class the harness boxes, labels, and marks.
bool IsHarnessMarkedClass(const char* className);

// Per-entity form of IsHarnessMarkedClass: also rejects transform-bogus
// entities (a redirected laser's transient (0,0,0) re-emit segments).
bool IsHarnessMarkedEntity(void* ent, const char* className);
