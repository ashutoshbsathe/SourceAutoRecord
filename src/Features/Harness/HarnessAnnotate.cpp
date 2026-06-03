#include <cstring>
#include <string>
#include <unordered_map>

#include "Entity.hpp"
#include "Event.hpp"
#include "Features/EntityList.hpp"
#include "Features/OverlayRender.hpp"
#include "MarkTable.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Variable.hpp"

Variable sar_harness_annotate(
    "sar_harness_annotate", "0", 0, 1,
    "Draw wireframe annotation boxes around harness puzzle entities.\n");

// x_height of the mark label. Single digits, so keep it legible; tune freely.
static constexpr float kMarkHeight = 6.0f;

// A2/A4: classname -> annotation color. Membership here is also the "do we
// annotate it?" test (one lookup gives both). Portals are special-cased at
// runtime to blue/orange via m_bIsPortal2; their entry is just the base color.
static const std::unordered_map<std::string, Color> kClassColors = {
    // Core puzzle objects (design doc v1 set).
    {"prop_portal", {64, 160, 255}},        // blue primary; orange if Portal2
    {"prop_weighted_cube", {255, 215, 0}},  // gold
    {"prop_monster_box", {255, 215, 0}},    // gold (cube variant)
    {"prop_button", {64, 220, 64}},         // green - pedestal push button
    {"func_weight_button", {64, 220, 64}},
    // Floor / weighted buttons -- the big red pedestal button and its variants.
    {"prop_floor_button", {64, 220, 64}},        // big red 1500kg floor button
    {"prop_under_floor_button", {64, 220, 64}},  // flush variant
    {"prop_floor_cube_button", {64, 220, 64}},   // cube-only
    {"prop_floor_ball_button", {64, 220, 64}},   // ball-only
    {"prop_testchamber_door", {255, 255, 255}},  // white
    {"env_portal_laser", {255, 40, 40}},         // red - laser emitter
    {"prop_laser_catcher", {255, 40, 40}},       // red - laser chain
    {"prop_laser_relay", {255, 40, 40}},         // red - laser chain
    {"point_laser_target", {255, 0, 255}},       // magenta - the goal surface
    {"player", {0, 255, 255}},                   // cyan
    // Hazards + brush-trigger volumes (added at A2; keep/drop at A5 checkpoint;
    // colors here are provisional, tune at the checkpoint). Trigger volumes are
    // invisible playspace -- their OBB reads as a slab, not a tight object box.
    // Confirm m_Collision OBB populates for brush ents via a snapshot dump.
    {"npc_portal_turret_floor", {180, 60, 220}},  // turret - violet
    {"trigger_portal_cleanser", {0, 210, 160}},   // fizzler - teal
    {"trigger_catapult", {255, 105, 180}},        // faith plate - pink
    {"prop_tractor_beam", {180, 255, 60}},        // funnel - lime
    // TODO(checkpoint): "chamber-mutating geometry" needs a different match
    // mechanism than this classname map, so it is deferred to the A5
    // checkpoint:
    //   - folding panels / stairs: a func_brush identified by targetname
    //     (e.g. "*_panel"), not classname -- needs a targetname-pattern filter,
    //     since func_brush is generic (glass, clips, scenery).
    //   - gels (orange/blue/white) + light bridges: paint/projector *surfaces*,
    //     not box-able entities at all.
};

// Box + label every entity whose classname is in kClassColors, colored by
// class. Iterates the server entity list directly (independent of any harness
// session), matching the loop in EntitySnapshotter::Update.
ON_EVENT(RENDER) {
  if (!sar_harness_annotate.GetBool()) return;
  if (!server || !entityList) return;

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;

    auto ent = info->m_pEntity;
    const char* className = server->GetEntityClassName(ent);
    if (!className) continue;
    auto colorIt = kClassColors.find(className);
    if (colorIt == kClassColors.end()) continue;

    auto se = SE(ent);
    Color color = colorIt->second;
    // Portals: blue (primary) is the map default, orange for the secondary.
    if (std::strcmp(className, "prop_portal") == 0 &&
        se->field<bool>("m_bIsPortal2")) {
      color = {255, 128, 0};
    }

    Vector origin = se->abs_origin();
    Vector mins = se->collision().OBBMins();
    Vector maxs = se->collision().OBBMaxs();
    QAngle angles = se->abs_angles();

    OverlayRender::addBoxMesh(
        origin, mins, maxs, angles,
        RenderCallback::constant({color.r, color.g, color.b, 5}),
        RenderCallback::constant(color));

    // Stable Set-of-Marks label, sitting just above the box top. Depth-tested
    // (no_depth=false) to match the boxes. Neither depth flag is clean for a
    // flat world-space text quad: depth-tested gets sliced by a wall the label
    // sits against, on-top x-rays every mark through walls (worse). The real
    // fix is the Track B (B1) LOS predicate -- cull marks for occluded entities
    // entirely; see B1's note.
    int mark =
        markTable.GetMark(i, static_cast<uint16_t>(info->m_SerialNumber));
    OverlayRender::addText(origin + Vector{0, 0, maxs.z}, std::to_string(mark),
                           kMarkHeight, /*visibility_scale*/ true,
                           /*no_depth*/ false, OverlayRender::TextAlign::BOTTOM,
                           color);
  }
}
