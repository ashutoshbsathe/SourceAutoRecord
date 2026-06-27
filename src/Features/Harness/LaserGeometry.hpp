#pragma once
#include "Utils/SDK/Math.hpp"
#include "Utils/SDK/Trace.hpp"

// Shared laser-seat geometry: one home for the beam ray, the down-trace rest,
// the redirect yaw, and the line-clear, called by both the recon commands
// (PuzzleAnnotate) and the interpose verb (MacroExecutor) so they never drift.

// Trace filter skipping two entities, so a ray's own endpoints don't self-stop
// it at fraction 0.
class SkipTwoEntities : public CTraceFilter {
 public:
  const void* a = nullptr;
  const void* b = nullptr;
  bool ShouldHitEntity(void* e, int) override { return e != a && e != b; }
};

// Forward beam ray: emitter -> first opaque world/prop hit (MASK_OPAQUE).
// E/fwd/hit/length filled; false if nothing is hit within range. skipA/skipB are
// extra entities the ray ignores -- the player + held cube, so a player standing
// in the beam doesn't shorten it and pull an interpose seat back to the emitter.
// The authoritative interception is a re-trace after a cube seats, never this ray.
bool ComputeBeamSegment(void* emitter, Vector* E, Vector* fwd, Vector* hit,
                        float* length, void* skipA = nullptr,
                        void* skipB = nullptr);

// Fraction [0,1] of from->to unobstructed by world geometry (MASK_OPAQUE),
// skipping skipA + skipB. 1.0 = clear.
float LaserLineClear(const Vector& from, const Vector& to, void* skipA,
                     void* skipB);

// Down-trace from above P and rest `object` (its own half-height) on the floor.
// seatOrigin = the resting centre pose; false on a miss (pit/void).
bool DownTraceRest(const Vector& P, void* object, Vector* seatOrigin);

// Flat (yaw-only) angles aiming `object`'s local +X redirect axis from `seat`
// at `target` (pitch/roll zeroed).
QAngle ComputeRedirectYaw(const Vector& seat, const Vector& target);
