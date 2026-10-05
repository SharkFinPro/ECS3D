using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;

namespace ECS3DManagedTests;

internal static unsafe partial class FakeNatives
{
  [UnmanagedCallersOnly]
  private static byte OpsHasComponent(IntPtr u, IntPtr type)
  {
    Rec("ComponentOps.hasComponent", u, type);
    return Flag("ComponentOps.hasComponent");
  }

  [UnmanagedCallersOnly]
  private static byte OpsAddComponent(IntPtr u, IntPtr type)
  {
    Rec("ComponentOps.addComponent", u, type);
    return Flag("ComponentOps.addComponent");
  }

  [UnmanagedCallersOnly]
  private static byte OpsRemoveComponent(IntPtr u, IntPtr type)
  {
    Rec("ComponentOps.removeComponent", u, type);
    return Flag("ComponentOps.removeComponent");
  }

  [UnmanagedCallersOnly]
  private static IntPtr OpsGetComponentTypes(IntPtr u)
  {
    Rec("ComponentOps.getComponentTypes", u);
    return Get<IntPtr>("ComponentOps.getComponentTypes");
  }

  private static ComponentOpsBindings MakeComponentOps() => new()
  {
    hasComponent = (delegate* unmanaged<IntPtr, IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, IntPtr, byte>)&OpsHasComponent,
    addComponent = (delegate* unmanaged<IntPtr, IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, IntPtr, byte>)&OpsAddComponent,
    removeComponent = (delegate* unmanaged<IntPtr, IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, IntPtr, byte>)&OpsRemoveComponent,
    getComponentTypes = &OpsGetComponentTypes
  };
}
