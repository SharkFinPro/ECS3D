using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct TransformBindings
{
  public delegate* unmanaged<IntPtr, float*, float*, float*, void> getPosition;
  public delegate* unmanaged<IntPtr, float*, float*, float*, void> getScale;
  public delegate* unmanaged<IntPtr, float*, float*, float*, void> getRotation;
  public delegate* unmanaged<IntPtr, float, float, float, void> setScale;
  public delegate* unmanaged<IntPtr, float, float, float, void> setRotation;
  public delegate* unmanaged<IntPtr, float, float, float, void> move;
}