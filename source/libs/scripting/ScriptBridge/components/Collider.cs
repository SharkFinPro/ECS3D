using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct ColliderBindings
{
    public delegate* unmanaged<IntPtr, int> getShape;

    public delegate* unmanaged<IntPtr, bool> getIsTrigger;
    public delegate* unmanaged<IntPtr, bool, bool> setIsTrigger;

    public delegate* unmanaged<IntPtr, uint> getLayer;
    public delegate* unmanaged<IntPtr, uint, bool> setLayer;

    public delegate* unmanaged<IntPtr, uint> getMask;
    public delegate* unmanaged<IntPtr, uint, bool> setMask;

    public delegate* unmanaged<IntPtr, float*, float*, float*, bool> getBoxOffset;
    public delegate* unmanaged<IntPtr, float, float, float, bool> setBoxOffset;
    public delegate* unmanaged<IntPtr, float*, float*, float*, bool> getBoxSize;
    public delegate* unmanaged<IntPtr, float, float, float, bool> setBoxSize;

    public delegate* unmanaged<IntPtr, float*, float*, float*, bool> getSphereOffset;
    public delegate* unmanaged<IntPtr, float, float, float, bool> setSphereOffset;
    public delegate* unmanaged<IntPtr, float*, bool> getSphereRadius;
    public delegate* unmanaged<IntPtr, float, bool> setSphereRadius;

    public delegate* unmanaged<IntPtr, bool> has;
}

// A collider's shape (backed by ColliderBindings.getShape) so a script can branch without knowing the
// concrete type up front, plus the shape-shared trigger/layer/mask fields and per-shape shape/offset
// accessors. New shapes (capsule, convex mesh) add their own group of accessors the same way, without
// touching ColliderShape or the common members here.
public enum ColliderShape
{
    None = 0,
    Box = 1,
    Sphere = 2
}

// Reached via World.tryGetCollider like Transform/RigidBody. getShape() names the concrete shape; the
// box-only and sphere-only accessors below fail safely (false / neutral default, nothing changed) when
// called on the wrong shape - check getShape() first, or just take the false and move on.
public unsafe class Collider
{
    private readonly IntPtr _uuid;

    internal Collider(string uuid)
    {
        _uuid = Marshal.StringToCoTaskMemUTF8(uuid);
    }

    ~Collider()
    {
        Marshal.FreeCoTaskMem(_uuid);
    }

    public ColliderShape getShape() => (ColliderShape)NativeBindings.Collider.getShape(_uuid);

    public bool getIsTrigger() => NativeBindings.Collider.getIsTrigger(_uuid);

    public bool setIsTrigger(bool isTrigger) => NativeBindings.Collider.setIsTrigger(_uuid, isTrigger);

    public uint getLayer() => NativeBindings.Collider.getLayer(_uuid);

    public bool setLayer(uint layer) => NativeBindings.Collider.setLayer(_uuid, layer);

    public uint getMask() => NativeBindings.Collider.getMask(_uuid);

    public bool setMask(uint mask) => NativeBindings.Collider.setMask(_uuid, mask);

    public bool tryGetBoxOffset(out Vector3 offset)
    {
        float x = 0, y = 0, z = 0;
        var ok = NativeBindings.Collider.getBoxOffset(_uuid, &x, &y, &z);
        offset = ok ? new Vector3(x, y, z) : Vector3.Zero;
        return ok;
    }

    public bool setBoxOffset(float x, float y, float z) => NativeBindings.Collider.setBoxOffset(_uuid, x, y, z);

    public bool setBoxOffset(Vector3 offset) => setBoxOffset(offset.X, offset.Y, offset.Z);

    public bool tryGetBoxSize(out Vector3 size)
    {
        float x = 0, y = 0, z = 0;
        var ok = NativeBindings.Collider.getBoxSize(_uuid, &x, &y, &z);
        size = ok ? new Vector3(x, y, z) : Vector3.Zero;
        return ok;
    }

    public bool setBoxSize(float x, float y, float z) => NativeBindings.Collider.setBoxSize(_uuid, x, y, z);

    public bool setBoxSize(Vector3 size) => setBoxSize(size.X, size.Y, size.Z);

    public bool tryGetSphereOffset(out Vector3 offset)
    {
        float x = 0, y = 0, z = 0;
        var ok = NativeBindings.Collider.getSphereOffset(_uuid, &x, &y, &z);
        offset = ok ? new Vector3(x, y, z) : Vector3.Zero;
        return ok;
    }

    public bool setSphereOffset(float x, float y, float z) => NativeBindings.Collider.setSphereOffset(_uuid, x, y, z);

    public bool setSphereOffset(Vector3 offset) => setSphereOffset(offset.X, offset.Y, offset.Z);

    public bool tryGetSphereRadius(out float radius)
    {
        float r = 0;
        var ok = NativeBindings.Collider.getSphereRadius(_uuid, &r);
        radius = ok ? r : 0f;
        return ok;
    }

    public bool setSphereRadius(float radius) => NativeBindings.Collider.setSphereRadius(_uuid, radius);
}
