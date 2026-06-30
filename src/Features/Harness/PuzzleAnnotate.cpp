#include "PuzzleAnnotate.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Command.hpp"
#include "Entity.hpp"
#include "Event.hpp"
#include "Features/Camera.hpp"
#include "Features/EntityList.hpp"
#include "Features/OverlayRender.hpp"
#include "LaserGeometry.hpp"
#include "MarkTable.hpp"
#include "SurfaceMarkTable.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/FileSystem.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Utils/Math.hpp"
#include "Utils/Memory.hpp"
#include "Utils/SDK/Class.hpp"
#include "Utils/SDK/Trace.hpp"
#include "Variable.hpp"

Variable sar_harness_annotate(
    "sar_harness_annotate", "0", 0, 1,
    "Draw wireframe annotation boxes around harness puzzle entities.\n");

Variable sar_harness_annotate_los(
    "sar_harness_annotate_los", "1", 0, 1,
    "Cull annotation boxes/marks for entities the camera can't see (LOS).\n");

// x_height of the mark label; kept small so labels don't collide and stack.
static constexpr float kMarkHeight = 4.0f;

// classname -> annotation color; membership also decides whether we annotate
// the class. Portals are recolored blue/orange at runtime via m_bIsPortal2.
static const std::unordered_map<std::string, Color> kClassColors = {
    // Core puzzle objects.
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
    {"point_laser_target", {255, 0, 255}},       // magenta - the goal surface
    // Laser catcher/relay unmarked: the goal bit m_bPowered reads off
    // point_laser_target, so they'd be noise. The player is unmarked: it's the
    // agent, not a target.
    // Hazards + brush-trigger volumes. Trigger volumes are invisible playspace
    // -- their OBB reads as a slab, not a tight object box.
    {"npc_portal_turret_floor", {180, 60, 220}},  // turret - violet
    {"trigger_portal_cleanser", {0, 210, 160}},   // fizzler - teal
    {"trigger_catapult", {255, 105, 180}},        // faith plate - pink
    {"prop_tractor_beam", {180, 255, 60}},        // funnel - lime
    // Not matchable by classname alone, so omitted: folding panels/stairs
    // (func_brush keyed by targetname) and gels/light bridges (paint surfaces).
};

// Whether this class is annotated/marked; MarkTable consults this too so the
// marked set matches the annotated set.
bool IsHarnessMarkedClass(const char* className) {
  return className && kClassColors.find(className) != kClassColors.end();
}

// Mark gate: class membership, plus reject env_portal_laser/prop_portal sitting
// at (0,0,0). Both ghost there -- transient laser re-emit segments carry an
// identity transform, and a prop_portal leaves abs_origin zeroed -- and marking
// them churns the mark table. A real emitter always has a true origin.
bool IsHarnessMarkedEntity(void* ent, const char* className) {
  if (!IsHarnessMarkedClass(className)) return false;
  if (!std::strcmp(className, "env_portal_laser") ||
      !std::strcmp(className, "prop_portal")) {
    Vector o = SE(ent)->abs_origin();
    if (o.x == 0.0f && o.y == 0.0f && o.z == 0.0f) return false;
  }
  return true;
}

// True if a ray from the eye to `target` is unobstructed by world geometry,
// with the player and the tested entity skipped so neither self-occludes the
// ray.
static bool MarkVisible(const Vector& eye, void* player, void* ent,
                        const Vector& target) {
  Vector d = target - eye;
  Ray_t ray;
  ray.m_IsRay = true;
  ray.m_IsSwept = true;
  ray.m_Start = VectorAligned(eye.x, eye.y, eye.z);
  ray.m_Delta = VectorAligned(d.x, d.y, d.z);
  ray.m_StartOffset = VectorAligned();
  ray.m_Extents = VectorAligned();
  SkipTwoEntities filter;
  filter.a = player;
  filter.b = ent;
  CGameTrace tr;
  engine->TraceRay(engine->engineTrace->ThisPtr(), ray, MASK_OPAQUE, &filter,
                   &tr);
  return tr.fraction > 0.97f;
}

// Visible if any of the box's sample points (center + 8 OBB corners) has a
// clear line to the eye -- robust to a thin occluder that happens to cross one
// ray.
static bool EntityVisible(const Vector& eye, void* player, void* ent,
                          const Vector& origin, const Vector& mins,
                          const Vector& maxs, const QAngle& angles) {
  if (MarkVisible(eye, player, ent, origin)) return true;
  auto rot = Math::AngleMatrix(angles);
  for (int i = 0; i < 8; ++i) {
    Vector corner{(i & 1) ? maxs.x : mins.x, (i & 2) ? maxs.y : mins.y,
                  (i & 4) ? maxs.z : mins.z};
    if (MarkVisible(eye, player, ent, origin + rot * corner)) return true;
  }
  return false;
}

// True if any box sample point projects onto the screen, i.e. the entity is at
// least partly in frame; gates the label clamp so off-screen entities' numbers
// aren't dragged onto a screen edge.
static bool InFrame(const Vector& origin, const Vector& mins,
                    const Vector& maxs, const QAngle& angles) {
  int sw = 0, sh = 0;
  engine->GetScreenSize(nullptr, sw, sh);
  if (sw <= 0 || sh <= 0) return true;
  auto rot = Math::AngleMatrix(angles);
  for (int i = 0; i <= 8; ++i) {
    Vector p = origin;
    if (i < 8) {
      Vector c{(i & 1) ? maxs.x : mins.x, (i & 2) ? maxs.y : mins.y,
               (i & 4) ? maxs.z : mins.z};
      p = origin + rot * c;
    }
    Vector s;
    if (engine->PointToScreen(p, s) != 0) continue;  // behind the camera
    if (s.x >= 0 && s.x < sw && s.y >= 0 && s.y < sh) return true;
  }
  return false;
}

