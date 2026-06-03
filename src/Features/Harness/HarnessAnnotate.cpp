#include <string>
#include <unordered_set>

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

// A2: the puzzle-relevant classnames we annotate, plus the player's own avatar.
static const std::unordered_set<std::string> kAnnotatedClasses = {
    // Core puzzle objects (design doc v1 set).
    "prop_portal",
    "prop_weighted_cube",
    "prop_monster_box",
    "prop_button",  // pedestal push button (CPortalButton)
    "func_weight_button",
    // Floor / weighted buttons -- the big red pedestal button and its variants.
    "prop_floor_button",        // big red 1500kg floor button
    "prop_under_floor_button",  // flush floor-button variant
    "prop_floor_cube_button",   // cube-only floor button
    "prop_floor_ball_button",   // ball-only floor button
    "prop_testchamber_door",
    "env_portal_laser",
    "prop_laser_catcher",
    "prop_laser_relay",
    "point_laser_target",
    "player",
    // Hazards + brush-trigger volumes (added at A2; final keep/drop decided at
    // the A5 checkpoint). Trigger volumes are invisible playspace -- their OBB
    // reads as a slab, not a tight object box. That's expected and useful.
    // Confirm m_Collision OBB populates for brush ents via a snapshot dump.
    "npc_portal_turret_floor",  // turret
    "trigger_portal_cleanser",  // emancipation grill / fizzler
    "trigger_catapult",         // faith plate
    "prop_tractor_beam",        // excursion funnel emitter
    // TODO(checkpoint): "chamber-mutating geometry" needs a different match
    // mechanism than this classname set, so it is deferred to the A5
    // checkpoint:
    //   - folding panels / stairs: a func_brush identified by targetname
    //     (e.g. "*_panel"), not classname -- needs a targetname-pattern filter,
    //     since func_brush is generic (glass, clips, scenery).
    //   - gels (orange/blue/white) + light bridges: paint/projector *surfaces*,
    //     not box-able entities at all.
};

// Box every entity whose classname is in kAnnotatedClasses. Iterates the server
// entity list directly (independent of any harness session), matching the loop
// in EntitySnapshotter::Update.
ON_EVENT(RENDER) {
  if (!sar_harness_annotate.GetBool()) return;
  if (!server || !entityList) return;

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;

    auto ent = info->m_pEntity;
    const char* className = server->GetEntityClassName(ent);
    if (!className || !kAnnotatedClasses.count(className)) continue;

    auto se = SE(ent);
    Vector origin = se->abs_origin();
    Vector mins = se->collision().OBBMins();
    Vector maxs = se->collision().OBBMaxs();
    QAngle angles = se->abs_angles();

    OverlayRender::addBoxMesh(origin, mins, maxs, angles,
                              RenderCallback::constant({255, 215, 0, 5}),
                              RenderCallback::constant({255, 215, 0}));

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
                           {255, 255, 255});
  }
}
