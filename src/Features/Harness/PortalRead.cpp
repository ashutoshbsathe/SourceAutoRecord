#include "PortalRead.hpp"

#include <cstring>

#include "Entity.hpp"
#include "Features/EntityList.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Utils/SDK/EntityEdict.hpp"

LivePortal ReadPortal(bool orange) {
  LivePortal p;
  if (!server || !entityList) return p;
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    void* ent = info->m_pEntity;
    const char* cn = server->GetEntityClassName(ent);
    if (!cn || std::strcmp(cn, "prop_portal")) continue;
    auto se = SE(ent);
    if (!se->field<bool>("m_bActivated")) continue;
    if (se->field<bool>("m_bIsPortal2") != orange) continue;
    p.ent = ent;
    p.active = true;
    p.center = server->GetAbsOrigin(ent);
    Math::AngleVectors(se->abs_angles(), &p.normal);
    p.linked = entityList->LookupEntity(
                   se->field<CBaseHandle>("m_hLinkedPortal")) != nullptr;
    break;
  }
  return p;
}