// Box + label every entity whose classname is in kClassColors, colored by
// class, by walking the server entity list directly.
ON_EVENT(RENDER) {
  // Rebuild marks every frame so EntityState.mark telemetry stays correct even
  // when the visual overlay is off; only the drawing below is cvar-gated.
  markTable.RebuildFromWorld();

  if (!sar_harness_annotate.GetBool()) return;
  if (!server || !entityList) return;

  // Eye + camera-right, used for the LOS cull and for placing left/right label
  // candidates beside the box.
  Vector eye;
  QAngle eyeAng;
  bool haveEye = camera && camera->GetEyePos<false>(GET_SLOT(), eye, eyeAng);
  Vector camRight{1, 0, 0};
  if (haveEye) Math::AngleVectors(eyeAng, nullptr, &camRight, nullptr);
  // Skip the player in each trace: the eye sits inside its hull and would
  // otherwise self-occlude at the ray start.
  bool los = sar_harness_annotate_los.GetBool();
  void* player = los ? server->GetPlayer(GET_SLOT() + 1) : nullptr;
  bool doCull = los && player && haveEye;

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;

    auto ent = info->m_pEntity;
    const char* className = server->GetEntityClassName(ent);
    if (!className || !IsHarnessMarkedEntity(ent, className)) continue;
    auto colorIt = kClassColors.find(className);  // gate above guarantees a hit

    auto se = SE(ent);
    Color color = colorIt->second;
    // Orange for the secondary portal, blue (the map default) otherwise.
    if (std::strcmp(className, "prop_portal") == 0 &&
        se->field<bool>("m_bIsPortal2")) {
      color = {255, 128, 0};
    }

    Vector origin = se->abs_origin();
    Vector mins = se->collision().OBBMins();
    Vector maxs = se->collision().OBBMaxs();
    QAngle angles = se->abs_angles();
    Vector anchor = origin + Vector{0, 0, maxs.z};

    // Box is always drawn and depth-tested, so it occludes against world
    // geometry naturally.
    OverlayRender::addBoxMesh(
        origin, mins, maxs, angles,
        RenderCallback::constant({color.r, color.g, color.b, 5}),
        RenderCallback::constant(color));

    // Skip the number for an off-frame entity (else the clamp drags its label
    // onto a screen edge).
    if (!InFrame(origin, mins, maxs, angles)) continue;

    // The number draws on top via clamp_to_screen, so cull it when the entity is
    // occluded; otherwise marks behind walls float through.
    if (doCull && !EntityVisible(eye, player, ent, origin, mins, maxs, angles))
      continue;

    // Mark label above the box. clamp_to_screen keeps the number in the viewport
    // and on top of geometry.
    int mark =
        markTable.GetMark(i, static_cast<uint16_t>(info->m_SerialNumber));
    // Declutter alternates: box bottom, then either side along the camera-right
    // axis, so a crowded label can move sideways instead of only stacking.
    std::vector<Vector> alts = {origin + Vector{0, 0, mins.z}};
    if (haveEye) {
      auto habs = [](float v) { return v < 0 ? -v : v; };
      float ex = habs(mins.x) > habs(maxs.x) ? habs(mins.x) : habs(maxs.x);
      float ey = habs(mins.y) > habs(maxs.y) ? habs(mins.y) : habs(maxs.y);
      float radius = (ex > ey ? ex : ey) + 6.0f;
      alts.push_back(origin + camRight * radius);
      alts.push_back(origin - camRight * radius);
    }
    OverlayRender::addText(anchor, std::to_string(mark), kMarkHeight,
                           /*visibility_scale*/ true, /*no_depth*/ false,
                           OverlayRender::TextAlign::BOTTOM, color,
                           /*bg_col*/ {0, 0, 0, 200}, /*clamp_to_screen*/ true,
                           /*alts*/ alts);
  }

  // Outline + label every portalable wall panel with its S-mark, reusing the
  // entity-mark legibility (LOS cull + on-screen clamp). Uniform neutral style
  // so no panel reads as more portalable than another. The quad floats 1u off
  // the wall to avoid z-fighting the surface.
  for (const auto& panel : surfaceMarkTable.Panels()) {
    OverlayRender::addBoxMesh(panel.center + panel.planeNormal * 1.0f,
                              panel.mins - panel.center,
                              panel.maxs - panel.center, {0, 0, 0},
                              RenderCallback::constant({200, 200, 200, 5}),
                              RenderCallback::constant({90, 90, 90}));

    Vector anchor = panel.center + panel.planeNormal * 2.0f;
    if (doCull && !MarkVisible(eye, player, nullptr, anchor)) continue;
    int sw = 0, sh = 0;
    engine->GetScreenSize(nullptr, sw, sh);
    Vector s;
    if (sw > 0 && sh > 0 &&
        (engine->PointToScreen(anchor, s) != 0 || s.x < 0 || s.x >= sw ||
         s.y < 0 || s.y >= sh))
      continue;
    OverlayRender::addText(anchor, "S" + std::to_string(panel.mark),
                           kMarkHeight, /*visibility_scale*/ true,
                           /*no_depth*/ false, OverlayRender::TextAlign::CENTER,
                           {210, 210, 210}, /*bg_col*/ {0, 0, 0, 200},
                           /*clamp_to_screen*/ true, /*alts*/ {});
  }
}

// ---------------------------------------------------------------------------
// Recon: dump candidate "status" fields for puzzle entities, to find which
// engine field encodes each element's status (button pressed / door open / ...).
// Run twice -- before vs after a state change -- and diff to see which flipped.
// Reads via getServerOffset, which resolves the datamap and the SendTable, so
// datamap-only fields (e.g. m_toggle_state on doors) show up even though the
// snapshotter (SendTable-only) would miss them.

// Hand-picked likely status encodings across the puzzle classes. If the field
// you need isn't here, the name-substring scan below should still surface it;
// failing that, `sar_dump_server_datamap` lists every field for the class.
static const char* kStatusCandidates[] = {
    "m_bActivated",       "m_bIsPortal2", "m_hLinkedPortal",  // portal
    "m_bPowered",         "m_bIsPowered", "m_bEnabled",
    "m_bDisabled",                                      // laser/catcher
    "m_toggle_state",     "m_bOpen",      "m_bLocked",  // door
    "m_nSequence",  // door/panel open-state is anim-driven, not a bool
    "m_nCubeType",  // cube
    "m_lifeState",        "m_iHealth",    "m_bTipped",
    "m_bSelfDestructing",                                 // turret
    "m_bButtonDown",      "m_bPressed",   "m_bTouching",  // buttons
};

