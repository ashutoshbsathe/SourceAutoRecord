#include "BspFilePanelSource.hpp"
#include "Command.hpp"
#include "Event.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "SurfaceMarkTable.hpp"

// Enumerates panels by parsing the map's .bsp at session start.
static BspFilePanelSource g_panelSource;

ON_EVENT(SESSION_START) {
  surfaceMarkTable.RebuildFromSource(g_panelSource,
                                     engine->GetCurrentMapName());
}

CON_COMMAND(
    sar_harness_panels_dump,
    "sar_harness_panels_dump - list the loaded portal-surface panels.\n") {
  auto panels = surfaceMarkTable.Panels();
  for (const auto& p : panels) {
    console->Msg("  S%d  center %.1f %.1f %.1f  normal %.2f %.2f %.2f\n",
                 p.mark, p.center.x, p.center.y, p.center.z, p.planeNormal.x,
                 p.planeNormal.y, p.planeNormal.z);
  }
  console->Print("panels: %d\n", (int)panels.size());
}
