#pragma once
#include <string>

#include "Features/Harness/harness.pb.h"
#include "VerbContext.hpp"

namespace verbs {
portal2_harness::MacroResult GoTo(const VerbContext& ctx,
                                  const std::string& target);
}  // namespace verbs