// Substrings that flag a prop as plausibly status-bearing, for the SendTable
// scan that catches anything not in kStatusCandidates.
static bool nameLooksStatusy(const char* n) {
  static const char* needles[] = {
      "Activ",   "Power",  "Enable", "Disable", "Open",  "oggle", "Press",
      "Touch",   "State",  "Lock",   "Tip",     "Life",  "Cube",  "Link",
      "Portal2", "Health", "Down",   "Latch",   "Catch", "Beam",  "Laser"};
  for (auto s : needles)
    if (std::strstr(n, s)) return true;
  return false;
}

// Format one field's current value as a string; empty string if the entity has
// no such field (so callers can probe freely).
static std::string ReconReadField(void* ent, const char* name) {
  auto val = EntField::getServerOffset(ent, name);
  size_t off = val.first;
  EntField::Type type = val.second;
  if (off == 0 || type == EntField::Type::NONE) return "";
  void* p = (char*)ent + off;
  char buf[96];
  switch (type) {
    case EntField::Type::BOOL:
      std::snprintf(buf, sizeof(buf), "%s", *(bool*)p ? "true" : "false");
      break;
    case EntField::Type::CHAR:
      std::snprintf(buf, sizeof(buf), "%d", (int)*(signed char*)p);
      break;
    case EntField::Type::SHORT:
      std::snprintf(buf, sizeof(buf), "%d", (int)*(short*)p);
      break;
    case EntField::Type::INT:
    case EntField::Type::ANY_INT:
      std::snprintf(buf, sizeof(buf), "%d", *(int*)p);
      break;
    case EntField::Type::FLOAT:
      std::snprintf(buf, sizeof(buf), "%.3f", *(float*)p);
      break;
    case EntField::Type::HANDLE:
      std::snprintf(buf, sizeof(buf), "0x%08X", *(int*)p);
      break;
    case EntField::Type::VECTOR: {
      Vector v = *(Vector*)p;
      std::snprintf(buf, sizeof(buf), "%.2f %.2f %.2f", v.x, v.y, v.z);
      break;
    }
    default:
      std::snprintf(buf, sizeof(buf), "<type %d>", (int)type);
      break;
  }
  return std::string(buf);
}

// One status-bearing field present on an entity. net=true means the field is a
// networked SendProp -- the snapshotter (SendTable-only) will see it; net=false
// means datamap-only, which the snapshotter currently misses.
struct ReconField {
  std::string name;
  std::string value;
  bool net;
};

// Gather every status-bearing field on an entity: the hand-picked candidates
// plus any SendTable prop whose name looks status-y, each tagged net/datamap.
static std::vector<ReconField> ReconCollect(void* ent) {
  std::unordered_set<std::string> netProps;
  ServerClass* sc = Memory::VMT<ServerClass*(__rescall*)(void*)>(
      ent, Offsets::GetServerClass)(ent);
  std::function<void(SendTable*)> walk = [&](SendTable* table) {
    if (!table) return;
    for (int j = 0; j < table->m_nProps; ++j) {
      SendProp* prop = &table->m_pProps[j];
      if (!prop->m_pVarName) continue;
      if (prop->m_Type == DPT_DataTable) {
        walk(prop->m_pDataTable);
        continue;
      }
      if (std::strcmp(prop->m_pVarName, "baseclass") == 0) continue;
      netProps.insert(prop->m_pVarName);
    }
  };
  if (sc) walk(sc->m_pTable);

  std::vector<ReconField> out;
  std::unordered_set<std::string> seen;
  auto add = [&](const std::string& name) {
    if (seen.count(name)) return;
    std::string v = ReconReadField(ent, name.c_str());
    if (v.empty()) return;
    seen.insert(name);
    out.push_back({name, v, netProps.count(name) > 0});
  };

  for (const char* name : kStatusCandidates) add(name);
  for (const auto& name : netProps)
    if (nameLooksStatusy(name.c_str())) add(name);
  return out;
}

// Baseline captured by the previous invocation, keyed "index|field" -> value,
// so a follow-up `diff` shows exactly which field flipped across a state
// change.
static std::unordered_map<std::string, std::string> g_reconBaseline;

CON_COMMAND(sar_harness_dump_fields,
            "sar_harness_dump_fields [diff|reset] - status-bearing fields for "
            "every puzzle entity (recon for the status resolver). No arg: full "
            "dump + store baseline. 'diff': only fields changed since the last "
            "dump (run it after a state change). 'reset': clear the baseline. "
            "Each field is tagged [net] (snapshotter sees it) or [dm ] "
            "(datamap-only, currently missed).\n") {
  if (args.ArgC() == 2 && !std::strcmp(args[1], "reset")) {
    g_reconBaseline.clear();
    console->Print("recon baseline cleared.\n");
    return;
  }
  if (!server || !entityList) {
    console->Print("recon: no server/entity list yet — load a map first.\n");
    return;
  }

  bool diff = args.ArgC() == 2 && !std::strcmp(args[1], "diff");
  if (diff && g_reconBaseline.empty()) {
    console->Print("no baseline yet; run sar_harness_dump_fields first.\n");
    diff = false;
  }

  std::unordered_map<std::string, std::string> current;
  int changes = 0;

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;

    auto ent = info->m_pEntity;
    const char* className = server->GetEntityClassName(ent);
    if (!className) continue;
    // Recon set = the mark/annotate classes plus the laser chain props
    // (catcher/relay) that kClassColors omits.
    bool reconClass = kClassColors.find(className) != kClassColors.end() ||
                      !std::strcmp(className, "prop_laser_catcher") ||
                      !std::strcmp(className, "prop_laser_relay");
    if (!reconClass) continue;

    const char* targetName = server->GetEntityName(ent);
    bool headerPrinted = false;
    auto header = [&]() {
      if (headerPrinted) return;
      headerPrinted = true;
      console->Print("[%d] %s \"%s\"\n", i, className,
                     (targetName && *targetName) ? targetName : "<no name>");
    };

    for (const auto& f : ReconCollect(ent)) {
      std::string key = std::to_string(i) + "|" + f.name;
      current[key] = f.value;
      const char* src = f.net ? "net" : "dm ";

      if (diff) {
        auto it = g_reconBaseline.find(key);
        if (it == g_reconBaseline.end() || it->second != f.value) {
          header();
          console->Msg(
              "    %s [%s] : %s -> %s\n", f.name.c_str(), src,
              it == g_reconBaseline.end() ? "(new)" : it->second.c_str(),
              f.value.c_str());
          ++changes;
        }
      } else {
        header();
        console->Msg("    %s [%s] = %s\n", f.name.c_str(), src,
                     f.value.c_str());
      }
    }
  }

  g_reconBaseline.swap(current);
  if (diff)
    console->Print("recon diff: %d field(s) changed.\n", changes);
  else
    console->Print(
        "recon dump: %d status field(s) across the matched puzzle entities.\n",
        (int)g_reconBaseline.size());
}

