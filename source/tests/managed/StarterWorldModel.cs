using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// A small stand-in scene for the world-level starters: named objects with positions, prefab spawns that mint
// uuids ("spawned-1", ...), and destroys. It replaces FakeNatives' World and Transform tables, which answer
// every uuid alike, and restores them on dispose. Log writes are captured, level 3 being a warning.
internal sealed unsafe class WorldModel : IDisposable
{
  private static WorldModel? s_current;

  private readonly List<string> _order = new();
  private readonly Dictionary<string, string> _names = new();
  private readonly Dictionary<string, Vector3> _positions = new();
  private readonly List<IntPtr> _strings = new();
  private readonly WorldBindings _world = NativeBindings.World;
  private readonly TransformBindings _transform = NativeBindings.Transform;
  private readonly LogBindings _log = NativeBindings.Log;
  private int _spawnCount;

  internal readonly List<(string Prefab, Vector3 At)> Spawns = new();
  internal readonly List<string> Destroyed = new();
  internal readonly List<(string Uuid, string Name)> Renames = new();
  internal readonly List<(string Uuid, Vector3 To)> Moves = new();
  internal readonly List<(int Level, string Message)> LogCalls = new();

  internal readonly List<string> Failures = new();

  internal bool FailSpawns;

  private static void Activate(WorldModel? model) => s_current = model;

  internal WorldModel()
  {
    Activate(this);
    NativeBindings.World = new WorldBindings
    {
      findObjectByName = &FindObjectByName,
      getObjectName = &GetObjectName,
      objectExists = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&ObjectExists,
      getAllObjectUuids = &GetAllObjectUuids,
      spawnObject = &SpawnObject,
      destroyObject = &DestroyObject,
      raycast = &Raycast,
      overlapSphere = &OverlapSphere,
      spawnPrefab = &SpawnPrefab,
      setObjectName = (delegate* unmanaged<IntPtr, IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, IntPtr, byte>)&SetObjectName
    };
    NativeBindings.Transform = new TransformBindings
    {
      getPosition = &GetPosition,
      setPosition = &SetPosition,
      getScale = &FailGetter,
      getRotation = &FailGetter,
      getLocalPosition = &FailGetter,
      getLocalScale = &FailGetter,
      getLocalRotation = &FailGetter,
      setScale = &FailSetter,
      setRotation = &FailSetter,
      move = &FailSetter,
      start = &FailLifecycle,
      stop = &FailLifecycle,
      has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&TransformHas
    };
    NativeBindings.Log = new LogBindings { write = &WriteLog };
  }

  internal string[] Warnings => LogCalls.Where(c => c.Level == 3).Select(c => c.Message).ToArray();

  internal void Add(string uuid, string name, Vector3 position)
  {
    _order.Add(uuid);
    _names[uuid] = name;
    _positions[uuid] = position;
  }

  internal void Remove(string uuid)
  {
    _order.Remove(uuid);
    _names.Remove(uuid);
  }

  internal Vector3 PositionOf(string uuid) => _positions[uuid];

  internal void Place(string uuid, Vector3 position) => _positions[uuid] = position;

  public void Dispose()
  {
    NativeBindings.World = _world;
    NativeBindings.Transform = _transform;
    NativeBindings.Log = _log;
    Activate(null);
    foreach (var ptr in _strings)
    {
      Marshal.FreeCoTaskMem(ptr);
    }
  }

  private IntPtr Text(string value)
  {
    var ptr = Marshal.StringToCoTaskMemUTF8(value);
    _strings.Add(ptr);
    return ptr;
  }

  private static string Read(IntPtr ptr) => Marshal.PtrToStringUTF8(ptr) ?? "";

  [UnmanagedCallersOnly]
  private static IntPtr FindObjectByName(IntPtr name)
  {
    var model = s_current!;
    var wanted = Read(name);
    var found = model._order.FirstOrDefault(u => model._names[u] == wanted) ?? "";
    return model.Text(found);
  }

  [UnmanagedCallersOnly]
  private static IntPtr GetObjectName(IntPtr uuid)
  {
    var model = s_current!;
    return model.Text(model._names.TryGetValue(Read(uuid), out var name) ? name : "");
  }

  [UnmanagedCallersOnly]
  private static byte ObjectExists(IntPtr uuid) => s_current!._names.ContainsKey(Read(uuid)) ? (byte)1 : (byte)0;

  [UnmanagedCallersOnly]
  private static IntPtr GetAllObjectUuids() => s_current!.Text(string.Join(",", s_current._order));

  [UnmanagedCallersOnly]
  private static IntPtr SpawnObject(IntPtr name, float x, float y, float z) => IntPtr.Zero;

  [UnmanagedCallersOnly]
  private static void DestroyObject(IntPtr uuid)
  {
    var model = s_current!;
    var id = Read(uuid);
    model.Destroyed.Add(id);
    model.Remove(id);
  }

  // #lizard forgives
  [UnmanagedCallersOnly]
  private static IntPtr Raycast(float ox, float oy, float oz, float dx, float dy, float dz, float max, uint mask,
                                IntPtr ignore) => IntPtr.Zero;

