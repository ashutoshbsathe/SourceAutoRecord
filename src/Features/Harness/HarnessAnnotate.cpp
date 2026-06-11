#include "HarnessAnnotate.hpp"

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
#include "Features/EntityList.hpp"
#include "Features/OverlayRender.hpp"
#include "MarkTable.hpp"
#include "Modules/Console.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Utils/Memory.hpp"
#include "Utils/SDK/Class.hpp"
#include "Variable.hpp"

Variable sar_harness_annotate(
    "sar_harness_annotate", "0", 0, 1,
    "Draw wireframe annotation boxes around harness puzzle entities.\n");

// x_height of the mark label. Single digits, so keep it legible; tune freely.
static constexpr float kMarkHeight = 6.0f;

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

    // Mark label, just above the box top, depth-tested to match the boxes.
    // Neither depth flag is clean for a flat world-space quad: depth-tested
    // gets sliced by a wall it sits against, on-top x-rays marks through walls.
    // Real fix is an LOS predicate that culls marks for occluded entities.
    int mark =
        markTable.GetMark(i, static_cast<uint16_t>(info->m_SerialNumber));
    OverlayRender::addText(origin + Vector{0, 0, maxs.z}, std::to_string(mark),
                           kMarkHeight, /*visibility_scale*/ true,
                           /*no_depth*/ false, OverlayRender::TextAlign::BOTTOM,
                           color);
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
