using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;

namespace ECS3DManagedTests;

internal static unsafe partial class FakeNatives
{
  [UnmanagedCallersOnly]
  private static IntPtr WorldFindObjectByName(IntPtr name)
  {
    Rec("World.findObjectByName", name);
    return Get<IntPtr>("World.findObjectByName");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldGetObjectName(IntPtr u)
  {
    Rec("World.getObjectName", u);
    return Get<IntPtr>("World.getObjectName");
  }

  [UnmanagedCallersOnly]
  private static byte WorldObjectExists(IntPtr u)
  {
    Rec("World.objectExists", u);
    return Flag("World.objectExists");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldGetAllObjectUuids()
  {
    Rec("World.getAllObjectUuids");
    return Get<IntPtr>("World.getAllObjectUuids");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldSpawnObject(IntPtr name, float x, float y, float z)
  {
    Rec("World.spawnObject", name, x, y, z);
    return Get<IntPtr>("World.spawnObject");
  }

  [UnmanagedCallersOnly]
  private static void WorldDestroyObject(IntPtr u) => Rec("World.destroyObject", u);

  // #lizard forgives
  [UnmanagedCallersOnly]
  [System.Diagnostics.CodeAnalysis.SuppressMessage("Major Code Smell", "S107",
    Justification = "Mirrors the native raycast signature.")]
  private static IntPtr WorldRaycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance,
                                     uint layerMask, IntPtr ignore)
  {
    Rec("World.raycast", ox, oy, oz, dx, dy, dz, maxDistance, layerMask, ignore);
    return Get<IntPtr>("World.raycast");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldOverlapSphere(float x, float y, float z, float radius, uint layerMask, IntPtr ignore)
  {
    Rec("World.overlapSphere", x, y, z, radius, layerMask, ignore);
    return Get<IntPtr>("World.overlapSphere");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldSpawnPrefab(IntPtr prefab, float x, float y, float z)
  {
    Rec("World.spawnPrefab", prefab, x, y, z);
    return Get<IntPtr>("World.spawnPrefab");
  }

  private static WorldBindings MakeWorld() => new()
  {
    findObjectByName = &WorldFindObjectByName,
    getObjectName = &WorldGetObjectName,
    objectExists = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&WorldObjectExists,
    getAllObjectUuids = &WorldGetAllObjectUuids,
    spawnObject = &WorldSpawnObject,
    destroyObject = &WorldDestroyObject,
    raycast = &WorldRaycast,
    overlapSphere = &WorldOverlapSphere,
    spawnPrefab = &WorldSpawnPrefab
  };
}
