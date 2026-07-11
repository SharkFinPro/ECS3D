using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct CameraBindings
{
    public delegate* unmanaged<IntPtr, float*, float*, float*, void> getDirection;
    public delegate* unmanaged<IntPtr, bool> has;
}

// The object's camera, scoped to the object it was constructed from. Read-only for now: it
// exposes the look direction so a script can move relative to where the camera faces. Reads the forward
// default (0,0,-1) when the object has no Camera.
public sealed unsafe class Camera
{
    private readonly IntPtr _uuid;

    internal Camera(string uuid)
    {
        _uuid = Marshal.StringToCoTaskMemUTF8(uuid);
    }

    ~Camera()
    {
        Marshal.FreeCoTaskMem(_uuid);
    }

    public bool has() => NativeBindings.Camera.has(_uuid);

    // The camera's local look direction (object space); (0,0,-1) is the object's forward.
    public Vector3 getDirection()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.Camera.getDirection(_uuid, &x, &y, &z);
        return new Vector3(x, y, z);
    }
}
