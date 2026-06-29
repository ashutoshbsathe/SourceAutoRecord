#include "SidecarPanelSource.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "Utils/json11.hpp"
#include "Variable.hpp"

Variable sar_harness_panel_dir(
    "sar_harness_panel_dir", "",
    "Directory of offline portal-panel sidecars (<map>.json).\n");

static Vector ReadVec(const json11::Json& j) {
  const auto& a = j.array_items();
  if (a.size() < 3) return Vector{0, 0, 0};
  return Vector{(float)a[0].number_value(), (float)a[1].number_value(),
                (float)a[2].number_value()};
}

std::vector<PanelDesc> SidecarPanelSource::EnumeratePanels(
    const std::string& mapName) {
  std::vector<PanelDesc> out;
  std::string dir = sar_harness_panel_dir.GetString();
  if (dir.empty()) return out;

  std::string base = mapName;
  auto slash = base.find_last_of('/');
  if (slash != std::string::npos) base = base.substr(slash + 1);

  std::ifstream f(dir + "/" + base + ".json");
  if (!f) return out;
  std::stringstream ss;
  ss << f.rdbuf();

  std::string err;
  auto root = json11::Json::parse(ss.str(), err);
  if (!err.empty()) return out;

  for (const auto& p : root["panels"].array_items()) {
    PanelDesc d;
    d.mark = p["mark"].int_value();
    d.planeNormal = ReadVec(p["plane_normal"]);
    d.center = ReadVec(p["center"]);
    d.mins = ReadVec(p["mins"]);
    d.maxs = ReadVec(p["maxs"]);
    d.anchorFlags = p["anchor_flags"].int_value();
    out.push_back(d);
  }
  return out;
}