// ---------------------------------------------------------------------------
// Recon: geometry + state for laser entities and weighted cubes. Dumps a cube's
// angles alongside the re-emitted beam segment's forward vector to back out the
// cube-local axis the beam exits along. Read-only.

static bool IsLaserProbeClass(const char* c) {
  return !std::strcmp(c, "env_portal_laser") ||
         !std::strcmp(c, "prop_laser_catcher") ||
         !std::strcmp(c, "prop_laser_relay") ||
         !std::strcmp(c, "point_laser_target") ||
         !std::strcmp(c, "prop_weighted_cube");
}

// Decode an EHANDLE field to its live entity index, or -1 if absent/invalid.
static int ReconReadHandleIndex(void* ent, const char* name) {
  auto val = EntField::getServerOffset(ent, name);
  if (val.first == 0 || val.second == EntField::Type::NONE) return -1;
  unsigned int raw = *(unsigned int*)((char*)ent + val.first);
  if (raw == 0xFFFFFFFFu) return -1;
  return (int)(raw & (Offsets::NUM_ENT_ENTRIES - 1));
}

CON_COMMAND(
    sar_harness_laser_probe,
    "sar_harness_laser_probe - world origin, angles, forward vector, the "
    "on/powered/cubetype bit, and the target's parent/owner handles for "
    "every laser entity (emitter/segment/catcher/relay/target) and "
    "weighted cube. Read-only recon for the reflector-cube redirect "
    "axis: run it while a dropped cube redirects the beam.\n") {
  if (!server || !entityList) {
    console->Print(
        "laser probe: no server/entity list yet — load a map first.\n");
    return;
  }

  int n = 0;
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    auto ent = info->m_pEntity;
    const char* className = server->GetEntityClassName(ent);
    if (!className || !IsLaserProbeClass(className)) continue;

    auto se = SE(ent);
    Vector o = se->abs_origin();
    QAngle a = se->abs_angles();
    Vector fwd;
    Math::AngleVectors(a, &fwd);
    const char* name = server->GetEntityName(ent);

    console->Print("[%d] %s \"%s\"\n", i, className,
                   (name && *name) ? name : "<no name>");
    console->Msg(
        "    origin %.1f %.1f %.1f  ang(p/y/r) %.1f %.1f %.1f  fwd %.3f %.3f "
        "%.3f\n",
        o.x, o.y, o.z, a.x, a.y, a.z, fwd.x, fwd.y, fwd.z);

    if (!std::strcmp(className, "env_portal_laser")) {
      Vector bE, bF, bHit;
      float bLen;
      bool bounded = ComputeBeamSegment(ent, &bE, &bF, &bHit, &bLen);
      console->Msg("    beam -> hit %.1f %.1f %.1f  len %.1f%s\n", bHit.x,
                   bHit.y, bHit.z, bLen, bounded ? "" : " (unbounded)");
    }

    for (const char* f : {"m_bLaserOn", "m_bPowered", "m_nCubeType"}) {
      std::string v = ReconReadField(ent, f);
      if (!v.empty()) console->Msg("    %s = %s\n", f, v.c_str());
    }

    if (!std::strcmp(className, "point_laser_target")) {
      for (const char* h : {"m_hMoveParent", "m_hOwnerEntity"}) {
        int idx = ReconReadHandleIndex(ent, h);
        if (idx < 0) continue;
        auto pInfo = entityList->GetEntityInfoByIndex(idx);
        const char* pc = (pInfo && pInfo->m_pEntity)
                             ? server->GetEntityClassName(pInfo->m_pEntity)
                             : "?";
        console->Msg("    %s -> [%d] %s\n", h, idx, pc ? pc : "?");
      }
    }
    ++n;
  }
  console->Print("laser probe: %d entit%s.\n", n, n == 1 ? "y" : "ies");
}

// ---------------------------------------------------------------------------
// Mutating recon: teleport the free reflector cube onto a point on a chosen
// emitter's beam ray (param t along the ray + optional lateral offset), oriented
// so its local +X redirect axis aims at a chosen target, then read m_bPowered.
// Sweep t + lateral to map the interception envelope. The cube must not be held;
// reload to reset.

static void ReconTeleportFree(void* ent, const Vector& origin,
                              const QAngle& angles) {
  Vector zeroVel{0, 0, 0};
  using _Teleport = void(__rescall*)(void*, const Vector*, const QAngle*,
                                     const Vector*, bool);
  _Teleport Teleport = Memory::VMT<_Teleport>(ent, Offsets::StartTouch + 11);
  Teleport(ent, &origin, &angles, &zeroVel, true);
}

