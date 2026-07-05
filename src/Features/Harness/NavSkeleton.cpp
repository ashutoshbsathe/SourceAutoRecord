#include "NavSkeleton.hpp"

#include <cmath>

#include "BspFilePanelSource.hpp"
#include "Command.hpp"
#include "Entity.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Utils/SDK/EntityEdict.hpp"
#include "Utils/SDK/Trace.hpp"

void NavSkeleton::Build(const std::string& mapName) {
  surfaces_.clear();
  edges_.clear();
  gates_.clear();
  uint32_t id = 0;
  for (const FloorSurface& fs : EnumerateFloorSurfaces(mapName)) {
    Surface s;
    s.id = id++;
    s.normal = fs.normal;
    s.z = fs.z;
    s.mins = fs.mins;
    s.maxs = fs.maxs;
    s.poly = {fs.corners[0], fs.corners[1], fs.corners[2], fs.corners[3]};
    surfaces_.push_back(std::move(s));
  }
  // TODO: banded/gated edges + dynamic brush surfaces.
}

NavSkeleton::PlanResult NavSkeleton::Plan(const Vector& start,
                                          const Vector& target) {
  (void)start;
  PlanResult r;
  r.target = target;
  // TODO: global surface A* + local within-surface routing.
  return r;
}

// Parse the current map's floor surfaces and print each surface's z + XY
// bounds, then the BSP-z vs down-trace delta at the player's feet -- the check
// that BSP floor z is where the body actually stands. Read-only.
CON_COMMAND(sar_harness_nav_dump,
            "sar_harness_nav_dump - list the go_to floor surfaces (z + bounds) "
            "and the BSP-z vs down-trace delta at the player's feet.\n") {
  if (!engine) {
    console->Print("nav_dump: no engine.\n");
    return;
  }
  NavSkeleton nav;
  nav.Build(engine->GetCurrentMapName());
  const std::vector<NavSkeleton::Surface>& surfaces = nav.Surfaces();
  console->Print("nav_dump: %d floor surfaces\n", (int)surfaces.size());
  for (const NavSkeleton::Surface& s : surfaces)
    console->Msg("    #%u z=%.0f  x[%.0f..%.0f] y[%.0f..%.0f]\n", s.id, s.z,
                 s.mins.x, s.maxs.x, s.mins.y, s.maxs.y);

  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl) {
    console->Print("    (no player; load a map for the floorZ delta)\n");
    return;
  }
  Vector feet = pl->abs_origin();
  CTraceFilterSimple filter;
  filter.SetPassEntity(pl);
  Vector top{feet.x, feet.y, feet.z + 40.0f};
  QAngle down{90, 0, 0};
  CGameTrace tr;
  if (!engine->Trace(top, down, 200.0f, MASK_PLAYERSOLID, filter, tr)) {
    console->Print(
        "    feet %.0f %.0f %.0f: no floor under a 200u down-trace\n", feet.x,
        feet.y, feet.z);
    return;
  }
  float traceZ = tr.endpos.z;

  const NavSkeleton::Surface* best = nullptr;
  float bestDz = 1e30f;
  for (const NavSkeleton::Surface& s : surfaces) {
    if (feet.x < s.mins.x || feet.x > s.maxs.x || feet.y < s.mins.y ||
        feet.y > s.maxs.y)
      continue;
    float dz = std::fabs(s.z - traceZ);
    if (dz < bestDz) {
      bestDz = dz;
      best = &s;
    }
  }
  if (!best) {
    console->Print(
        "    feet %.0f %.0f %.0f: trace floor z=%.1f, NO enumerated surface "
        "covers the feet\n",
        feet.x, feet.y, feet.z, traceZ);
    return;
  }
  console->Print(
      "    feet %.0f %.0f %.0f: trace z=%.1f  surface #%u z=%.1f  delta=%.1f\n",
      feet.x, feet.y, feet.z, traceZ, best->id, best->z, best->z - traceZ);
}
