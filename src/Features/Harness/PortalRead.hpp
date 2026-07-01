#pragma once
#include "Utils/SDK/Math.hpp"

// The live placed portal of a color (orange = secondary), read from the server
// entity list; active=false if none exists. Gated on m_bActivated so the portal
// gun's inactive ghost portals are ignored. center/normal come from the real
// face (server abs origin + angles), valid even on the settle frame where a
// portal's own abs_origin momentarily reads zero.
struct LivePortal {
  void* ent = nullptr;
  bool active = false;
  Vector center{0, 0, 0};
  Vector normal{0, 0, 0};
  bool linked = false;  // partner portal resolves
};

LivePortal ReadPortal(bool orange);
