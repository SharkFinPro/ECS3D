using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;
using static ECS3DManagedTests.FakeNatives;

namespace ECS3DManagedTests;

internal static unsafe class FakeCameraNatives
{
  [UnmanagedCallersOnly]
  private static void CameraGetDirection(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Camera.getDirection", u);
    OutVec3("Camera.getDirection", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static byte CameraHas(IntPtr u)
  {
    Rec("Camera.has", u);
    return Flag("Camera.has");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetDirection(IntPtr u, float x, float y, float z) =>
    Rec("Camera.setDirection", u, x, y, z);

  [UnmanagedCallersOnly]
  private static float CameraGetFov(IntPtr u)
  {
    Rec("Camera.getFov", u);
    return Get<float>("Camera.getFov");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetFov(IntPtr u, float value) => Rec("Camera.setFov", u, value);

  [UnmanagedCallersOnly]
  private static float CameraGetNearPlane(IntPtr u)
  {
    Rec("Camera.getNearPlane", u);
    return Get<float>("Camera.getNearPlane");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetNearPlane(IntPtr u, float value) => Rec("Camera.setNearPlane", u, value);

  [UnmanagedCallersOnly]
  private static float CameraGetFarPlane(IntPtr u)
  {
    Rec("Camera.getFarPlane", u);
    return Get<float>("Camera.getFarPlane");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetFarPlane(IntPtr u, float value) => Rec("Camera.setFarPlane", u, value);

  [UnmanagedCallersOnly]
  private static byte CameraIsActive(IntPtr u)
  {
    Rec("Camera.isActive", u);
    return Flag("Camera.isActive");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetActive(IntPtr u, byte value) => Rec("Camera.setActive", u, value);

  internal static CameraBindings Make() => new()
  {
    getDirection = &CameraGetDirection,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&CameraHas,
    setDirection = &CameraSetDirection,
    getFov = &CameraGetFov,
    setFov = &CameraSetFov,
    getNearPlane = &CameraGetNearPlane,
    setNearPlane = &CameraSetNearPlane,
    getFarPlane = &CameraGetFarPlane,
    setFarPlane = &CameraSetFarPlane,
    isActive = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&CameraIsActive,
    setActive =
      (delegate* unmanaged<IntPtr, bool, void>)(void*)(delegate* unmanaged<IntPtr, byte, void>)&CameraSetActive
  };
}
