#ifndef LIGHTRENDERERBINDINGS_H
#define LIGHTRENDERERBINDINGS_H

// LightRenderer script bindings: read/change how an object lights the scene (color, the three light
// strengths, whether it is a spot light, its spot direction/cone angle, and enable/disable). Mirrors the
// TransformBindings/ModelRendererBindings pattern: a C-ABI struct of function pointers, resolved against
// the server's current ObjectManager via BindingContext.
//
// vec3 getters use float* out-params the same way TransformBindings::getPosition does; vec3 setters take
// three floats the same way TransformBindings::setScale does.
//
// Every setter goes through the component's own setter, so its rules still apply here: a non-finite
// color/strength/direction is ignored, and coneAngle is clamped to [minConeAngleDegrees,
// maxConeAngleDegrees]. A successful set records the edit on BindingContext so the app can replicate it
// (LightRenderer isn't covered by the per-tick state delta, unlike Transform).
struct LightRendererBindings
{
  bool(*getIsSpotLight)(const char* uuid);
  void(*getColor)(const char* uuid, float* r, float* g, float* b);
  float(*getAmbient)(const char* uuid);
  float(*getDiffuse)(const char* uuid);
  float(*getSpecular)(const char* uuid);
  void(*getDirection)(const char* uuid, float* x, float* y, float* z);
  float(*getConeAngle)(const char* uuid);
  void(*setSpotLight)(const char* uuid, bool isSpotLight);
  void(*setColor)(const char* uuid, float r, float g, float b);
  void(*setAmbient)(const char* uuid, float ambient);
  void(*setDiffuse)(const char* uuid, float diffuse);
  void(*setSpecular)(const char* uuid, float specular);
  void(*setDirection)(const char* uuid, float x, float y, float z);
  void(*setConeAngle)(const char* uuid, float coneAngle);
  bool(*has)(const char* uuid);
};

class LightRendererBindingsProvider {
public:
  [[nodiscard]] static LightRendererBindings getBindings();

private:
  // find() resolves a uuid against the server's ObjectManager via BindingContext (set by ScriptSystem).
  static bool bindGetIsSpotLight(const char* uuid);
  static void bindGetColor(const char* uuid, float* r, float* g, float* b);
  static float bindGetAmbient(const char* uuid);
  static float bindGetDiffuse(const char* uuid);
  static float bindGetSpecular(const char* uuid);
  static void bindGetDirection(const char* uuid, float* x, float* y, float* z);
  static float bindGetConeAngle(const char* uuid);

  // Each setter defers its validation to the component's own setter (non-finite input, cone angle
  // clamping) and records the edit on BindingContext for replication when the component was found.
  static void bindSetSpotLight(const char* uuid, bool isSpotLight);
  static void bindSetColor(const char* uuid, float r, float g, float b);
  static void bindSetAmbient(const char* uuid, float ambient);
  static void bindSetDiffuse(const char* uuid, float diffuse);
  static void bindSetSpecular(const char* uuid, float specular);
  static void bindSetDirection(const char* uuid, float x, float y, float z);
  static void bindSetConeAngle(const char* uuid, float coneAngle);

  // Whether the object identified by uuid currently has a LightRenderer (backs World.tryGetLightRenderer).
  static bool bindHas(const char* uuid);
};



#endif //LIGHTRENDERERBINDINGS_H
