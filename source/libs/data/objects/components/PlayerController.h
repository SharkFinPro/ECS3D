#ifndef PLAYERCONTROLLER_H
#define PLAYERCONTROLLER_H

#include "Component.h"
#include <cstdint>

// Marks an object as owned by a player. playerSlot is the player index the object belongs to; the
// server binds each connection to a slot (see ServerApp), so a script on this object reads that
// player's input via ScriptBase.input. The slot is a ComponentVariable so a script/spawner can rebind it
// at runtime and it resets on scene stop.
class PlayerController final : public Component {
public:
  PlayerController();

  [[nodiscard]] int32_t getPlayerSlot() const;
  void setPlayerSlot(int32_t playerSlot);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  ComponentVariable<int32_t> m_playerSlot{0};
};



#endif //PLAYERCONTROLLER_H
