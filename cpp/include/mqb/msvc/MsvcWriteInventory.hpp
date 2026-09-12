#pragma once

#include "mqb/core/WriteInventory.hpp"
#include "mqb/msvc/MsvcCompiler.hpp"
#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"

namespace mqb::msvc {

// Pure additive view of known recipe outputs. Inputs/provider truth are NOT
// reinterpreted. Each native recipe retains an unresolved-side-effect entry:
// arbitrary parameters, environment, temp/shared-service writes and mutable
// ProcessSpec values prevent claiming these outputs are a complete write set.
// Discovery/toolchain caches and bootstrap/PCH creator files must be supplied
// separately by their owners; an unbuilt provider graph remains unresolved.
void append_known_writes(WriteInventory&, const MsvcCompileRecipe&);
void append_known_writes(WriteInventory&, const MsvcModuleScanRecipe&);
void append_known_writes(WriteInventory&, const MsvcLinkRecipe&);
void append_known_writes(WriteInventory&, const MsvcArchiveRecipe&);

} // namespace mqb::msvc
