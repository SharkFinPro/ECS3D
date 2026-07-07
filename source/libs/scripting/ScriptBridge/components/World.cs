using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WorldBindings
{
    public delegate* unmanaged<IntPtr, IntPtr> findObjectByName;
    public delegate* unmanaged<IntPtr, IntPtr> getObjectName;
    public delegate* unmanaged<IntPtr, bool> objectExists;
    public delegate* unmanaged<IntPtr> getAllObjectUuids;
    public delegate* unmanaged<IntPtr, float, float, float, IntPtr> spawnObject;
    public delegate* unmanaged<IntPtr, void> destroyObject;
}

// World queries available to any user script: find objects, read names, and enumerate the scene.
// Returned uuids can be handed to a Transform/RigidBody wrapper to read/write another object's state.
public static unsafe class World
{
    public static string findObjectByName(string name)
    {
        var namePtr = Marshal.StringToCoTaskMemUTF8(name);
        try
        {
            // The native side returns a pointer into its own thread-local buffer; marshal it out
            // immediately and do not free it (argument ownership is ours, return ownership is native's).
            return Marshal.PtrToStringUTF8(NativeBindings.World.findObjectByName(namePtr)) ?? "";
        }
        finally
        {
            Marshal.FreeCoTaskMem(namePtr);
        }
    }

    public static string getObjectName(string uuid)
    {
        var uuidPtr = Marshal.StringToCoTaskMemUTF8(uuid);
        try
        {
            return Marshal.PtrToStringUTF8(NativeBindings.World.getObjectName(uuidPtr)) ?? "";
        }
        finally
        {
            Marshal.FreeCoTaskMem(uuidPtr);
        }
    }

    public static bool objectExists(string uuid)
    {
        var uuidPtr = Marshal.StringToCoTaskMemUTF8(uuid);
        try
        {
            return NativeBindings.World.objectExists(uuidPtr);
        }
        finally
        {
            Marshal.FreeCoTaskMem(uuidPtr);
        }
    }

    public static string[] getAllObjects()
    {
        var result = Marshal.PtrToStringUTF8(NativeBindings.World.getAllObjectUuids());
        if (string.IsNullOrEmpty(result))
        {
            return Array.Empty<string>();
        }

        return result.Split(',');
    }

    // Acquire another object's Transform. Returns false (and a null wrapper) when the object doesn't
    // exist or has no Transform, so scripts branch instead of operating on a dead handle. This is the
    // project-wide convention for reaching another object's components; every component wrapper follows
    // it. A handle that outlives its component (e.g. the object is destroyed later this tick) degrades
    // to safe no-op calls.
    public static bool tryGetTransform(string uuid, out Transform transform)
    {
        if (has(NativeBindings.Transform.has, uuid))
        {
            transform = new Transform(uuid);
            return true;
        }

        transform = null!;
        return false;
    }

    public static bool tryGetRigidBody(string uuid, out RigidBody rigidBody)
    {
        if (has(NativeBindings.RigidBody.has, uuid))
        {
            rigidBody = new RigidBody(uuid);
            return true;
        }

        rigidBody = null!;
        return false;
    }

    private static bool has(delegate* unmanaged<IntPtr, bool> nativeHas, string uuid)
    {
        var uuidPtr = Marshal.StringToCoTaskMemUTF8(uuid);
        try
        {
            return nativeHas(uuidPtr);
        }
        finally
        {
            Marshal.FreeCoTaskMem(uuidPtr);
        }
    }

    // Spawn a minimal object (a Transform at the given position) and return its uuid. The object appears
    // on connected clients after the current tick. Full prefab spawn (with model/scripts) arrives in
    // Phase 5; for now, hand the returned uuid to tryGetTransform to drive it.
    public static string spawn(string name, float x, float y, float z)
    {
        var namePtr = Marshal.StringToCoTaskMemUTF8(name);
        try
        {
            return Marshal.PtrToStringUTF8(NativeBindings.World.spawnObject(namePtr, x, y, z)) ?? "";
        }
        finally
        {
            Marshal.FreeCoTaskMem(namePtr);
        }
    }

    public static string spawn(string name, Vector3 position) => spawn(name, position.X, position.Y, position.Z);

    // Destroy an object at runtime. Safe if the uuid is unknown/already gone (no-op). The removal takes
    // effect at the end of the tick (mark-for-deletion), so a wrapper held for this uuid degrades to
    // safe no-op calls afterward.
    public static void destroy(string uuid)
    {
        var uuidPtr = Marshal.StringToCoTaskMemUTF8(uuid);
        try
        {
            NativeBindings.World.destroyObject(uuidPtr);
        }
        finally
        {
            Marshal.FreeCoTaskMem(uuidPtr);
        }
    }
}
