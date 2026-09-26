using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RigidBodyBindings
{
    public delegate* unmanaged<IntPtr, float, float, float, float, float, float, int, void> applyForce;
    public delegate* unmanaged<IntPtr, float, float, float, void> setVelocity;
    public delegate* unmanaged<IntPtr, bool> isFalling;
    public delegate* unmanaged<IntPtr, bool> has;
    // New fields go at the END to keep the layout matched with the native RigidBodyBindings struct.
    public delegate* unmanaged<IntPtr, float, float, float, void> setAngularVelocity;
    public delegate* unmanaged<IntPtr, float*, float*, float*, void> getVelocity;
    public delegate* unmanaged<IntPtr, float*, float*, float*, void> getAngularVelocity;
    public delegate* unmanaged<IntPtr, float> getMass;
    public delegate* unmanaged<IntPtr, float, void> setMass;
    public delegate* unmanaged<IntPtr, float> getFriction;
    public delegate* unmanaged<IntPtr, float, void> setFriction;
    public delegate* unmanaged<IntPtr, float> getGravity;
    public delegate* unmanaged<IntPtr, float, void> setGravity;
    public delegate* unmanaged<IntPtr, bool> getDoGravity;
    public delegate* unmanaged<IntPtr, bool, void> setDoGravity;
}

// How RigidBody.applyForce turns a force into a change of velocity. Velocity is in units per tick, as getVelocity
// and setVelocity read and write it; a force or an acceleration acts for the tick's dt seconds, the way gravity
// does. The values must match the native ForceMode in RigidBody.h.
public enum ForceMode
{
    // Mass times units per tick, per second: the velocity changes by force * dt / mass.
    Force = 0,
    // Units per tick, per second: the velocity changes by acceleration * dt, whatever the mass.
    Acceleration = 1,
    // Mass times units per tick: the velocity changes by impulse / mass.
    Impulse = 2,
    // Units per tick, added to the velocity whatever the mass.
    VelocityChange = 3
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

    // Applied when physics next runs, this tick, at position in world space; off the center it turns the body too.
    public void applyForce(float x, float y, float z, float px, float py, float pz, ForceMode mode = ForceMode.Force) =>
        NativeBindings.RigidBody.applyForce(_uuid, x, y, z, px, py, pz, (int)mode);

    public void applyForce(Vector3 force, Vector3 position, ForceMode mode = ForceMode.Force)
        => applyForce(force.X, force.Y, force.Z, position.X, position.Y, position.Z, mode);

    public void setVelocity(float x, float y, float z) =>
        NativeBindings.RigidBody.setVelocity(_uuid, x, y, z);

    public void setVelocity(Vector3 velocity) => setVelocity(velocity.X, velocity.Y, velocity.Z);

    public void setAngularVelocity(float x, float y, float z) =>
        NativeBindings.RigidBody.setAngularVelocity(_uuid, x, y, z);

    public void setAngularVelocity(Vector3 angularVelocity) =>
        setAngularVelocity(angularVelocity.X, angularVelocity.Y, angularVelocity.Z);

    public bool isFalling() => NativeBindings.RigidBody.isFalling(_uuid);

    public Vector3 getVelocity()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.RigidBody.getVelocity(_uuid, &x, &y, &z);
        return new Vector3(x, y, z);
    }

    public Vector3 getAngularVelocity()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.RigidBody.getAngularVelocity(_uuid, &x, &y, &z);
        return new Vector3(x, y, z);
    }

    public float getMass() => NativeBindings.RigidBody.getMass(_uuid);

    public void setMass(float mass) => NativeBindings.RigidBody.setMass(_uuid, mass);

    public float getFriction() => NativeBindings.RigidBody.getFriction(_uuid);

    public void setFriction(float friction) => NativeBindings.RigidBody.setFriction(_uuid, friction);

    public float getGravity() => NativeBindings.RigidBody.getGravity(_uuid);

    public void setGravity(float gravity) => NativeBindings.RigidBody.setGravity(_uuid, gravity);

    public bool getDoGravity() => NativeBindings.RigidBody.getDoGravity(_uuid);

    public void setDoGravity(bool doGravity) => NativeBindings.RigidBody.setDoGravity(_uuid, doGravity);
}