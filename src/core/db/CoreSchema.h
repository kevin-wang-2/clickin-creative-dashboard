#pragma once

#include "core/db/Migration.h"

namespace clickin {

// Returns the ordered list of Core-owned migrations.
// Plugins may register their own migrations via DatabaseService before startup.
std::vector<Migration> coreSchemaV1();

} // namespace clickin
