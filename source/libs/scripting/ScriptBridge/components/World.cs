using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WorldBindings
{
    public delegate* unmanaged<IntPtr, IntPtr> findObjectByName;
    public delegate* unmanaged<IntPtr, IntPtr> getObjectName;
    public delegate* unmanaged<IntPtr, bool> objectExists;
    public delegate* unmanaged<IntPtr> getAllObjectUuids;
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
}