CON_COMMAND(
    sar_harness_laser_intercept_spike,
    "sar_harness_laser_intercept_spike <emitter_idx> <target_idx> <cube_idx> "
    "[t] [lateral] - rest cube_idx on the floor under emitter_idx's beam ray "
    "at "
    "(origin + t*fwd + lateral*perp), aimed +X at target_idx. <3 args: list "
    "selectable emitters/targets/cubes. Mutating recon; read m_bPowered "
    "after it settles.\n") {
  if (!server || !entityList) {
    console->Print("intercept spike: no server/entity list yet.\n");
    return;
  }

  if (args.ArgC() < 4) {
    console->Print(
        "usage: sar_harness_laser_intercept_spike <emitter_idx> <target_idx> "
        "<cube_idx> [t] [lateral]  (omit t to snap the cube perpendicular onto "
        "the beam)\n");
    for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
      auto info = entityList->GetEntityInfoByIndex(i);
      if (!info || !info->m_pEntity) continue;
      auto ent = info->m_pEntity;
      const char* cn = server->GetEntityClassName(ent);
      if (!cn) continue;
      bool isEmitter = !std::strcmp(cn, "env_portal_laser");
      bool isTarget = !std::strcmp(cn, "point_laser_target");
      bool isCube = !std::strcmp(cn, "prop_weighted_cube");
      if (!isEmitter && !isTarget && !isCube) continue;
      Vector o = SE(ent)->abs_origin();
      if (isEmitter) {
        QAngle a = SE(ent)->abs_angles();
        Vector f;
        Math::AngleVectors(a, &f);
        const char* nm = server->GetEntityName(ent);
        console->Msg(
            "  emitter [%d] \"%s\" origin %.0f %.0f %.0f fwd %.2f %.2f %.2f\n",
            i, (nm && *nm) ? nm : "<unnamed>", o.x, o.y, o.z, f.x, f.y, f.z);
      } else if (isTarget) {
        console->Msg("  target  [%d] origin %.0f %.0f %.0f m_bPowered=%s\n", i,
                     o.x, o.y, o.z, ReconReadField(ent, "m_bPowered").c_str());
      } else {
        console->Msg("  cube    [%d] origin %.0f %.0f %.0f m_nCubeType=%s\n", i,
                     o.x, o.y, o.z, ReconReadField(ent, "m_nCubeType").c_str());
      }
    }
    return;
  }

  int emIdx = std::atoi(args[1]);
  int tgtIdx = std::atoi(args[2]);
  int cubeIdx = std::atoi(args[3]);

  auto getEnt = [&](int idx) -> void* {
    if (idx < 0 || idx >= Offsets::NUM_ENT_ENTRIES) return nullptr;
    auto info = entityList->GetEntityInfoByIndex(idx);
    return (info && info->m_pEntity) ? info->m_pEntity : nullptr;
  };
  void* emitter = getEnt(emIdx);
  void* target = getEnt(tgtIdx);
  void* cube = getEnt(cubeIdx);
  if (!emitter || !target || !cube) {
    console->Print("intercept spike: bad emitter/target/cube index.\n");
    return;
  }

  Vector E = SE(emitter)->abs_origin();
  QAngle ea = SE(emitter)->abs_angles();
  Vector F;
  Math::AngleVectors(ea, &F);
  Vector perp{F.y, -F.x, 0};
  Math::VectorNormalize(perp);

  // Default t (no arg): the cube's current position projected onto the ray (a
  // perpendicular snap onto the beam). An explicit t is a world-unit distance
  // along the ray from the emitter.
  Vector rel = SE(cube)->abs_origin() - E;
  float t = args.ArgC() >= 5 ? (float)std::atof(args[4])
                             : rel.x * F.x + rel.y * F.y + rel.z * F.z;
  float lateral = args.ArgC() >= 6 ? (float)std::atof(args[5]) : 0.0f;
  Vector P = E + F * t + perp * lateral;

  // Rest the cube on the floor under P (down-trace); dropping it mid-air would
  // let the fall tumble its yaw past the redirect tolerance.
  Vector seat;
  if (!DownTraceRest(P, cube, &seat)) {
    console->Print(
        "intercept spike: NO_FLOOR under (%.1f %.1f %.1f) -- pit/void; "
        "refusing to place the cube mid-air.\n",
        P.x, P.y, P.z);
    return;
  }
  P = seat;

  Vector tgtC = SE(target)->abs_origin();
  QAngle cubeAng = ComputeRedirectYaw(P, tgtC);
  Vector aim = tgtC - P;
  Math::VectorNormalize(aim);  // recomputed only for the recon print below

  float inClear = LaserLineClear(E, P, emitter, cube);
  float outClear = LaserLineClear(P, tgtC, cube, target);

  ReconTeleportFree(cube, P, cubeAng);

  console->Print("intercept spike: cube [%d] -> (%.1f %.1f %.1f) yaw %.1f\n",
                 cubeIdx, P.x, P.y, P.z, cubeAng.y);
  console->Msg("  t=%.2f lateral=%.1f  +X aims %.2f %.2f %.2f at target [%d]\n",
               t, lateral, aim.x, aim.y, aim.z, tgtIdx);
  console->Msg(
      "  emitter->cube clear=%.2f  cube->target clear=%.2f  (1.00 = "
      "unobstructed)\n",
      inClear, outClear);
  console->Msg(
      "  rested on floor; read m_bPowered: sar_harness_laser_probe (or watch "
      "the catcher).\n");
}

// ---------------------------------------------------------------------------
// Recon: portal placement. The probe is read-only: it dumps the portalgun's
// fire-ability and, per prop_portal, both origin reads (a freshly placed portal
// zeroes abs_origin; server->GetAbsOrigin carries the real face), the link
// handle, and the color bit. The fire spike commits a portal along the player's
// view: TraceFirePortal only previews, so portal_place does the actual commit,
// then the result is read back. Aim at a surface, then fire.

CON_COMMAND(
    sar_harness_portal_probe,
    "sar_harness_portal_probe - portalgun fire-ability + per-prop_portal "
    "abs_origin vs server origin, m_hLinkedPortal, m_bIsPortal2, m_bActivated. "
    "Read-only recon.\n") {
  if (!server || !entityList) {
    console->Print("portal probe: no server/entity list yet — load a map.\n");
    return;
  }

  void* player = server->GetPlayer(1);
  if (player) {
    auto wpn = SE(player)->active_weapon();
    void* gun = entityList->LookupEntity(wpn);
    bool isGun = gun && entityList->IsPortalGun(wpn);
    const char* gc = gun ? server->GetEntityClassName(gun) : nullptr;
    console->Print("portalgun: active_weapon=%s is_portalgun=%s\n",
                   gc ? gc : "<none>", isGun ? "yes" : "no");
    if (isGun) {
      console->Msg(
          "    m_bCanFirePortal1=%s m_bCanFirePortal2=%s linkage=%s\n",
          ReconReadField(gun, "m_bCanFirePortal1").c_str(),
          ReconReadField(gun, "m_bCanFirePortal2").c_str(),
          ReconReadField(gun, "m_iPortalLinkageGroupID").c_str());
      console->Msg("    m_hPrimaryPortal -> [%d]  m_hSecondaryPortal -> [%d]\n",
                   ReconReadHandleIndex(gun, "m_hPrimaryPortal"),
                   ReconReadHandleIndex(gun, "m_hSecondaryPortal"));
    }
  } else {
    console->Print("portalgun: no player.\n");
  }

  int n = 0;
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    auto ent = info->m_pEntity;
    const char* cn = server->GetEntityClassName(ent);
    if (!cn || std::strcmp(cn, "prop_portal")) continue;

    Vector oAbs = SE(ent)->abs_origin();
    Vector oSrv = server->GetAbsOrigin(ent);
    const char* nm = server->GetEntityName(ent);
    console->Print("[%d] prop_portal \"%s\"\n", i, (nm && *nm) ? nm : "<no name>");
    console->Msg(
        "    abs_origin %.1f %.1f %.1f  server_origin %.1f %.1f %.1f\n", oAbs.x,
        oAbs.y, oAbs.z, oSrv.x, oSrv.y, oSrv.z);
    console->Msg(
        "    m_bActivated=%s m_bIsPortal2=%s m_hLinkedPortal -> [%d]\n",
        ReconReadField(ent, "m_bActivated").c_str(),
        ReconReadField(ent, "m_bIsPortal2").c_str(),
        ReconReadHandleIndex(ent, "m_hLinkedPortal"));
    ++n;
  }
  console->Print("portal probe: %d prop_portal entit%s.\n", n,
                 n == 1 ? "y" : "ies");
}

