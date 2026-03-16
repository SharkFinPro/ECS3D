using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RigidBodyBindings
{
    public delegate* unmanaged<IntPtr, float, float, float, float, float, float, void> applyForce;
    public delegate* unmanaged<IntPtr, float, float, float, void> setVelocity;
}

public unsafe class RigidBody
{
    private readonly IntPtr _uuid;

    internal RigidBody(string uuid)
    {
        _uuid = Marshal.StringToCoTaskMemUTF8(uuid);
    }

    ~RigidBody()
    {
        Marshal.FreeCoTaskMem(_uuid);
    }

    public void applyForce(float x, float y, float z, float px, float py, float pz) =>
        NativeBindings.RigidBody.applyForce(_uuid, x, y, z, px, py, pz);

    public void setVelocity(float x, float y, float z) =>
        NativeBindings.RigidBody.setVelocity(_uuid, x, y, z);
}