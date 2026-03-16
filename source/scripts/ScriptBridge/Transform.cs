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
}