CON_COMMAND(
    sar_harness_portal_fire_spike,
    "sar_harness_portal_fire_spike <blue|orange> - fire a portal along the "
    "player's current view. Logs TraceFirePortal's preview (ePlacementResult + "
    "finalPos), commits via portal_place, then reads the placed portal back. "
    "Mutating recon; aim at a surface first, reload to reset.\n") {
  if (!server || !entityList || !engine) {
    console->Print("portal fire spike: no server/entity list yet.\n");
    return;
  }
  if (args.ArgC() < 2 ||
      (std::strcmp(args[1], "blue") && std::strcmp(args[1], "orange"))) {
    console->Print(
        "usage: sar_harness_portal_fire_spike <blue|orange>  (aim first)\n");
    return;
  }
  bool orange = !std::strcmp(args[1], "orange");

  void* player = server->GetPlayer(1);
  if (!player) {
    console->Print("portal fire spike: no player.\n");
    return;
  }
  auto wpn = SE(player)->active_weapon();
  uintptr_t gun = (uintptr_t)entityList->LookupEntity(wpn);
  if (!gun || !entityList->IsPortalGun(wpn)) {
    console->Print("portal fire spike: no portalgun equipped.\n");
    return;
  }

  // Prime the gun's portal entities so the placement has a backing prop_portal,
  // as TraceFirePortal expects.
  unsigned char linkage = SE(gun)->field<unsigned char>("m_iPortalLinkageGroupID");
  if (!entityList->LookupEntity(SE(gun)->field<CBaseHandle>("m_hPrimaryPortal"))) {
    auto b = server->FindPortal(linkage, false, true);
    SE(gun)->field<CBaseHandle>("m_hPrimaryPortal") =
        ((IHandleEntity*)b)->GetRefEHandle();
  }
  if (!entityList->LookupEntity(SE(gun)->field<CBaseHandle>("m_hSecondaryPortal"))) {
    auto o = server->FindPortal(linkage, true, true);
    SE(gun)->field<CBaseHandle>("m_hSecondaryPortal") =
        ((IHandleEntity*)o)->GetRefEHandle();
  }

  Vector eye;
  QAngle ang;
  if (!camera || !camera->GetEyePos<true>(0, eye, ang)) {
    console->Print("portal fire spike: no eye position.\n");
    return;
  }
  Vector dir;
  Math::AngleVectors(ang, &dir);

  TracePortalPlacementInfo_t pinfo;
  int ret = server->TraceFirePortal(gun, eye, dir, orange, 2, pinfo);
  static const char* kResultName[] = {
      "SUCCESS",        "USED_HELPER",          "BUMPED",
      "CANT_FIT",       "CLEANSER",             "OVERLAP_LINKED",
      "OVERLAP_PARTNER", "INVALID_VOLUME",      "INVALID_SURFACE",
      "PASSTHROUGH"};
  int r = (int)pinfo.ePlacementResult;
  const char* rn = (r >= 0 && r < 10) ? kResultName[r] : "?";
  console->Print(
      "portal fire spike: %s  ret=%d ePlacementResult=%d (%s)\n",
      orange ? "orange" : "blue", ret, r, rn);
  console->Msg(
      "    finalPos %.1f %.1f %.1f  finalAngle %.1f %.1f %.1f  helper=%s\n",
      pinfo.finalPos.x, pinfo.finalPos.y, pinfo.finalPos.z, pinfo.finalAngle.x,
      pinfo.finalAngle.y, pinfo.finalAngle.z,
      pinfo.placementHelper ? "yes" : "no");

  if (r > (int)PORTAL_PLACEMENT_BUMPED) {
    console->Print("    not placeable here — aim at a portalable surface.\n");
    return;
  }

  char cmd[160];
  std::snprintf(cmd, sizeof(cmd),
                "portal_place %d %d %.6f %.6f %.6f %.6f %.6f %.6f", (int)linkage,
                orange ? 1 : 0, pinfo.finalPos.x, pinfo.finalPos.y,
                pinfo.finalPos.z, pinfo.finalAngle.x, pinfo.finalAngle.y,
                pinfo.finalAngle.z);
  engine->ExecuteCommand(cmd);

  void* portal = (void*)server->FindPortal(linkage, orange, false);
  if (!portal) {
    console->Print("    portal_place issued but FindPortal returned none.\n");
    return;
  }
  Vector pAbs = SE(portal)->abs_origin();
  Vector pSrv = server->GetAbsOrigin(portal);
  console->Print(
      "    placed: abs_origin %.1f %.1f %.1f  server_origin %.1f %.1f %.1f\n",
      pAbs.x, pAbs.y, pAbs.z, pSrv.x, pSrv.y, pSrv.z);
  console->Msg(
      "    m_bActivated=%s m_bIsPortal2=%s m_hLinkedPortal -> [%d]  (probe "
      "again after both colors to confirm the pair)\n",
      ReconReadField(portal, "m_bActivated").c_str(),
      ReconReadField(portal, "m_bIsPortal2").c_str(),
      ReconReadHandleIndex(portal, "m_hLinkedPortal"));
}