  [UnmanagedCallersOnly]
  private static IntPtr OverlapSphere(float x, float y, float z, float radius, uint mask, IntPtr ignore) =>
    IntPtr.Zero;

  [UnmanagedCallersOnly]
  private static IntPtr SpawnPrefab(IntPtr prefab, float x, float y, float z)
  {
    var model = s_current!;
    model.Spawns.Add((Read(prefab), new Vector3(x, y, z)));
    if (model.FailSpawns)
    {
      return model.Text("");
    }

    var uuid = $"spawned-{++model._spawnCount}";
    model.Add(uuid, "Player", new Vector3(x, y, z));
    FakeNatives.Set($"PlayerController.has:{uuid}", true);
    FakeNatives.Set($"PlayerController.getPlayerSlot:{uuid}", -1);
    return model.Text(uuid);
  }

  [UnmanagedCallersOnly]
  private static byte SetObjectName(IntPtr uuid, IntPtr name)
  {
    var model = s_current!;
    var id = Read(uuid);
    if (!model._names.ContainsKey(id))
    {
      return 0;
    }

    model._names[id] = Read(name);
    model.Renames.Add((id, Read(name)));
    return 1;
  }

  [UnmanagedCallersOnly]
  private static void GetPosition(IntPtr uuid, float* x, float* y, float* z)
  {
    var position = s_current!._positions.TryGetValue(Read(uuid), out var p) ? p : Vector3.Zero;
    *x = position.X;
    *y = position.Y;
    *z = position.Z;
  }

  [UnmanagedCallersOnly]
  private static void SetPosition(IntPtr uuid, float x, float y, float z)
  {
    var model = s_current!;
    var id = Read(uuid);
    var to = new Vector3(x, y, z);
    model._positions[id] = to;
    model.Moves.Add((id, to));
  }

  [UnmanagedCallersOnly]
  private static void FailGetter(IntPtr uuid, float* x, float* y, float* z)
  {
    s_current!.Failures.Add($"an unexpected Transform getter was called for '{Read(uuid)}'");
    *x = 0f;
    *y = 0f;
    *z = 0f;
  }

  [UnmanagedCallersOnly]
  private static void FailSetter(IntPtr uuid, float x, float y, float z) =>
    s_current!.Failures.Add($"an unexpected Transform setter was called for '{Read(uuid)}'");

  [UnmanagedCallersOnly]
  private static void FailLifecycle(IntPtr uuid) =>
    s_current!.Failures.Add($"an unexpected Transform start/stop was called for '{Read(uuid)}'");

  [UnmanagedCallersOnly]
  private static byte TransformHas(IntPtr uuid) => s_current!._names.ContainsKey(Read(uuid)) ? (byte)1 : (byte)0;

  [UnmanagedCallersOnly]
  private static void WriteLog(int level, IntPtr message) => s_current!.LogCalls.Add((level, Read(message)));
}

public class StarterWorldListener : SceneScript
{
  public List<PlayerSpawnedEvent> Spawned { get; } = new();

  public void Listen() => subscribe<PlayerSpawnedEvent>(StarterEvents.PlayerSpawned, e => Spawned.Add(e));
}

// Shared setup for the world-level starter suites: FakeNatives, a WorldModel, and the common helpers.
public abstract class StarterWorldFixture : IDisposable
{
  protected const string PlayerPrefabUuid = "9c7f0e83-4d16-4b52-a09e-27f5b3d81c6a";
  protected const string PaletteRed = "7c8c01db-771f-4048-94c2-004b8d19e7aa";
  protected const string PaletteBlue = "66a8a6fc-d151-4567-9ce5-2e17cc675fe0";
  protected const string PaletteGreen = "c5eee5e6-9bd0-4f9b-977f-b9220a21a630";
  protected const string PaletteOrange = "ea184ad8-ca64-48cd-b69e-92bcb4649158";

  protected readonly IDisposable _natives = FakeNatives.Install();
  protected readonly WorldModel _world = new();

  protected StarterWorldFixture()
  {
    Bridge.Events.clear();
  }

  public void Dispose()
  {
    Bridge.RemoveInstance("listener-scene", nameof(StarterWorldListener));
    Bridge.Events.clear();
    var failures = _world.Failures.ToArray();
    _world.Dispose();
    var error = FakeNatives.LastError;
    _natives.Dispose();
    Assert.Null(error);
    Assert.Empty(failures);
  }

  protected static void SetField(object script, string name, object value) =>
    script.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(script, value);

  protected static void AssertVec(Vector3 expected, Vector3 actual) => AssertVec(expected, actual, 1e-3f);

  protected static void AssertVec(Vector3 expected, Vector3 actual, float tolerance) =>
    Assert.True(Vector3.Distance(expected, actual) <= tolerance, $"expected {expected} but got {actual}");

  protected static float[] ArgsOf(string callPrefix)
  {
    var call = FakeNatives.Calls.Last(c => c.StartsWith(callPrefix + "(", StringComparison.Ordinal));
    var args = call.Substring(call.IndexOf('(') + 1).TrimEnd(')').Split(',');
    return args.Skip(1).Select(a => float.Parse(a, CultureInfo.InvariantCulture)).ToArray();
  }

  protected static bool Called(string call) => FakeNatives.Calls.Contains(call);
}
