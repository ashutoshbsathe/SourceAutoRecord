#include <algorithm>
#include <string>
#include <vector>

#include "Event.hpp"
#include "Features/OverlayRender.hpp"
#include "Modules/Engine.hpp"
#include "NavSkeleton.hpp"
#include "Variable.hpp"

Variable sar_harness_nav_draw(
    "sar_harness_nav_draw", "0", 0, 1,
    "Draw the go_to nav surface graph: surfaces as translucent quads shaded by "
    "height, edges as lines colored by type. Rebuilds on map change.\n");
Variable sar_harness_nav_draw_drops("sar_harness_nav_draw_drops", "0", 0, 1,
                                    "Include DROP edges in the nav graph draw "
                                    "(they dominate; off by default).\n");
Variable sar_harness_nav_draw_labels("sar_harness_nav_draw_labels", "0", 0, 1,
                                     "Label each nav surface with its #id.\n");
Variable sar_harness_nav_draw_xray(
    "sar_harness_nav_draw_xray", "0", 0, 1,
    "Draw the nav graph with no depth test (through walls).\n");

namespace {
NavSkeleton g_nav;
std::string g_navMap;

Vector Center(const NavSkeleton::Surface& s) {
  if (s.poly.size() >= 4)
    return (s.poly[0] + s.poly[1] + s.poly[2] + s.poly[3]) * 0.25f;
  return {(s.mins.x + s.maxs.x) * 0.5f, (s.mins.y + s.maxs.y) * 0.5f, s.z};
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
}  // namespace

ON_EVENT(RENDER) {
  if (!sar_harness_nav_draw.GetBool() || !engine) return;
  std::string map = engine->GetCurrentMapName();
  if (map != g_navMap) {
    g_nav.Build(map);
    g_navMap = map;
  }
  const std::vector<NavSkeleton::Surface>& surfaces = g_nav.Surfaces();
  if (surfaces.empty()) return;
  bool xray = sar_harness_nav_draw_xray.GetBool();

  // Surface fills, shaded by height (low = blue, high = red), bucketed so a few
  // meshes carry all the quads.
  float minZ = surfaces[0].z, maxZ = surfaces[0].z;
  for (const NavSkeleton::Surface& s : surfaces) {
    minZ = std::min(minZ, s.z);
    maxZ = std::max(maxZ, s.z);
  }
  float span = maxZ - minZ;
  constexpr int kBuckets = 12;
  MeshId fill[kBuckets];
  for (int b = 0; b < kBuckets; ++b) {
    float t = (b + 0.5f) / kBuckets;
    Color c{(uint8_t)(60 + t * 160), 90, (uint8_t)(220 - t * 160), 45};
    fill[b] = OverlayRender::createMesh(RenderCallback::constant(c, xray),
                                        RenderCallback::none);
  }
  for (const NavSkeleton::Surface& s : surfaces) {
    if (s.poly.size() < 4) continue;
    float t = span > 1.0f ? (s.z - minZ) / span : 0.5f;
    int b = std::min(kBuckets - 1, (int)(t * kBuckets));
    // Lift along the surface normal (tracks inclines) with a per-id epsilon so
    // coplanar overlapping surfaces don't z-fight.
    Vector lift = s.normal * (1.0f + (s.id % 16) * 0.05f);
    OverlayRender::addQuad(fill[b], s.poly[0] + lift, s.poly[1] + lift,
                           s.poly[2] + lift, s.poly[3] + lift,
                           /*cull_back*/ false);
  }

  // Edges as lines between surface centers, one mesh per type.
  bool drops = sar_harness_nav_draw_drops.GetBool();
  MeshId em[6];
  for (int t = 0; t < 6; ++t)
    em[t] = OverlayRender::createMesh(
        RenderCallback::none, RenderCallback::constant(EdgeColor(t), xray));
  for (const NavSkeleton::Edge& e : g_nav.Edges()) {
    if (e.type == NavSkeleton::DROP && !drops) continue;
    if (e.from >= surfaces.size() || e.to >= surfaces.size() || e.type >= 6)
      continue;
    const NavSkeleton::Surface& a = surfaces[e.from];
    const NavSkeleton::Surface& b = surfaces[e.to];
    OverlayRender::addLine(em[e.type], Center(a) + a.normal * 2.0f,
                           Center(b) + b.normal * 2.0f);
  }

  if (sar_harness_nav_draw_labels.GetBool())
    for (const NavSkeleton::Surface& s : surfaces)
      OverlayRender::addText(Center(s) + s.normal * 3.0f,
                             "#" + std::to_string(s.id), 4.0f,
                             /*visibility_scale*/ true, /*no_depth*/ true,
                             OverlayRender::TextAlign::CENTER, {220, 220, 220},
                             /*bg_col*/ {0, 0, 0, 160}, /*clamp*/ false, {});
}