// ---------------------------------------------------------------------------
// Read-only portal-surface recon (no portal placed): lists every
// info_placement_helper (origin/radius/state), then sweeps a coarse
// TraceFirePortal preview grid across the wall under the crosshair and prints it
// as an ASCII portalability map, marking which cells snapped to a helper.

static void PlaneAxes(Vector n, Vector* ax1, Vector* ax2) {
  Vector up = (n.z < 0.9f && n.z > -0.9f) ? Vector{0, 0, 1} : Vector{1, 0, 0};
  *ax1 = n.Cross(up).Normalize();
  *ax2 = n.Cross(*ax1).Normalize();
}

CON_COMMAND(
    sar_harness_portal_surface_census,
    "sar_harness_portal_surface_census - surface recon. Lists every "
    "info_placement_helper (origin/radius/state), then sweeps a coarse "
    "TraceFirePortal preview grid on the wall under the crosshair and prints "
    "an ASCII portalability map. Read-only (no portal placed); aim at a wall "
    "first.\n") {
  if (!server || !entityList || !engine) {
    console->Print("portal surface census: no server/entity list yet.\n");
    return;
  }

  int helpers = 0;
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    auto ent = info->m_pEntity;
    const char* cn = server->GetEntityClassName(ent);
    if (!cn || std::strcmp(cn, "info_placement_helper")) continue;
    auto se = SE(ent);
    Vector o = se->abs_origin();
#ifdef _WIN32
    float radius = se->fieldOff<float>("m_flRadius", 648);
#else
    float radius = se->fieldOff<float>("m_flRadius", 664);
#endif
    const char* nm = server->GetEntityName(ent);
    console->Print("[%d] info_placement_helper \"%s\"\n", i,
                   (nm && *nm) ? nm : "<no name>");
    console->Msg(
        "    origin %.1f %.1f %.1f  radius %.1f  force=%d snap=%d disabled=%d "
        "defer=%d\n",
        o.x, o.y, o.z, radius, (int)se->field<bool>("m_bForcePlacement"),
        (int)se->field<bool>("m_bSnapToHelperAngles"),
        (int)se->field<bool>("m_bDisabled"),
        (int)se->field<bool>("m_bDeferringToPortal"));
    ++helpers;
  }
  console->Print("helpers: %d info_placement_helper entit%s.\n", helpers,
                 helpers == 1 ? "y" : "ies");

  void* player = server->GetPlayer(1);
  Vector eye;
  QAngle ang;
  if (!player || !camera || !camera->GetEyePos<true>(0, eye, ang)) {
    console->Print("portal surface census: no eye/player.\n");
    return;
  }
  Vector fwd;
  Math::AngleVectors(ang, &fwd);
  Vector delta = fwd * 2500.0f;

  Ray_t ray;
  ray.m_IsRay = true;
  ray.m_IsSwept = true;
  ray.m_Start = VectorAligned(eye.x, eye.y, eye.z);
  ray.m_Delta = VectorAligned(delta.x, delta.y, delta.z);
  ray.m_StartOffset = VectorAligned();
  ray.m_Extents = VectorAligned();
  SkipTwoEntities filter;
  filter.a = player;
  filter.b = nullptr;
  CGameTrace tr;
  engine->TraceRay(engine->engineTrace->ThisPtr(), ray, MASK_SHOT_PORTAL,
                   &filter, &tr);
  if (tr.fraction >= 1.0f || tr.plane.normal.Length() < 0.9f) {
    console->Print("    no wall under the crosshair — aim at a surface.\n");
    return;
  }
  Vector n = tr.plane.normal;
  Vector hit = eye + delta * tr.fraction;
  Vector ax1, ax2;
  PlaneAxes(n, &ax1, &ax2);
  console->Msg("    aimed wall: hit %.1f %.1f %.1f  normal %.2f %.2f %.2f\n",
               hit.x, hit.y, hit.z, n.x, n.y, n.z);

  // Prime the gun's two portal entities; no portal is placed, TraceFirePortal
  // previews only.
  auto wpn = SE(player)->active_weapon();
  uintptr_t gun = (uintptr_t)entityList->LookupEntity(wpn);
  if (!gun || !entityList->IsPortalGun(wpn)) {
    console->Print("    no portalgun equipped — can't probe portalability.\n");
    return;
  }
  unsigned char linkage =
      SE(gun)->field<unsigned char>("m_iPortalLinkageGroupID");
  if (!entityList->LookupEntity(
          SE(gun)->field<CBaseHandle>("m_hPrimaryPortal"))) {
    auto b = server->FindPortal(linkage, false, true);
    SE(gun)->field<CBaseHandle>("m_hPrimaryPortal") =
        ((IHandleEntity*)b)->GetRefEHandle();
  }
  if (!entityList->LookupEntity(
          SE(gun)->field<CBaseHandle>("m_hSecondaryPortal"))) {
    auto o = server->FindPortal(linkage, true, true);
    SE(gun)->field<CBaseHandle>("m_hSecondaryPortal") =
        ((IHandleEntity*)o)->GetRefEHandle();
  }

  // '#' placeable, 'H' snapped to a placement helper, '.' not portalable.
  const float kStep = 64.0f;
  const int kHalf = 4;
  int placeable = 0, usedHelper = 0;
  for (int r = kHalf; r >= -kHalf; --r) {
    char rowbuf[16];
    int col = 0;
    for (int c = -kHalf; c <= kHalf; ++c) {
      Vector p = hit + ax1 * (c * kStep) + ax2 * (r * kStep);
      Vector origin = p + n * 10.0f;
      Vector dir = -n;
      TracePortalPlacementInfo_t pinfo;
      server->TraceFirePortal(gun, origin, dir, false, 2, pinfo);
      int res = (int)pinfo.ePlacementResult;
      char ch;
      if (res == PORTAL_PLACEMENT_USED_HELPER) {
        ch = 'H';
        ++usedHelper;
        ++placeable;
      } else if (res <= PORTAL_PLACEMENT_BUMPED) {
        ch = '#';
        ++placeable;
      } else {
        ch = '.';
      }
      rowbuf[col++] = ch;
    }
    rowbuf[col] = '\0';
    console->Msg("    %s\n", rowbuf);
  }
  int total = (2 * kHalf + 1) * (2 * kHalf + 1);
  console->Print(
      "portal surface census: %d/%d cells portalable (%d via helper) at %gu "
      "spacing on the aimed wall.\n",
      placeable, total, usedHelper, kStep);
}

