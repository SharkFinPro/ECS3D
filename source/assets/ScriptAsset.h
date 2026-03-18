#ifndef ECS3D_SCRIPTASSET_H
#define ECS3D_SCRIPTASSET_H

#include "Asset.h"

class Script;
class ScriptManager;

class ScriptAsset final : public Asset {
public:
  ScriptAsset(uuids::uuid uuid,
              std::string path,
              std::string className);

  void load() override;

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

  [[nodiscard]] std::shared_ptr<Script> createScript() const;

private:
  std::shared_ptr<ScriptManager> m_scriptManager = nullptr;

  std::string m_path;

  std::string m_className;
};


#endif //ECS3D_SCRIPTASSET_H