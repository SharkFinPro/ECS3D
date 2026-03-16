using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct TransformBindings
{
  internal delegate* unmanaged<float> getPositionX;
}