#pragma once
#include "Utils/SDK/Math.hpp"
#include "Utils/SDK/Trace.hpp"

// Shared laser-seat geometry: beam ray, down-trace rest, redirect yaw, line-clear.

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
// extra entities the ray ignores (e.g. player + held cube) so they don't shorten
// the beam.
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
