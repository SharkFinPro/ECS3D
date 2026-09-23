using System;
using System.Globalization;
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
    public delegate* unmanaged<float, float, float, float, float, float, float, uint, IntPtr, IntPtr> raycast;
    public delegate* unmanaged<float, float, float, float, uint, IntPtr, IntPtr> overlapSphere;
    public delegate* unmanaged<IntPtr, float, float, float, IntPtr> spawnPrefab;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct ComponentOpsBindings
{
    public delegate* unmanaged<IntPtr, IntPtr, bool> hasComponent;
    public delegate* unmanaged<IntPtr, IntPtr, bool> addComponent;
    public delegate* unmanaged<IntPtr, IntPtr, bool> removeComponent;
    public delegate* unmanaged<IntPtr, IntPtr> getComponentTypes;
}

// The result of a successful World.raycast: the object hit and where.
public struct RaycastHit
{
    public string objectUuid;
    public float distance;
    public Vector3 point;
    public Vector3 normal;
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

    public static bool tryGetCollider(string uuid, out Collider collider)
    {
        if (has(NativeBindings.Collider.has, uuid))
        {
            collider = new Collider(uuid);
            return true;
        }

        collider = null!;
        return false;
    }

    public static bool tryGetModelRenderer(string uuid, out ModelRenderer modelRenderer)
    {
        if (has(NativeBindings.ModelRenderer.has, uuid))
        {
            modelRenderer = new ModelRenderer(uuid);
            return true;
        }

        modelRenderer = null!;
        return false;
    }

    public static bool tryGetLightRenderer(string uuid, out LightRenderer lightRenderer)
    {
        if (has(NativeBindings.LightRenderer.has, uuid))
        {
            lightRenderer = new LightRenderer(uuid);
            return true;
        }

        lightRenderer = null!;
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
    // on connected clients after the current tick. Hand the returned uuid to tryGetTransform to drive it.
    // For an object with models/colliders/scripts, save it as a prefab and use spawnPrefab.
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

    // Spawn a prefab asset - its whole object subtree, with fresh uuids - with the root at the given
    // position, and return the root's uuid. Returns "" when prefabUuid isn't a registered prefab or its
    // body is malformed (the spawn is skipped and the server logs it; nothing throws). The prefab's asset
    // uuid is the one shown by the editor's asset browser.
    public static string spawnPrefab(string prefabUuid, float x, float y, float z)
    {
        var uuidPtr = Marshal.StringToCoTaskMemUTF8(prefabUuid);
        try
        {
            return Marshal.PtrToStringUTF8(NativeBindings.World.spawnPrefab(uuidPtr, x, y, z)) ?? "";
        }
        finally
        {
            Marshal.FreeCoTaskMem(uuidPtr);
        }
    }

    public static string spawnPrefab(string prefabUuid, Vector3 position)
        => spawnPrefab(prefabUuid, position.X, position.Y, position.Z);

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

    // Cast a ray against scene colliders. Returns true and fills `hit` with the nearest collider within
    // maxDistance whose layer is in layerMask; false (and hit = default) on a miss. The math runs on the
    // server via the sim query injected into BindingContext. Direction need not be normalized.
    public static bool raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit,
                               uint layerMask = 0xFFFFFFFFu, string ignoreUuid = "")
    {
        hit = default;

        var ignorePtr = Marshal.StringToCoTaskMemUTF8(ignoreUuid);
        string raw;
        try
        {
            // Native returns "uuid,dist,px,py,pz,nx,ny,nz" into its thread-local buffer, or "" on a miss;
            // marshal it out immediately (return ownership is native's) and parse. ignoreUuid (our arg,
            // our ownership) lets the caller exclude an object - used to skip self.
            raw = Marshal.PtrToStringUTF8(NativeBindings.World.raycast(
                origin.X, origin.Y, origin.Z, direction.X, direction.Y, direction.Z,
                maxDistance, layerMask, ignorePtr)) ?? "";
        }
        finally
        {
            Marshal.FreeCoTaskMem(ignorePtr);
        }

        if (raw.Length == 0)
        {
            return false;
        }

        var parts = raw.Split(',');
        if (parts.Length != 8)
        {
            return false;
        }

        // std::to_string writes with the C locale ('.' decimal), so parse invariant to match.
        hit = new RaycastHit
        {
            objectUuid = parts[0],
            distance = float.Parse(parts[1], CultureInfo.InvariantCulture),
            point = new Vector3(
                float.Parse(parts[2], CultureInfo.InvariantCulture),
                float.Parse(parts[3], CultureInfo.InvariantCulture),
                float.Parse(parts[4], CultureInfo.InvariantCulture)),
            normal = new Vector3(
                float.Parse(parts[5], CultureInfo.InvariantCulture),
                float.Parse(parts[6], CultureInfo.InvariantCulture),
                float.Parse(parts[7], CultureInfo.InvariantCulture))
        };
        return true;
    }

