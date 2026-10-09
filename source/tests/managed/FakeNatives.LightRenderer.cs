using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;
using static ECS3DManagedTests.FakeNatives;

namespace ECS3DManagedTests;

internal static unsafe class FakeLightRendererNatives
{
  [UnmanagedCallersOnly]
  private static byte LightGetIsSpotLight(IntPtr u)
  {
    Rec("LightRenderer.getIsSpotLight", u);
    return Flag("LightRenderer.getIsSpotLight");
  }

  [UnmanagedCallersOnly]
  private static void LightGetColor(IntPtr u, float* r, float* g, float* b)
  {
    Rec("LightRenderer.getColor", u);
    OutVec3("LightRenderer.getColor", r, g, b);
  }

  [UnmanagedCallersOnly]
  private static float LightGetAmbient(IntPtr u)
  {
    Rec("LightRenderer.getAmbient", u);
    return Get<float>("LightRenderer.getAmbient");
  }

  [UnmanagedCallersOnly]
  private static float LightGetDiffuse(IntPtr u)
  {
    Rec("LightRenderer.getDiffuse", u);
    return Get<float>("LightRenderer.getDiffuse");
  }

  [UnmanagedCallersOnly]
  private static float LightGetSpecular(IntPtr u)
  {
    Rec("LightRenderer.getSpecular", u);
    return Get<float>("LightRenderer.getSpecular");
  }

  [UnmanagedCallersOnly]
  private static void LightGetDirection(IntPtr u, float* x, float* y, float* z)
  {
    Rec("LightRenderer.getDirection", u);
    OutVec3("LightRenderer.getDirection", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static float LightGetConeAngle(IntPtr u)
  {
    Rec("LightRenderer.getConeAngle", u);
    return Get<float>("LightRenderer.getConeAngle");
  }

  [UnmanagedCallersOnly]
  private static void LightSetSpotLight(IntPtr u, byte value) => Rec("LightRenderer.setSpotLight", u, value);

  [UnmanagedCallersOnly]
  private static void LightSetColor(IntPtr u, float r, float g, float b) =>
    Rec("LightRenderer.setColor", u, r, g, b);

  [UnmanagedCallersOnly]
  private static void LightSetAmbient(IntPtr u, float value) => Rec("LightRenderer.setAmbient", u, value);

  [UnmanagedCallersOnly]
  private static void LightSetDiffuse(IntPtr u, float value) => Rec("LightRenderer.setDiffuse", u, value);

  [UnmanagedCallersOnly]
  private static void LightSetSpecular(IntPtr u, float value) => Rec("LightRenderer.setSpecular", u, value);

  [UnmanagedCallersOnly]
  private static void LightSetDirection(IntPtr u, float x, float y, float z) =>
    Rec("LightRenderer.setDirection", u, x, y, z);

  [UnmanagedCallersOnly]
  private static void LightSetConeAngle(IntPtr u, float value) => Rec("LightRenderer.setConeAngle", u, value);

  [UnmanagedCallersOnly]
  private static byte LightHas(IntPtr u)
  {
    Rec("LightRenderer.has", u);
    return Flag("LightRenderer.has");
  }

  internal static LightRendererBindings Make() => new()
  {
    getIsSpotLight =
      (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&LightGetIsSpotLight,
    getColor = &LightGetColor,
    getAmbient = &LightGetAmbient,
    getDiffuse = &LightGetDiffuse,
    getSpecular = &LightGetSpecular,
    getDirection = &LightGetDirection,
    getConeAngle = &LightGetConeAngle,
    setSpotLight = (delegate* unmanaged<IntPtr, bool, void>)(void*)
      (delegate* unmanaged<IntPtr, byte, void>)&LightSetSpotLight,
    setColor = &LightSetColor,
    setAmbient = &LightSetAmbient,
    setDiffuse = &LightSetDiffuse,
    setSpecular = &LightSetSpecular,
    setDirection = &LightSetDirection,
    setConeAngle = &LightSetConeAngle,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&LightHas
  };
}
