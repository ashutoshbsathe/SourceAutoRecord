#include "PuzzleAnnotate.hpp"

#include <cstdio>
#include <cstring>
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
#include "MarkTable.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
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

// x_height of the mark label. Single digits, so keep it legible; tune freely.
// Kept modest: oversized labels collide constantly and stack into tall columns.
static constexpr float kMarkHeight = 4.0f;

// classname -> annotation color. Membership here is also the "do we annotate
// it?" test (one lookup gives both). Portals are special-cased at runtime to
// blue/orange via m_bIsPortal2; their entry is just the base color.
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
    {"prop_laser_catcher", {255, 40, 40}},       // red - laser chain
    {"prop_laser_relay", {255, 40, 40}},         // red - laser chain
    {"point_laser_target", {255, 0, 255}},       // magenta - the goal surface
    // The player is deliberately absent: it's the agent, not an addressable
    // target, so it gets neither a mark nor a (self-occluding) first-person
    // box.
    // Hazards + brush-trigger volumes (provisional colors). Trigger volumes are
    // invisible playspace -- their OBB reads as a slab, not a tight object box.
    {"npc_portal_turret_floor", {180, 60, 220}},  // turret - violet
    {"trigger_portal_cleanser", {0, 210, 160}},   // fizzler - teal
    {"trigger_catapult", {255, 105, 180}},        // faith plate - pink
    {"prop_tractor_beam", {180, 255, 60}},        // funnel - lime
    // Not handled here, since classname alone can't match them: folding
    // panels/stairs (func_brush keyed by targetname) and gels/light bridges
    // (paint surfaces, not box-able entities).
};

// Single source of truth for "do we annotate/mark this class?" -- MarkTable
// consults this so the marked set matches the annotated set drawn below.
bool IsHarnessMarkedClass(const char* className) {
  return className && kClassColors.find(className) != kClassColors.end();
}

// Trace filter that skips two entities: the player (the ray starts inside our
// own hull) and the entity under test (so the ray doesn't stop on its own
// surface). Without skipping the player, every ray hits us immediately.
class SkipTwoEntities : public CTraceFilter {
 public:
  const void* a = nullptr;
  const void* b = nullptr;
  bool ShouldHitEntity(void* e, int) override { return e != a && e != b; }
};

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

// True if any of the box's sample points projects onto the screen -- i.e. the
// entity is at least partly in the frame. Without this, the label clamp would
// drag an entirely off-screen entity's number onto a screen edge.
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
// class. Iterates the server entity list directly (independent of any harness
// session), matching the loop in EntitySnapshotter::Update.
ON_EVENT(RENDER) {
  // Marks are a property of the world, not the overlay: rebuild every frame so
  // EntityState.mark telemetry and macro mark-resolution stay correct even with
  // the visual overlay off. Self-guards on server/entityList; cheap (one
  // entity-list walk). Only the drawing below is gated on the cvar.
  markTable.RebuildFromWorld();

  if (!sar_harness_annotate.GetBool()) return;
  if (!server || !entityList) return;

  // Eye + camera-right vector, computed once. Used both for the LOS cull and to
  // place the left/right label candidates beside the box on screen.
  Vector eye;
  QAngle eyeAng;
  bool haveEye = camera && camera->GetEyePos<false>(GET_SLOT(), eye, eyeAng);
  Vector camRight{1, 0, 0};
  if (haveEye) Math::AngleVectors(eyeAng, nullptr, &camRight, nullptr);
  // The LOS cvar gates the cull only; the player is skipped by each trace so
  // the ray doesn't hit our own hull at its start (the eye sits inside it).
  bool los = sar_harness_annotate_los.GetBool();
  void* player = los ? server->GetPlayer(GET_SLOT() + 1) : nullptr;
  bool doCull = los && player && haveEye;

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
    Vector anchor = origin + Vector{0, 0, maxs.z};

    // Box is always drawn and depth-tested, so it occludes against world
    // geometry naturally -- you see the visible part, nothing flashes in/out.
    OverlayRender::addBoxMesh(
        origin, mins, maxs, angles,
        RenderCallback::constant({color.r, color.g, color.b, 5}),
        RenderCallback::constant(color));

    // No number for an entity that isn't in the frame (else the clamp drags an
    // off-screen entity's label onto a screen edge).
    if (!InFrame(origin, mins, maxs, angles)) continue;

    // The number is drawn on top (clamp_to_screen), so x-ray it only when the
    // entity is actually visible -- otherwise marks for entities behind walls
    // would float through. Sampling the whole box keeps a mostly-visible entity
    // from being hidden by a thin occluder crossing a single ray.
    if (doCull && !EntityVisible(eye, player, ent, origin, mins, maxs, angles))
      continue;

    // Mark label above the box. clamp_to_screen keeps the number in the
    // viewport (a box whose top is off-screen still shows its label) and on top
    // of geometry (no wall slicing).
    int mark =
        markTable.GetMark(i, static_cast<uint16_t>(info->m_SerialNumber));
    // Declutter candidates: the box bottom, then either side (offset along the
    // camera's right axis past the box) so a crowded label can move sideways
    // instead of only stacking.
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
}

// ---------------------------------------------------------------------------
// Recon: dump candidate "status" fields for puzzle entities. Diagnostic to find
// which engine field encodes each element's status (button pressed / door open
// / ...). Run twice -- before vs after a state change -- and diff the output to
// see which field flipped. Reads via getServerOffset, which resolves against
// both the datamap and the SendTable, so datamap-only fields (e.g.
// m_toggle_state on doors) show up even though the snapshotter would miss them.

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
    if (!className || kClassColors.find(className) == kClassColors.end())
      continue;

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
