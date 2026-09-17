#ifndef SCRIPTFIELDEDIT_H
#define SCRIPTFIELDEDIT_H

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace scripting {
  // Why one field of a script edit cannot be written to the live instance. Empty means it can.
  // cachedType is the type the instance actually exposes for that field name, or nullptr when the
  // instance exposes no such field. Kept free of any CLR dependency so it can be tested headless.
  [[nodiscard]] std::string rejectFieldEdit(const nlohmann::json& field, const std::string* cachedType);
}

#endif //SCRIPTFIELDEDIT_H
