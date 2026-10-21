#ifndef DEFAULTWELCOME_H
#define DEFAULTWELCOME_H

#include <nlohmann/json_fwd.hpp>

// The Welcome scene of the built-in project: the objects as a serialized array, and the scene scripts as the
// serialized "scripts" array a scene carries.
[[nodiscard]] nlohmann::json buildWelcomeObjects();

[[nodiscard]] nlohmann::json buildWelcomeSceneScripts();

#endif //DEFAULTWELCOME_H
