#ifndef SCRIPTRUNTIME_H
#define SCRIPTRUNTIME_H

#include <string>

namespace scripting {
  inline const std::string defaultUserScriptsDir = "scripts/UserScripts";
}

// What ScriptSystem needs from the managed script host: per-instance lifecycle calls and the
// exposed-field accessors. Behind an interface so ScriptSystem's bookkeeping can run against a fake
// without booting the CLR. An instance handed to ScriptSystem is already initialized.
class ScriptRuntime {
public:
  virtual ~ScriptRuntime() = default;

  virtual void reloadScripts() const = 0;

  // True when a managed instance now exists under the pair; false when the class is missing or its
  // constructor threw.
  [[nodiscard]] virtual bool attachScript(const char* uuid,
                                          const char* className) const = 0;

  // True when an instance exists under the pair and has not faulted.
  [[nodiscard]] virtual bool isHealthy(const char* uuid,
                                       const char* className) const = 0;

  virtual void detachScript(const char* uuid,
                            const char* className) const = 0;

  virtual void start(const char* uuid,
                     const char* className) const = 0;

  virtual void stop(const char* uuid,
                    const char* className) const = 0;

  virtual void fixedUpdate(const char* uuid,
                           const char* className,
                           float dt) const = 0;

  virtual void variableUpdate(const char* uuid,
                              const char* className) const = 0;

  // event matches CollisionEvent in ScriptSystem.h (0 = enter, 1 = stay, 2 = exit).
  virtual void onCollision(const char* uuid,
                           const char* className,
                           const char* otherUuid,
                           int event) const = 0;

  [[nodiscard]] virtual std::string getExposedFields(const char* uuid,
                                                     const char* className) const = 0;

  [[nodiscard]] virtual float getFieldFloat(const char* uuid,
                                            const char* className,
                                            const char* fieldName) const = 0;

  [[nodiscard]] virtual int getFieldInt(const char* uuid,
                                        const char* className,
                                        const char* fieldName) const = 0;

  [[nodiscard]] virtual bool getFieldBool(const char* uuid,
                                          const char* className,
                                          const char* fieldName) const = 0;

  virtual void getFieldVector3(const char* uuid,
                               const char* className,
                               const char* fieldName,
                               float& x, float& y, float& z) const = 0;

  virtual void setFieldFloat(const char* uuid,
                             const char* className,
                             const char* fieldName,
                             float value) const = 0;

  virtual void setFieldInt(const char* uuid,
                           const char* className,
                           const char* fieldName,
                           int value) const = 0;

  virtual void setFieldBool(const char* uuid,
                            const char* className,
                            const char* fieldName,
                            bool value) const = 0;

  virtual void setFieldVector3(const char* uuid,
                               const char* className,
                               const char* fieldName,
                               float x, float y, float z) const = 0;
};

#endif //SCRIPTRUNTIME_H