// Trace the crosshair (MASK_SOLID, so grates/glass are hit instead of passed
// through) and dump the hit face's surface metadata.
CON_COMMAND(
    sar_harness_bsp_face_probe,
    "sar_harness_bsp_face_probe - trace the crosshair and print the hit "
    "surface's material, flags (+ SURF_NOPORTAL bit), worldSurfaceIndex, "
    "plane, hit point, and world-brush-vs-entity. Aim at a wall first.\n") {
  if (!server || !entityList || !engine) {
    console->Print("bsp face probe: no server/entity list yet — load a map.\n");
    return;
  }
  void* player = server->GetPlayer(1);
  Vector eye;
  QAngle ang;
  if (!player || !camera || !camera->GetEyePos<true>(0, eye, ang)) {
    console->Print("bsp face probe: no eye/player.\n");
    return;
  }
  Vector fwd;
  Math::AngleVectors(ang, &fwd);
  Vector delta = fwd * 2500.0f;

  Ray_t ray;
  ray.m_IsRay = true;
  ray.m_IsSwept = true;
  ray.m_Start = VectorAligned(eye.x, eye.y, eye.z);
  ray.m_Delta = VectorAligned(delta.x, delta.y, delta.z);
  ray.m_StartOffset = VectorAligned();
  ray.m_Extents = VectorAligned();
  SkipTwoEntities filter;
  filter.a = player;
  filter.b = nullptr;
  CGameTrace tr;
  engine->TraceRay(engine->engineTrace->ThisPtr(), ray, MASK_SOLID, &filter,
                   &tr);
  if (tr.fraction >= 1.0f || tr.plane.normal.Length() < 0.9f) {
    console->Print("    no surface under the crosshair — aim at a wall.\n");
    return;
  }
  Vector hit = eye + delta * tr.fraction;
  const char* mat = tr.surface.name ? tr.surface.name : "<null>";
  const char* cls = tr.m_pEnt ? server->GetEntityClassName(tr.m_pEnt) : "world";
  console->Print("[bsp face] material \"%s\"  (%s)\n", mat, cls ? cls : "?");
  console->Msg("    flags 0x%04X  noportal=%d  worldSurfaceIndex=%u\n",
               tr.surface.flags, (tr.surface.flags & SURF_NOPORTAL) ? 1 : 0,
               tr.worldSurfaceIndex);
  console->Msg(
      "    hit %.1f %.1f %.1f  normal %.2f %.2f %.2f  dist %.1f  contents "
      "0x%X\n",
      hit.x, hit.y, hit.z, tr.plane.normal.x, tr.plane.normal.y,
      tr.plane.normal.z, tr.plane.dist, tr.contents);
}

// Open the running map's .bsp from the engine search paths and dump the
// geometry lumps' directory entries + a per-lump LZMA marker.
CON_COMMAND(
    sar_harness_bsp_lump_probe,
    "sar_harness_bsp_lump_probe - resolve maps/<currentmap>.bsp and print the "
    "header plus the offset/size/fourCC and payload magic of the geometry "
    "lumps. Read-only; flags any LZMA-compressed lump.\n") {
  if (!engine || !fileSystem) {
    console->Print("bsp lump probe: no engine/filesystem.\n");
    return;
  }
  std::string map = engine->GetCurrentMapName();
  if (map.empty()) {
    console->Print("bsp lump probe: no map loaded.\n");
    return;
  }
  std::string rel = "maps/" + map + ".bsp";
  std::string path = fileSystem->FindFileSomewhere(rel).value_or("");
  if (path.empty()) {
    console->Print("bsp lump probe: couldn't resolve \"%s\".\n", rel.c_str());
    return;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    console->Print("bsp lump probe: couldn't open \"%s\".\n", path.c_str());
    return;
  }

  int ident = 0, version = 0;
  f.read((char*)&ident, 4);
  f.read((char*)&version, 4);
  char id4[5] = {0};
  std::memcpy(id4, &ident, 4);
  console->Print("bsp \"%s\"\n    ident \"%s\"  version %d\n", path.c_str(), id4,
                 version);

  struct LumpRef {
    int idx;
    const char* name;
  };
  const LumpRef wanted[] = {
      {1, "PLANES"},     {2, "TEXDATA"},         {3, "VERTEXES"},
      {6, "TEXINFO"},    {7, "FACES"},           {12, "EDGES"},
      {13, "SURFEDGES"}, {43, "TEXDATASTRDATA"}, {44, "TEXDATASTRTBL"},
  };
  bool anyLzma = false;
  for (const auto& w : wanted) {
    // lump_t = { int fileofs; int filelen; int version; int fourCC; } at
    // header offset 8 + idx*16. A nonzero fourCC or a "LZMA" payload magic
    // means the lump is compressed.
    f.seekg(8 + w.idx * 16, std::ios::beg);
    int ofs = 0, len = 0, lver = 0, fourCC = 0;
    f.read((char*)&ofs, 4);
    f.read((char*)&len, 4);
    f.read((char*)&lver, 4);
    f.read((char*)&fourCC, 4);
    unsigned char m[4] = {0, 0, 0, 0};
    if (ofs > 0 && len >= 4) {
      f.seekg(ofs, std::ios::beg);
      f.read((char*)m, 4);
    }
    bool lzma = fourCC != 0 ||
                (m[0] == 'L' && m[1] == 'Z' && m[2] == 'M' && m[3] == 'A');
    anyLzma = anyLzma || lzma;
    console->Msg(
        "    [%2d] %-14s ofs %9d len %9d ver %d fourCC %d magic "
        "%02X%02X%02X%02X%s\n",
        w.idx, w.name, ofs, len, lver, fourCC, m[0], m[1], m[2], m[3],
        lzma ? "  <LZMA>" : "");
  }
  console->Print(
      "bsp lump probe: %s\n",
      anyLzma ? "LZMA-compressed lump(s) present — a decoder is required."
              : "geometry lumps uncompressed — no LZMA decoder needed.");
}
