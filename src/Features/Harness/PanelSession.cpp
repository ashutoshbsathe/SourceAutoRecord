#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "BspFilePanelSource.hpp"
#include "Command.hpp"
#include "Entity.hpp"
#include "Event.hpp"
#include "Features/EntityList.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "SurfaceMarkTable.hpp"
#include "Utils/Math.hpp"

// Enumerates panels by parsing the map's .bsp at session start.
static BspFilePanelSource g_panelSource;

// Brush-entity surfaces at rest for the current map, marks assigned after the
// static set (keyed by targetname, so they survive deploy/retract); posed
// against their live entities every frame.
static std::vector<DynamicPanelRest> g_dynRests;
static std::vector<int> g_dynMarks;
static std::unordered_set<std::string> g_dynNames;

ON_EVENT(SESSION_START) {
  std::string map = engine->GetCurrentMapName();
  surfaceMarkTable.RebuildFromSource(g_panelSource, map);
  g_dynRests = g_panelSource.EnumerateDynamicRests(map);

  int mark = 0;
  for (const auto& p : surfaceMarkTable.Panels()) mark = std::max(mark, p.mark);
  g_dynMarks.clear();
  g_dynNames.clear();
  for (const auto& r : g_dynRests) {
    g_dynMarks.push_back(++mark);
    g_dynNames.insert(r.targetname);
  }
}

// The engine owns each panel's hinge/door animation, so the posed rect is
// abs_origin + R(abs_angles) * local corner. Corners are re-rectified under
// the world-frame plane basis so their ordering matches the static panels'.
ON_EVENT(RENDER) {
  if (g_dynRests.empty() || !server || !entityList) return;

  std::unordered_map<std::string, std::pair<Vector, QAngle>> live;
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    const char* nm = server->GetEntityName(info->m_pEntity);
    if (!nm || !g_dynNames.count(nm)) continue;
    auto se = SE(info->m_pEntity);
    live[nm] = {se->abs_origin(), se->abs_angles()};
  }

  std::vector<PanelDesc> posed;
  for (size_t k = 0; k < g_dynRests.size(); ++k) {
    const auto& r = g_dynRests[k];
    auto it = live.find(r.targetname);
    if (it == live.end()) continue;

    auto rot = Math::AngleMatrix(it->second.second);
    Vector n = rot * r.normal;
    Vector u, v;
    PlaneAxes(n, &u, &v);
    float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f, dist = 0;
    for (int c = 0; c < 4; ++c) {
      Vector w = it->second.first + rot * r.corners[c];
      umin = std::min(umin, w.Dot(u));
      umax = std::max(umax, w.Dot(u));
      vmin = std::min(vmin, w.Dot(v));
      vmax = std::max(vmax, w.Dot(v));
      if (c == 0) dist = w.Dot(n);
    }
    auto pp = [&](float uc, float vc) { return u * uc + v * vc + n * dist; };

    PanelDesc d;
    d.mark = g_dynMarks[k];
    d.planeNormal = n;
    d.corners[0] = pp(umin, vmin);
    d.corners[1] = pp(umax, vmin);
    d.corners[2] = pp(umax, vmax);
    d.corners[3] = pp(umin, vmax);
    d.center = pp((umin + umax) * 0.5f, (vmin + vmax) * 0.5f);
    d.mins = d.maxs = d.corners[0];
    for (int c = 1; c < 4; ++c) {
      d.mins.x = std::min(d.mins.x, d.corners[c].x);
      d.mins.y = std::min(d.mins.y, d.corners[c].y);
      d.mins.z = std::min(d.mins.z, d.corners[c].z);
      d.maxs.x = std::max(d.maxs.x, d.corners[c].x);
      d.maxs.y = std::max(d.maxs.y, d.corners[c].y);
      d.maxs.z = std::max(d.maxs.z, d.corners[c].z);
    }
    d.anchorFlags = 0;
    posed.push_back(d);
  }
  surfaceMarkTable.SetDynamic(std::move(posed));
}

CON_COMMAND(
    sar_harness_panels_dump,
    "sar_harness_panels_dump - list the loaded portal-surface panels.\n") {
  auto panels = surfaceMarkTable.Panels();
  for (const auto& p : panels) {
    console->Msg(
        "  S%d  center %.1f %.1f %.1f  normal %.2f %.2f %.2f  c0 (%.0f %.0f "
        "%.0f) c2 (%.0f %.0f %.0f)\n",
        p.mark, p.center.x, p.center.y, p.center.z, p.planeNormal.x,
        p.planeNormal.y, p.planeNormal.z, p.corners[0].x, p.corners[0].y,
        p.corners[0].z, p.corners[2].x, p.corners[2].y, p.corners[2].z);
  }
  console->Print("panels: %d\n", (int)panels.size());
}
