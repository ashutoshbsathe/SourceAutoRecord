#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Entity.hpp"
#include "Event.hpp"
#include "Features/OverlayRender.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "NavSkeleton.hpp"
#include "Variable.hpp"

Variable sar_harness_nav_draw(
    "sar_harness_nav_draw", "0", 0, 1,
    "Draw the go_to nav flood: cell carpet colored by cluster, cluster edges "
    "as typed lines. Rebuilds on map change or when toggled back on.\n");
Variable sar_harness_nav_draw_cells(
    "sar_harness_nav_draw_cells", "1024", -1, 100000,
    "Cell draw radius around the player (units); -1 or 0 = whole map.\n");
Variable sar_harness_nav_draw_drops("sar_harness_nav_draw_drops", "0", 0, 1,
                                    "Include DROP edges in the nav draw.\n");
Variable sar_harness_nav_draw_labels("sar_harness_nav_draw_labels", "0", 0, 1,
                                     "Label each cluster with its C#id.\n");
Variable sar_harness_nav_draw_xray(
    "sar_harness_nav_draw_xray", "0", 0, 1,
    "Draw the nav flood with no depth test (through walls).\n");

namespace {
NavSkeleton g_nav;
std::string g_navMap;

constexpr int kPalette = 24;

// Distinct-ish translucent fills cycled by cluster id (golden-angle hues).
Color ClusterColor(uint32_t id) {
  float h = std::fmod((id % kPalette) * 0.618034f, 1.0f) * 6.0f;
  float x = 1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f);
  float r = 0, g = 0, b = 0;
  switch ((int)h) {
    case 0:
      r = 1, g = x;
      break;
    case 1:
      r = x, g = 1;
      break;
    case 2:
      g = 1, b = x;
      break;
    case 3:
      g = x, b = 1;
      break;
    case 4:
      r = x, b = 1;
      break;
    default:
      r = 1, b = x;
      break;
  }
  return {(uint8_t)(50 + r * 190), (uint8_t)(50 + g * 190),
          (uint8_t)(50 + b * 190), 60};
}

Color EdgeColor(uint8_t t) {
  switch (t) {
    case NavSkeleton::WALK:
      return {60, 220, 60};
    case NavSkeleton::STEP_UP:
      return {60, 220, 220};
    case NavSkeleton::STEP_DOWN:
      return {230, 170, 40};
    case NavSkeleton::DROP:
      return {130, 130, 130};
    default:
      return {230, 60, 230};  // PORTAL / FLING
  }
}

Vector Centroid(const NavSkeleton::CellCluster& cl) {
  return {(cl.mins.x + cl.maxs.x) * 0.5f, (cl.mins.y + cl.maxs.y) * 0.5f,
          cl.zMax + 4.0f};
}
}  // namespace

ON_EVENT(RENDER) {
  if (!sar_harness_nav_draw.GetBool() || !engine) {
    g_navMap.clear();  // toggling the draw back on rebuilds at live state
    return;
  }
  std::string map = engine->GetCurrentMapName();
  if (map != g_navMap) {
    g_nav.Build(map);
    g_navMap = map;
  }
  const std::vector<NavSkeleton::FloodCell>& cells = g_nav.Cells();
  if (cells.empty()) return;
  bool xray = sar_harness_nav_draw_xray.GetBool();
  bool drops = sar_harness_nav_draw_drops.GetBool();

  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  float cellR = sar_harness_nav_draw_cells.GetFloat();
  bool all = cellR <= 0 || !pl;
  Vector eye = pl ? pl->abs_origin() : Vector{0, 0, 0};

  // Cell carpet: one inset quad per cell so coverage holes read as gaps.
  MeshId fill[kPalette];
  for (int b = 0; b < kPalette; ++b)
    fill[b] = OverlayRender::createMesh(
        RenderCallback::constant(ClusterColor(b), xray), RenderCallback::none);
  constexpr float kPitch = NavSkeleton::kCellSize;
  constexpr float kHalf = kPitch * 0.5f - 2.0f;
  for (const NavSkeleton::FloodCell& c : cells) {
    float x = (c.cx + 0.5f) * kPitch, y = (c.cy + 0.5f) * kPitch;
    if (!all) {
      float dx = x - eye.x, dy = y - eye.y, dz = c.z - eye.z;
      if (dx * dx + dy * dy + dz * dz > cellR * cellR) continue;
    }
    float z = c.z + 1.0f;
    OverlayRender::addQuad(
        fill[c.cluster % kPalette], Vector{x - kHalf, y - kHalf, z},
        Vector{x + kHalf, y - kHalf, z}, Vector{x + kHalf, y + kHalf, z},
        Vector{x - kHalf, y + kHalf, z},
        /*cull_back*/ false);
  }

  // Cluster edges bend through their via point, so the line marks where the
  // transition physically is.
  const std::vector<NavSkeleton::CellCluster>& clusters = g_nav.Clusters();
  MeshId em[6];
  for (int t = 0; t < 6; ++t)
    em[t] = OverlayRender::createMesh(
        RenderCallback::none, RenderCallback::constant(EdgeColor(t), xray));
  for (const NavSkeleton::Edge& e : g_nav.ClusterEdges()) {
    if (e.type == NavSkeleton::DROP && !drops) continue;
    if (e.from >= clusters.size() || e.to >= clusters.size() || e.type >= 6)
      continue;
    Vector via = e.via + Vector{0, 0, 4.0f};
    OverlayRender::addLine(em[e.type], Centroid(clusters[e.from]), via);
    OverlayRender::addLine(em[e.type], via, Centroid(clusters[e.to]));
  }

  if (sar_harness_nav_draw_labels.GetBool())
    for (const NavSkeleton::CellCluster& cl : clusters)
      OverlayRender::addText(Centroid(cl) + Vector{0, 0, 4.0f},
                             "C" + std::to_string(cl.id), 4.0f,
                             /*visibility_scale*/ true, /*no_depth*/ true,
                             OverlayRender::TextAlign::CENTER, {220, 220, 220},
                             /*bg_col*/ {0, 0, 0, 160}, /*clamp*/ false, {});
}
