using System;
using System.Numerics;
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
    public delegate* unmanaged<IntPtr, void> start;
    public delegate* unmanaged<IntPtr, void> stop;
    public delegate* unmanaged<IntPtr, bool> has;
    // New fields go at the END to keep the layout matched with the native TransformBindings struct.
    public delegate* unmanaged<IntPtr, float*, float*, float*, void> getLocalPosition;
    public delegate* unmanaged<IntPtr, float*, float*, float*, void> getLocalScale;
    public delegate* unmanaged<IntPtr, float*, float*, float*, void> getLocalRotation;
    public delegate* unmanaged<IntPtr, float, float, float, void> setPosition;
}

public unsafe class Transform
{
    private readonly IntPtr _uuid;

    internal Transform(string uuid)
    {
        _uuid = Marshal.StringToCoTaskMemUTF8(uuid);
    }

    ~Transform()
    {
        Marshal.FreeCoTaskMem(_uuid);
    }

    public Vector3 getPosition()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.Transform.getPosition(_uuid, &x, &y, &z);

        return new Vector3(x, y, z);
    }

    public Vector3 getScale()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.Transform.getScale(_uuid, &x, &y, &z);

        return new Vector3(x, y, z);
    }

    public Vector3 getRotation()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.Transform.getRotation(_uuid, &x, &y, &z);

        return new Vector3(x, y, z);
    }

    public void setScale(float x, float y, float z) =>
        NativeBindings.Transform.setScale(_uuid, x, y, z);

    public void setRotation(float x, float y, float z) =>
        NativeBindings.Transform.setRotation(_uuid, x, y, z);

    public void move(float x, float y, float z) =>
        NativeBindings.Transform.move(_uuid, x, y, z);

    public void start() => NativeBindings.Transform.start(_uuid);

    public void stop() => NativeBindings.Transform.stop(_uuid);

    // getPosition/getScale/getRotation are parent-combined (world); these read this object's own local
    // values.
    public Vector3 getLocalPosition()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.Transform.getLocalPosition(_uuid, &x, &y, &z);

        return new Vector3(x, y, z);
    }

    public Vector3 getLocalScale()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.Transform.getLocalScale(_uuid, &x, &y, &z);

        return new Vector3(x, y, z);
    }

    public Vector3 getLocalRotation()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.Transform.getLocalRotation(_uuid, &x, &y, &z);

        return new Vector3(x, y, z);
    }

    // Overwrites the local position outright (setScale/setRotation's sibling); move() is additive.
    public void setPosition(float x, float y, float z) =>
        NativeBindings.Transform.setPosition(_uuid, x, y, z);

    public void setPosition(Vector3 position) => setPosition(position.X, position.Y, position.Z);
}