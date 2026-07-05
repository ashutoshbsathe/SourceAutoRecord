#include "GoTo.hpp"

#include "Features/Harness/MacroExecutor.hpp"

// TODO: migrate the remaining verbs (aim_at/look/move/pick_up/release/interact/
// place_portal/interpose/redirect_to/pass_through/jump_into/drop_into) into
// verbs/ so MacroExecutor holds only dispatch.
namespace verbs {
portal2_harness::MacroResult GoTo(const VerbContext& ctx,
                                  const std::string& target) {
  return ctx.exec->GoTo(target);
}
}  // namespace verbs
