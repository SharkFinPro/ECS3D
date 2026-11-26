#ifndef WORLDUNITS_H
#define WORLDUNITS_H

// The engine's content is authored at this many world units per meter. Authoring tools convert a size entered in
// meters with it; the simulation reads it through PhysicsSystem::unitsPerMeter.
inline constexpr float worldUnitsPerMeter = 5.0f;

#endif //WORLDUNITS_H
