using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct CameraBindings
{
    public delegate* unmanaged<IntPtr, float*, float*, float*, void> getDirection;
    public delegate* unmanaged<IntPtr, bool> has;
    // New fields go at the END to keep the layout matched with the native CameraBindings struct.
    public delegate* unmanaged<IntPtr, float, float, float, void> setDirection;
    public delegate* unmanaged<IntPtr, float> getFov;
    public delegate* unmanaged<IntPtr, float, void> setFov;
    public delegate* unmanaged<IntPtr, float> getNearPlane;
    public delegate* unmanaged<IntPtr, float, void> setNearPlane;
    public delegate* unmanaged<IntPtr, float> getFarPlane;
    public delegate* unmanaged<IntPtr, float, void> setFarPlane;
    public delegate* unmanaged<IntPtr, bool> isActive;
    public delegate* unmanaged<IntPtr, bool, void> setActive;
}

// The object's camera, scoped to the object it was constructed from. Exposes the look direction and the
// projection/activation params so a script can move relative to where the camera faces or tune how it
// renders. Reads the forward default (0,0,-1) when the object has no Camera.
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

    public void setDirection(float x, float y, float z) => NativeBindings.Camera.setDirection(_uuid, x, y, z);

    public void setDirection(Vector3 direction) => setDirection(direction.X, direction.Y, direction.Z);

    public float getFov() => NativeBindings.Camera.getFov(_uuid);

    public void setFov(float fov) => NativeBindings.Camera.setFov(_uuid, fov);

    public float getNearPlane() => NativeBindings.Camera.getNearPlane(_uuid);

    public void setNearPlane(float nearPlane) => NativeBindings.Camera.setNearPlane(_uuid, nearPlane);

    public float getFarPlane() => NativeBindings.Camera.getFarPlane(_uuid);

    public void setFarPlane(float farPlane) => NativeBindings.Camera.setFarPlane(_uuid, farPlane);

    public bool isActive() => NativeBindings.Camera.isActive(_uuid);

    public void setActive(bool active) => NativeBindings.Camera.setActive(_uuid, active);
}