    // Every object whose collider overlaps the given sphere and whose layer is in layerMask. Hand a
    // returned uuid to tryGetTransform/tryGetRigidBody to act on it.
    public static string[] overlapSphere(Vector3 center, float radius, uint layerMask = 0xFFFFFFFFu,
                                         string ignoreUuid = "")
    {
        var ignorePtr = Marshal.StringToCoTaskMemUTF8(ignoreUuid);
        try
        {
            var raw = Marshal.PtrToStringUTF8(NativeBindings.World.overlapSphere(
                center.X, center.Y, center.Z, radius, layerMask, ignorePtr)) ?? "";

            return raw.Length == 0 ? Array.Empty<string>() : raw.Split(',');
        }
        finally
        {
            Marshal.FreeCoTaskMem(ignorePtr);
        }
    }

    // Generic component operations, keyed by the same type-name strings the editor's component list and
    // ComponentRegistry use ("RigidBody", "Box", "Sphere", "ModelRenderer", "LightRenderer",
    // "PlayerController", "Camera", "Transform"). Transform is queryable but not addable/removable (it is
    // structural); Script is not addressable through this API at all (adding one needs a class name).
    //
    // addComponent/removeComponent apply immediately - a hasComponent call later in the same tick already
    // reflects them - but replicate to clients via a full re-snapshot after the tick finishes, the same as
    // the editor's own structural edits, so a burst of calls in one tick costs one re-snapshot, not one
    // per call.
    public static bool hasComponent(string uuid, string componentType) =>
        withTwoStrings(NativeBindings.ComponentOps.hasComponent, uuid, componentType);

    // Fails safely (returns false, no change) for: an unknown uuid, an unrecognized type name, Transform,
    // Script, or a unique-per-type component the object already carries.
    public static bool addComponent(string uuid, string componentType) =>
        withTwoStrings(NativeBindings.ComponentOps.addComponent, uuid, componentType);

    // Fails safely (returns false, no change) for: an unknown uuid, an unrecognized type name, Transform,
    // Script, or a component the object does not currently carry.
    public static bool removeComponent(string uuid, string componentType) =>
        withTwoStrings(NativeBindings.ComponentOps.removeComponent, uuid, componentType);

    // Every component type name the object currently carries (Transform included, Script excluded, order
    // unspecified). Empty for an unknown uuid.
    public static string[] getComponentTypes(string uuid)
    {
        var uuidPtr = Marshal.StringToCoTaskMemUTF8(uuid);
        try
        {
            var raw = Marshal.PtrToStringUTF8(NativeBindings.ComponentOps.getComponentTypes(uuidPtr)) ?? "";
            return raw.Length == 0 ? Array.Empty<string>() : raw.Split(',');
        }
        finally
        {
            Marshal.FreeCoTaskMem(uuidPtr);
        }
    }

    private static bool withTwoStrings(delegate* unmanaged<IntPtr, IntPtr, bool> native, string a, string b)
    {
        var aPtr = Marshal.StringToCoTaskMemUTF8(a);
        var bPtr = Marshal.StringToCoTaskMemUTF8(b);
        try
        {
            return native(aPtr, bPtr);
        }
        finally
        {
            Marshal.FreeCoTaskMem(aPtr);
            Marshal.FreeCoTaskMem(bPtr);
        }
    }
}
