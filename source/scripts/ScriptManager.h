#ifndef ECS3D_SCRIPTMANAGER_H
#define ECS3D_SCRIPTMANAGER_H

#include "ScriptEngine.h"
#include <uuid.h>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class ECS3D;

class ScriptManager {
public:
  explicit ScriptManager(ECS3D* ecs);

  void checkForScriptChanges(float dt);

  [[nodiscard]] bool isScriptAttached(uuids::uuid uuid,
                                      const char* className) const;

  void attachScript(uuids::uuid uuid,
                    const char* className,
                    const std::function<void()> &preReload,
                    const std::function<void()> &postReload);

  void detachScript(uuids::uuid uuid,
                    const char* className);

  void fixedUpdate(uuids::uuid uuid,
                   const char* className,
                   float dt) const;

  void variableUpdate(uuids::uuid uuid,
                      const char* className) const;

  struct ExposedField {
    std::string name;
    std::string displayName;
    std::string type;
  };

  const std::vector<ExposedField>* getExposedFields(uuids::uuid uuid,
                                                    const char* className) const;

  float getFieldFloat(uuids::uuid uuid,
                      const char* className,
                      const char* fieldName) const;

  int getFieldInt(uuids::uuid uuid,
                  const char* className,
                  const char* fieldName) const;

  bool getFieldBool(uuids::uuid uuid,
                    const char* className,
                    const char* fieldName) const;

  void setFieldFloat(uuids::uuid uuid,
                     const char* className,
                     const char* fieldName,
                     float value) const;

  void setFieldInt(uuids::uuid uuid,
                   const char* className,
                   const char* fieldName,
                   int value) const;

  void setFieldBool(uuids::uuid uuid,
                    const char* className,
                    const char* fieldName,
                    bool value) const;

private:
  ECS3D* m_ecs;
  ScriptEngine m_scriptEngine;

  struct ReloadCallbacks {
    std::function<void()> preReload;
    std::function<void()> postReload;
  };

  std::unordered_map<std::string, ReloadCallbacks> m_attachedScripts;

  using ScriptsSnapshot = std::unordered_map<std::string, std::filesystem::file_time_type>;

  ScriptsSnapshot m_scriptsSnapshot;

  float m_timeSinceLastSnapshot = 0.0f;

  mutable std::unordered_map<std::string, std::vector<ExposedField>> m_fieldCache;

  [[nodiscard]] static ScriptsSnapshot takeSnapshot();

  static std::string cacheKey(uuids::uuid uuid, const char* className);
};


#endif //ECS3D_SCRIPTMANAGER_H