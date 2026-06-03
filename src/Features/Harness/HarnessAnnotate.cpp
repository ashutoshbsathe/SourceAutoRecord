#include <cstring>

#include "Entity.hpp"
#include "Event.hpp"
#include "Features/EntityList.hpp"
#include "Features/OverlayRender.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Variable.hpp"

Variable sar_harness_annotate(
    "sar_harness_annotate", "0", 0, 1,
    "Draw wireframe annotation boxes around harness puzzle entities.\n");

// A1: prove the annotation pipeline by boxing every prop_weighted_cube.
// Iterates the server entity list directly (independent of any harness
// session), matching the loop in EntitySnapshotter::Update.
ON_EVENT(RENDER) {
  if (!sar_harness_annotate.GetBool()) return;
  if (!server || !entityList) return;

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;

    auto ent = info->m_pEntity;
    const char* className = server->GetEntityClassName(ent);
    if (!className || std::strcmp(className, "prop_weighted_cube") != 0)
      continue;

    auto se = SE(ent);
    OverlayRender::addBoxMesh(
        se->abs_origin(), se->collision().OBBMins(), se->collision().OBBMaxs(),
        se->abs_angles(), RenderCallback::none,
        RenderCallback::constant({255, 215, 0}, /*nodepth*/ true));
  }
}
