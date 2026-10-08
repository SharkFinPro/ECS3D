using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;
using static ECS3DManagedTests.FakeNatives;

namespace ECS3DManagedTests;

internal static unsafe class FakeColliderNatives
{
  [UnmanagedCallersOnly]
  private static int ColliderGetShape(IntPtr u)
  {
    Rec("Collider.getShape", u);
    return Get<int>("Collider.getShape");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetIsTrigger(IntPtr u)
  {
    Rec("Collider.getIsTrigger", u);
    return Flag("Collider.getIsTrigger");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetIsTrigger(IntPtr u, byte value)
  {
    Rec("Collider.setIsTrigger", u, value);
    return Flag("Collider.setIsTrigger");
  }

  [UnmanagedCallersOnly]
  private static uint ColliderGetLayer(IntPtr u)
  {
    Rec("Collider.getLayer", u);
    return Get<uint>("Collider.getLayer");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetLayer(IntPtr u, uint value)
  {
    Rec("Collider.setLayer", u, value);
    return Flag("Collider.setLayer");
  }

  [UnmanagedCallersOnly]
  private static uint ColliderGetMask(IntPtr u)
  {
    Rec("Collider.getMask", u);
    return Get<uint>("Collider.getMask");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetMask(IntPtr u, uint value)
  {
    Rec("Collider.setMask", u, value);
    return Flag("Collider.setMask");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetBoxOffset(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Collider.getBoxOffset", u);
    OutVec3("Collider.getBoxOffset", x, y, z);
    return Flag("Collider.getBoxOffset.ok");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetBoxOffset(IntPtr u, float x, float y, float z)
  {
    Rec("Collider.setBoxOffset", u, x, y, z);
    return Flag("Collider.setBoxOffset");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetBoxSize(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Collider.getBoxSize", u);
    OutVec3("Collider.getBoxSize", x, y, z);
    return Flag("Collider.getBoxSize.ok");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetBoxSize(IntPtr u, float x, float y, float z)
  {
    Rec("Collider.setBoxSize", u, x, y, z);
    return Flag("Collider.setBoxSize");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetSphereOffset(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Collider.getSphereOffset", u);
    OutVec3("Collider.getSphereOffset", x, y, z);
    return Flag("Collider.getSphereOffset.ok");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetSphereOffset(IntPtr u, float x, float y, float z)
  {
    Rec("Collider.setSphereOffset", u, x, y, z);
    return Flag("Collider.setSphereOffset");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetSphereRadius(IntPtr u, float* r)
  {
    Rec("Collider.getSphereRadius", u);
    *r = Get<float>("Collider.getSphereRadius");
    return Flag("Collider.getSphereRadius.ok");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetSphereRadius(IntPtr u, float value)
  {
    Rec("Collider.setSphereRadius", u, value);
    return Flag("Collider.setSphereRadius");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderHas(IntPtr u)
  {
    Rec("Collider.has", u);
    return Flag("Collider.has");
  }

  internal static ColliderBindings Make() => new()
  {
    getShape = &ColliderGetShape,
    getIsTrigger =
      (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&ColliderGetIsTrigger,
    setIsTrigger =
      (delegate* unmanaged<IntPtr, bool, bool>)(void*)(delegate* unmanaged<IntPtr, byte, byte>)&ColliderSetIsTrigger,
    getLayer = &ColliderGetLayer,
    setLayer = (delegate* unmanaged<IntPtr, uint, bool>)(void*)
      (delegate* unmanaged<IntPtr, uint, byte>)&ColliderSetLayer,
    getMask = &ColliderGetMask,
    setMask = (delegate* unmanaged<IntPtr, uint, bool>)(void*)(delegate* unmanaged<IntPtr, uint, byte>)&ColliderSetMask,
    getBoxOffset = (delegate* unmanaged<IntPtr, float*, float*, float*, bool>)(void*)
      (delegate* unmanaged<IntPtr, float*, float*, float*, byte>)&ColliderGetBoxOffset,
    setBoxOffset = (delegate* unmanaged<IntPtr, float, float, float, bool>)(void*)
      (delegate* unmanaged<IntPtr, float, float, float, byte>)&ColliderSetBoxOffset,
    getBoxSize = (delegate* unmanaged<IntPtr, float*, float*, float*, bool>)(void*)
      (delegate* unmanaged<IntPtr, float*, float*, float*, byte>)&ColliderGetBoxSize,
    setBoxSize = (delegate* unmanaged<IntPtr, float, float, float, bool>)(void*)
      (delegate* unmanaged<IntPtr, float, float, float, byte>)&ColliderSetBoxSize,
    getSphereOffset = (delegate* unmanaged<IntPtr, float*, float*, float*, bool>)(void*)
      (delegate* unmanaged<IntPtr, float*, float*, float*, byte>)&ColliderGetSphereOffset,
    setSphereOffset = (delegate* unmanaged<IntPtr, float, float, float, bool>)(void*)
      (delegate* unmanaged<IntPtr, float, float, float, byte>)&ColliderSetSphereOffset,
    getSphereRadius = (delegate* unmanaged<IntPtr, float*, bool>)(void*)
      (delegate* unmanaged<IntPtr, float*, byte>)&ColliderGetSphereRadius,
    setSphereRadius = (delegate* unmanaged<IntPtr, float, bool>)(void*)
      (delegate* unmanaged<IntPtr, float, byte>)&ColliderSetSphereRadius,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&ColliderHas
  };
}
