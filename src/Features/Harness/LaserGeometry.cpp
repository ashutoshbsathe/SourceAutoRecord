#include "LaserGeometry.hpp"

#include "Entity.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Utils/Math.hpp"
#include "Utils/SDK/Class.hpp"
#include "Utils/SDK/Trace.hpp"

namespace {
constexpr float kBeamMax = 16384.0f;      // forward-ray max reach
constexpr float kRestProbeUp = 64.0f;     // down-trace start above P
constexpr float kRestProbeDist = 256.0f;  // ... and how far down to look
}  // namespace

bool ComputeBeamSegment(void* emitter, Vector* E, Vector* fwd, Vector* hit,
                        float* length) {
  *E = SE(emitter)->abs_origin();
  QAngle ea = SE(emitter)->abs_angles();
  Math::AngleVectors(ea, fwd);
  CTraceFilterSimple filter;
  filter.SetPassEntity(emitter);
  CGameTrace tr;
  if (!engine->Trace(*E, ea, kBeamMax, MASK_OPAQUE, filter, tr)) {
    *hit = *E + *fwd * kBeamMax;
    *length = kBeamMax;
    return false;
  }
  *hit = tr.endpos;
  *length = (*hit - *E).Length();
  return true;
}

float LaserLineClear(const Vector& from, const Vector& to, void* skipA,
                     void* skipB) {
  Vector d = to - from;
  Ray_t ray;
  ray.m_IsRay = true;
  ray.m_IsSwept = true;
  ray.m_Start = VectorAligned(from.x, from.y, from.z);
  ray.m_Delta = VectorAligned(d.x, d.y, d.z);
  ray.m_StartOffset = VectorAligned();
  ray.m_Extents = VectorAligned();
  SkipTwoEntities filter;
  filter.a = skipA;
  filter.b = skipB;
  CGameTrace tr;
  engine->TraceRay(engine->engineTrace->ThisPtr(), ray, MASK_OPAQUE, &filter,
                   &tr);
  return tr.fraction;
}

bool DownTraceRest(const Vector& P, void* object, Vector* seatOrigin) {
  ICollideable& cc = SE(object)->collision();
  float halfH = (cc.OBBMaxs().z - cc.OBBMins().z) * 0.5f;
  Vector dStart{P.x, P.y, P.z + kRestProbeUp};
  QAngle down{90, 0, 0};
  CTraceFilterSimple filter;
  filter.SetPassEntity(object);
  CGameTrace tr;
  if (!engine->Trace(dStart, down, kRestProbeDist, MASK_PLAYERSOLID, filter,
                     tr))
    return false;
  *seatOrigin = Vector{P.x, P.y, tr.endpos.z + halfH};
  return true;
}

QAngle ComputeRedirectYaw(const Vector& seat, const Vector& target) {
  Vector aim = target - seat;
  Math::VectorNormalize(aim);
  Vector up{0, 0, 1};
  QAngle ang{0, 0, 0};
  Math::VectorAngles(aim, up, &ang);
  ang.z = 0;
  return ang;
}
