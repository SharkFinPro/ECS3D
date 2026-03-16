using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RigidBodyBindings
{
  public delegate* unmanaged<IntPtr, float, float, float, float, float, float, void> applyForce;
  public delegate* unmanaged<IntPtr, float, float, float, void> setVelocity;
}