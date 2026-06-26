#ifndef DEFAULTPROJECT_H
#define DEFAULTPROJECT_H

#include <nlohmann/json_fwd.hpp>

// Builds the built-in sample project as a project blob (the same JSON shape ProjectSerializer loads,
// so it can just be deserialize()'d). Scene 3 and the falling-balls scene are generated procedurally
// (random placement), which a static file can't capture.
[[nodiscard]] nlohmann::json buildDefaultProject();

#endif //DEFAULTPROJECT_H
