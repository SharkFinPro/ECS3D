using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;

namespace ECS3DManagedTests;

internal static unsafe partial class FakeNatives
{
  [UnmanagedCallersOnly]
  private static void TransformGetPosition(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getPosition", u);
    OutVec3("Transform.getPosition", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformGetScale(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getScale", u);
    OutVec3("Transform.getScale", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformGetRotation(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getRotation", u);
    OutVec3("Transform.getRotation", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformSetScale(IntPtr u, float x, float y, float z) =>
    Rec("Transform.setScale", u, x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformSetRotation(IntPtr u, float x, float y, float z) =>
    Rec("Transform.setRotation", u, x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformMove(IntPtr u, float x, float y, float z) => Rec("Transform.move", u, x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformStart(IntPtr u) => Rec("Transform.start", u);

  [UnmanagedCallersOnly]
  private static void TransformStop(IntPtr u) => Rec("Transform.stop", u);

  [UnmanagedCallersOnly]
  private static byte TransformHas(IntPtr u)
  {
    Rec("Transform.has", u);
    return Flag("Transform.has");
  }

  [UnmanagedCallersOnly]
  private static void TransformGetLocalPosition(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getLocalPosition", u);
    OutVec3("Transform.getLocalPosition", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformGetLocalScale(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getLocalScale", u);
    OutVec3("Transform.getLocalScale", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformGetLocalRotation(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getLocalRotation", u);
    OutVec3("Transform.getLocalRotation", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformSetPosition(IntPtr u, float x, float y, float z) =>
    Rec("Transform.setPosition", u, x, y, z);

  private static TransformBindings MakeTransform() => new()
  {
    getPosition = &TransformGetPosition,
    getScale = &TransformGetScale,
    getRotation = &TransformGetRotation,
    setScale = &TransformSetScale,
    setRotation = &TransformSetRotation,
    move = &TransformMove,
    start = &TransformStart,
    stop = &TransformStop,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&TransformHas,
    getLocalPosition = &TransformGetLocalPosition,
    getLocalScale = &TransformGetLocalScale,
    getLocalRotation = &TransformGetLocalRotation,
    setPosition = &TransformSetPosition
  };
}
