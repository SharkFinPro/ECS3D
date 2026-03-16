using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RigidBodyBindings
{
    public delegate* unmanaged<IntPtr, float, float, float, float, float, float, void> applyForce;
    public delegate* unmanaged<IntPtr, float, float, float, void> setVelocity;
    public delegate* unmanaged<IntPtr, bool> isFalling;
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

    public void applyForce(Vector3 force, Vector3 position)
        => applyForce(force.X, force.Y, force.Z, position.X, position.Y, position.Z);

    public void setVelocity(float x, float y, float z) =>
        NativeBindings.RigidBody.setVelocity(_uuid, x, y, z);

    public void setVelocity(Vector3 velocity) => setVelocity(velocity.X, velocity.Y, velocity.Z);

    public bool isFalling() => NativeBindings.RigidBody.isFalling(_uuid);
}