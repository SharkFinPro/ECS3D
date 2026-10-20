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

  internal WorldModel()
  {
    s_current = this;
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
    s_current = null;
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

// PlayerSpawnManager and DayNightCycle against a WorldModel and FakeNatives.
[Collection("BridgeInstances")]
public class StarterWorldTests : IDisposable
{
  private const string PlayerPrefabUuid = "9c7f0e83-4d16-4b52-a09e-27f5b3d81c6a";
  private const string PaletteRed = "7c8c01db-771f-4048-94c2-004b8d19e7aa";
  private const string PaletteBlue = "66a8a6fc-d151-4567-9ce5-2e17cc675fe0";
  private const string PaletteGreen = "c5eee5e6-9bd0-4f9b-977f-b9220a21a630";
  private const string PaletteOrange = "ea184ad8-ca64-48cd-b69e-92bcb4649158";

  private readonly IDisposable _natives = FakeNatives.Install();
  private readonly WorldModel _world = new();

  public StarterWorldTests()
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

  private static void SetField(object script, string name, object value) =>
    script.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(script, value);

  private static void AssertVec(Vector3 expected, Vector3 actual, float tolerance = 1e-3f) =>
    Assert.True(Vector3.Distance(expected, actual) <= tolerance, $"expected {expected} but got {actual}");

  private static float[] ArgsOf(string callPrefix)
  {
    var call = FakeNatives.Calls.Last(c => c.StartsWith(callPrefix + "(", StringComparison.Ordinal));
    var args = call.Substring(call.IndexOf('(') + 1).TrimEnd(')').Split(',');
    return args.Skip(1).Select(a => float.Parse(a, CultureInfo.InvariantCulture)).ToArray();
  }

  private static bool Called(string call) => FakeNatives.Calls.Contains(call);

  // ---- PlayerSpawnManager ----

  private void AddSpawnPoints()
  {
    _world.Add("crate", "Crate", new Vector3(99f, 0f, 99f));
    _world.Add("sp3", "SpawnPoint3", new Vector3(30f, 0f, 3f));
    _world.Add("sp1", "SpawnPoint1", new Vector3(10f, 0f, 1f));
    _world.Add("sp4", "SpawnPoint4", new Vector3(40f, 0f, 4f));
    _world.Add("sp2", "SpawnPoint2", new Vector3(20f, 0f, 2f));
  }

  private static PlayerSpawnManager NewManager() => new();

  [Fact]
  public void Spawns_AtPointsRoundRobinSortedByName_OfThePlayerPrefab()
  {
    AddSpawnPoints();
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);
    manager.onPlayerJoined(2);

    Assert.Equal(3, _world.Spawns.Count);
    Assert.All(_world.Spawns, s => Assert.Equal(PlayerPrefabUuid, s.Prefab));
    AssertVec(new Vector3(10f, 1f, 1f), _world.Spawns[0].At);
    AssertVec(new Vector3(20f, 1f, 2f), _world.Spawns[1].At);
    AssertVec(new Vector3(30f, 1f, 3f), _world.Spawns[2].At);
    Assert.Empty(_world.Warnings);
  }

  [Fact]
  public void Spawns_SetTheSlotOnTheNewBodyDuringTheJoin()
  {
    AddSpawnPoints();
    var manager = NewManager();

    manager.onPlayerJoined(0);
    Assert.True(Called("PlayerController.setPlayerSlot(spawned-1,0)"));
    Assert.False(Called("PlayerController.setPlayerSlot(spawned-2,1)"));

    manager.onPlayerJoined(1);
    Assert.True(Called("PlayerController.setPlayerSlot(spawned-2,1)"));
  }

  [Fact]
  public void SpawnedBodies_AreNamedAfterTheirSlot()
  {
    AddSpawnPoints();
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.onPlayerJoined(3);

    Assert.Equal(new[] { ("spawned-1", "Player 0"), ("spawned-2", "Player 3") }, _world.Renames);
    Assert.Equal("Player 3", World.getObjectName("spawned-2"));
  }

  [Fact]
  public void AdoptedBody_KeepsItsName_WhileASpawnedOneInTheSameSceneIsRenamed()
  {
    AddSpawnPoints();
    _world.Add("placed", "Hero", new Vector3(10f, 1f, 1f));
    FakeNatives.Set("PlayerController.has:placed", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:placed", 0);
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);

    Assert.Equal(new[] { ("spawned-1", "Player 1") }, _world.Renames);
    Assert.Equal("Hero", World.getObjectName("placed"));
  }

  [Fact]
  public void FifthPlayer_SharesAPointThreeUnitsHigher()
  {
    AddSpawnPoints();
    var manager = NewManager();

    for (var slot = 0; slot < 5; slot++)
    {
      manager.onPlayerJoined(slot);
    }

    AssertVec(new Vector3(10f, 1f, 1f), _world.Spawns[0].At);
    AssertVec(new Vector3(40f, 1f, 4f), _world.Spawns[3].At);
    AssertVec(new Vector3(10f, 4f, 1f), _world.Spawns[4].At);
  }

  [Fact]
  public void SpawnHeight_IsAddedToThePointAndTheSpawnPrefixIsConfigurable()
  {
    _world.Add("home", "Start A", new Vector3(5f, 2f, 5f));
    _world.Add("decoy", "SpawnPoint1", new Vector3(50f, 0f, 50f));
    var manager = NewManager();
    SetField(manager, "m_spawnPointPrefix", "Start");
    SetField(manager, "m_spawnHeight", 2.5f);

    manager.onPlayerJoined(0);

    AssertVec(new Vector3(5f, 4.5f, 5f), _world.Spawns[0].At);
  }

  [Fact]
  public void AdoptsAPlacedBody_NeverSpawnsOrDestroysIt_AndTakesUpAPoint()
  {
    AddSpawnPoints();
    _world.Add("placed", "Player 0", new Vector3(10f, 1f, 1f));
    FakeNatives.Set("PlayerController.has:placed", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:placed", 0);
    var manager = NewManager();

    manager.onPlayerJoined(0);
    Assert.Empty(_world.Spawns);

    manager.onPlayerJoined(1);
    Assert.Single(_world.Spawns);
    AssertVec(new Vector3(20f, 1f, 2f), _world.Spawns[0].At);

    manager.onPlayerLeft(0);
    Assert.Empty(_world.Destroyed);

    manager.onPlayerLeft(1);
    Assert.Equal(new[] { "spawned-1" }, _world.Destroyed);
  }

  [Fact]
  public void Leaving_DestroysTheSpawnedBodyOnce_AndTheSlotCanJoinAgain()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);

    manager.onPlayerLeft(1);
    Assert.Equal(new[] { "spawned-2" }, _world.Destroyed);

    manager.onPlayerLeft(1);
    Assert.Single(_world.Destroyed);

    manager.onPlayerJoined(1);
    Assert.Equal(3, _world.Spawns.Count);
  }

  [Fact]
  public void ReconnectingRepeatedly_AlwaysLandsOnTheGround()
  {
    AddSpawnPoints();
    var manager = NewManager();

    for (var round = 0; round < 8; round++)
    {
      manager.onPlayerJoined(0);
      manager.onPlayerLeft(0);
    }

    Assert.Equal(8, _world.Spawns.Count);
    Assert.All(_world.Spawns, s => AssertVec(new Vector3(10f, 1f, 1f), s.At));
  }

  [Fact]
  public void FivePlayersAtOnce_OnlyTheFifthIsLifted_AndALeaverFreesItsPoint()
  {
    AddSpawnPoints();
    var manager = NewManager();

    for (var slot = 0; slot < 5; slot++)
    {
      manager.onPlayerJoined(slot);
    }

    Assert.Equal(new[] { 1f, 1f, 1f, 1f, 4f }, _world.Spawns.Select(s => s.At.Y).ToArray());

    manager.onPlayerLeft(1);
    manager.onPlayerJoined(5);

    AssertVec(new Vector3(20f, 1f, 2f), _world.Spawns[5].At);
  }

  [Fact]
  public void ASlotSwitch_LeavesThenJoins_DestroyingAndRespawningTheBody()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);

    manager.onPlayerLeft(0);
    manager.onPlayerJoined(1);

    Assert.Equal(new[] { "spawned-1" }, _world.Destroyed);
    Assert.Equal(2, _world.Spawns.Count);
    AssertVec(new Vector3(10f, 1f, 1f), _world.Spawns[1].At);
    Assert.True(Called("PlayerController.setPlayerSlot(spawned-2,1)"));
  }

  [Fact]
  public void RejoiningWhileTheBodyLives_DoesNotSpawnASecondOne()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);

    manager.onPlayerJoined(0);

    Assert.Single(_world.Spawns);
  }

  [Fact]
  public void FallingBelowTheLimit_MovesTheBodyHomeWithZeroVelocity()
  {
    AddSpawnPoints();
    FakeNatives.Set("RigidBody.has", true);
    var manager = NewManager();
    manager.onPlayerJoined(0);

    _world.Place("spawned-1", new Vector3(3f, -19f, 3f));
    manager.fixedUpdate(0.02f);
    Assert.Empty(_world.Moves);
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("RigidBody.setVelocity(", StringComparison.Ordinal)));

    _world.Place("spawned-1", new Vector3(3f, -25f, 3f));
    manager.fixedUpdate(0.02f);

    Assert.Single(_world.Moves);
    Assert.Equal("spawned-1", _world.Moves[0].Uuid);
    AssertVec(new Vector3(10f, 1f, 1f), _world.Moves[0].To);
    Assert.True(Called("RigidBody.setVelocity(spawned-1,0,0,0)"));
    Assert.True(Called("RigidBody.setAngularVelocity(spawned-1,0,0,0)"));
  }

  [Fact]
  public void FallingAdoptedBody_ReturnsToWhereItWasPlaced_AndABodyWithoutPhysicsIsStillMoved()
  {
    _world.Add("placed", "Player 0", new Vector3(5f, 2f, 5f));
    FakeNatives.Set("PlayerController.has:placed", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:placed", 0);
    var manager = NewManager();
    manager.onPlayerJoined(0);

    _world.Place("placed", new Vector3(0f, -50f, 0f));
    manager.fixedUpdate(0.02f);

    AssertVec(new Vector3(5f, 2f, 5f), _world.PositionOf("placed"));
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("RigidBody.setVelocity(", StringComparison.Ordinal)));
  }

  [Fact]
  public void RespawnBelowY_IsConfigurable()
  {
    AddSpawnPoints();
    var manager = NewManager();
    SetField(manager, "m_respawnBelowY", 5.0f);
    manager.onPlayerJoined(0);

    _world.Place("spawned-1", new Vector3(3f, 6f, 3f));
    manager.fixedUpdate(0.02f);
    Assert.Empty(_world.Moves);

    _world.Place("spawned-1", new Vector3(3f, 4f, 3f));
    manager.fixedUpdate(0.02f);
    Assert.Single(_world.Moves);
  }

  [Fact]
  public void DestroyedBody_IsRespawnedAfterTwoSeconds_AtItsSpawnPoint()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);
    _world.Remove("spawned-2");

    manager.fixedUpdate(0.02f);
    manager.tickTimers(1.9f);
    manager.fixedUpdate(0.02f);
    Assert.Equal(2, _world.Spawns.Count);

    manager.tickTimers(0.2f);

    Assert.Equal(3, _world.Spawns.Count);
    AssertVec(new Vector3(20f, 1f, 2f), _world.Spawns[2].At);
    Assert.True(Called("PlayerController.setPlayerSlot(spawned-3,1)"));

    manager.fixedUpdate(0.02f);
    manager.tickTimers(5f);
    Assert.Equal(3, _world.Spawns.Count);
  }

  [Fact]
  public void DestroyedBody_IsNotRespawnedWhenThePlayerLeavesFirst()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);
    _world.Remove("spawned-1");
    manager.fixedUpdate(0.02f);

    manager.onPlayerLeft(0);
    manager.tickTimers(3f);

    Assert.Single(_world.Spawns);
  }

  [Fact]
  public void TintPlayers_SetsTheSlotColorTexture_CyclingFromSlotFour()
  {
    AddSpawnPoints();
    FakeNatives.Set("ModelRenderer.has", true);
    var manager = NewManager();
    SetField(manager, "m_tintPlayers", true);

    for (var slot = 0; slot < 5; slot++)
    {
      manager.onPlayerJoined(slot);
    }

    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-1,{PaletteRed})"));
    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-2,{PaletteBlue})"));
    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-3,{PaletteGreen})"));
    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-4,{PaletteOrange})"));
    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-5,{PaletteRed})"));
  }

  [Fact]
  public void TintPlayersOff_LeavesTheTextureAlone()
  {
    AddSpawnPoints();
    FakeNatives.Set("ModelRenderer.has", true);
    var manager = NewManager();

    manager.onPlayerJoined(0);

    Assert.Single(_world.Spawns);
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("ModelRenderer.setTextureUUID(", StringComparison.Ordinal)));
  }

  [Fact]
  public void NoSpawnPoints_SpawnsAtTheOriginRaisedBySpawnHeight_AndWarnsOnce()
  {
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);

    Assert.Equal(2, _world.Spawns.Count);
    AssertVec(new Vector3(0f, 1f, 0f), _world.Spawns[0].At);
    AssertVec(new Vector3(0f, 1f, 0f), _world.Spawns[1].At);
    var warnings = _world.Warnings;
    Assert.Single(warnings);
    Assert.Contains("SpawnPoint", warnings[0]);
  }

  [Fact]
  public void FailedSpawn_WarnsAndIsNotRetried()
  {
    AddSpawnPoints();
    _world.FailSpawns = true;
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.fixedUpdate(0.02f);
    manager.tickTimers(5f);
    manager.fixedUpdate(0.02f);
    manager.tickTimers(5f);

    Assert.Single(_world.Spawns);
    Assert.Single(_world.Warnings);
  }

  private StarterWorldListener ListenForSpawns()
  {
    var listener = new StarterWorldListener();
    Bridge.AddInstance("listener-scene", nameof(StarterWorldListener), listener);
    listener.Listen();
    return listener;
  }

  [Fact]
  public void PublishesPlayerSpawned_ForASpawnedAndAnAdoptedBody()
  {
    AddSpawnPoints();
    _world.Add("placed", "Player 0", new Vector3(10f, 1f, 1f));
    FakeNatives.Set("PlayerController.has:placed", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:placed", 0);
    var listener = ListenForSpawns();
    var manager = NewManager();
    Assert.Empty(listener.Spawned);

    manager.onPlayerJoined(2);
    manager.onPlayerJoined(0);

    Assert.Equal(2, listener.Spawned.Count);
    Assert.Equal(2, listener.Spawned[0].Slot);
    Assert.Equal("spawned-1", listener.Spawned[0].Uuid);
    Assert.Equal(0, listener.Spawned[1].Slot);
    Assert.Equal("placed", listener.Spawned[1].Uuid);
  }

  // ---- DayNightCycle ----

  private DayNightCycle NewCycle(float startTime, bool spot = false, float diffuse = 0.8f)
  {
    _world.Add("sun", "Sun", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("LightRenderer.has", true);
    FakeNatives.Set("LightRenderer.getDiffuse", diffuse);
    FakeNatives.Set("LightRenderer.getIsSpotLight", spot);
    var cycle = new DayNightCycle();
    SetField(cycle, "m_startTime", startTime);
    return cycle;
  }

  [Fact]
  public void Noon_PutsTheLightAtTheTopOfTheArcAroundTheCenter()
  {
    var cycle = NewCycle(0.25f);
    SetField(cycle, "m_arcCenter", new Vector3(10f, 5f, -3f));

    cycle.fixedUpdate(0.0f);

    AssertVec(new Vector3(10f, 45f, -3f), _world.PositionOf("sun"));
    Assert.Empty(_world.Warnings);
  }

  [Fact]
  public void TheArcStartsAtTheEasternHorizonAndReachesNoonAfterAQuarterPeriod()
  {
    var cycle = NewCycle(0.0f);

    cycle.fixedUpdate(0.0f);
    AssertVec(new Vector3(40f, 0f, 0f), _world.PositionOf("sun"));

    cycle.fixedUpdate(30.0f);
    AssertVec(new Vector3(0f, 40f, 0f), _world.PositionOf("sun"));

    cycle.fixedUpdate(60.0f);
    AssertVec(new Vector3(0f, -40f, 0f), _world.PositionOf("sun"));
  }

  [Fact]
  public void TheDayWrapsAfterAFullPeriod()
  {
    var cycle = NewCycle(0.0f);

    cycle.fixedUpdate(150.0f);

    AssertVec(new Vector3(0f, 40f, 0f), _world.PositionOf("sun"));
  }

  [Fact]
  public void Noon_UsesTheDayColorAndAmbient_AndKeepsTheLightsOwnDiffuse()
  {
    var cycle = NewCycle(0.25f);

    cycle.fixedUpdate(0.0f);

    var color = ArgsOf("LightRenderer.setColor");
    AssertVec(new Vector3(1.0f, 0.96f, 0.88f), new Vector3(color[0], color[1], color[2]), 1e-4f);
    Assert.Equal(0.3f, ArgsOf("LightRenderer.setAmbient")[0], 1e-4f);
    Assert.Equal(0.8f, ArgsOf("LightRenderer.setDiffuse")[0], 1e-4f);
  }

  [Fact]
  public void Midnight_UsesTheNightColorAndAmbient_AndDimsTheDiffuseWithIt()
  {
    var cycle = NewCycle(0.75f);

    cycle.fixedUpdate(0.0f);

    AssertVec(new Vector3(0f, -40f, 0f), _world.PositionOf("sun"));
    var color = ArgsOf("LightRenderer.setColor");
    AssertVec(new Vector3(0.1f, 0.15f, 0.4f), new Vector3(color[0], color[1], color[2]), 1e-4f);
    Assert.Equal(0.06f, ArgsOf("LightRenderer.setAmbient")[0], 1e-4f);
    Assert.Equal(0.16f, ArgsOf("LightRenderer.setDiffuse")[0], 1e-4f);
  }

  [Fact]
  public void AtTheHorizon_TheColorIsDusk_AndTheAmbientStaysAtTheNightFloor()
  {
    var cycle = NewCycle(0.5f);

    cycle.fixedUpdate(0.0f);

    var color = ArgsOf("LightRenderer.setColor");
    AssertVec(new Vector3(1.0f, 0.5f, 0.2f), new Vector3(color[0], color[1], color[2]), 1e-3f);
    Assert.Equal(0.06f, ArgsOf("LightRenderer.setAmbient")[0], 1e-4f);
  }

  [Fact]
  public void MidMorning_BlendsDuskTowardDayAndAmbientTowardDay()
  {
    // Height 0.15 is halfway between the dusk (0) and day (0.3) thresholds.
    var phase = MathF.Asin(0.15f) / (2.0f * MathF.PI);
    var cycle = NewCycle(phase);

    cycle.fixedUpdate(0.0f);

    var color = ArgsOf("LightRenderer.setColor");
    var expected = Vector3.Lerp(new Vector3(1.0f, 0.5f, 0.2f), new Vector3(1.0f, 0.96f, 0.88f), 0.5f);
    AssertVec(expected, new Vector3(color[0], color[1], color[2]), 1e-3f);
    Assert.Equal(0.06f + (0.3f - 0.06f) * 0.15f, ArgsOf("LightRenderer.setAmbient")[0], 1e-3f);
  }

  [Fact]
  public void ASpotLight_IsAimedAtTheCenter_AndAPointLightIsNotGivenADirection()
  {
    var spot = NewCycle(0.25f, spot: true);
    spot.fixedUpdate(0.0f);
    var down = ArgsOf("LightRenderer.setDirection");
    AssertVec(new Vector3(0f, -1f, 0f), new Vector3(down[0], down[1], down[2]));

    FakeNatives.Calls.Clear();
    spot.fixedUpdate(30.0f);
    var west = ArgsOf("LightRenderer.setDirection");
    var toCenter = Vector3.Normalize(-_world.PositionOf("sun"));
    AssertVec(toCenter, new Vector3(west[0], west[1], west[2]));

    FakeNatives.Calls.Clear();
    FakeNatives.Set("LightRenderer.getIsSpotLight", false);
    spot.fixedUpdate(0.0f);
    Assert.True(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.setAmbient(", StringComparison.Ordinal)));
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.setDirection(", StringComparison.Ordinal)));
  }

  [Fact]
  public void MissingLight_WarnsOnceAndTouchesNothing_ThenWorksOnceItAppears()
  {
    var cycle = new DayNightCycle();
    SetField(cycle, "m_startTime", 0.25f);
    FakeNatives.Set("LightRenderer.has", true);

    for (var tick = 0; tick < 12; tick++)
    {
      cycle.fixedUpdate(0.25f);
    }

    var warnings = _world.Warnings;
    Assert.Single(warnings);
    Assert.Contains("Sun", warnings[0]);
    Assert.Empty(_world.Moves);
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.set", StringComparison.Ordinal)));

    _world.Add("sun", "Sun", Vector3.Zero);
    for (var tick = 0; tick < 4; tick++)
    {
      cycle.fixedUpdate(0.25f);
    }

    Assert.NotEmpty(_world.Moves);
    Assert.True(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.setColor(", StringComparison.Ordinal)));
  }

  private static float LastDiffuseSetOn(string uuid)
  {
    var call = FakeNatives.Calls.Last(c => c.StartsWith($"LightRenderer.setDiffuse({uuid},", StringComparison.Ordinal));
    return float.Parse(call.Substring(call.LastIndexOf(',') + 1).TrimEnd(')'), CultureInfo.InvariantCulture);
  }

  [Fact]
  public void TheBaseDiffuseIsReadOncePerLight_SoDimmingDoesNotCompound()
  {
    var cycle = NewCycle(0.75f);
    cycle.fixedUpdate(0.0f);
    Assert.Equal(0.16f, LastDiffuseSetOn("sun"), 1e-4f);

    FakeNatives.Set("LightRenderer.getDiffuse", 0.16f);
    cycle.fixedUpdate(60.0f);

    Assert.Equal(0.8f, LastDiffuseSetOn("sun"), 1e-4f);
    Assert.Equal(1, FakeNatives.Calls.Count(c => c == "LightRenderer.getDiffuse(sun)"));
  }

  [Fact]
  public void ChangingTheLightName_RestoresThePreviousLightsDiffuse()
  {
    var cycle = NewCycle(0.75f);
    _world.Add("moon", "Moon", Vector3.Zero);
    cycle.fixedUpdate(0.0f);
    Assert.Equal(0.16f, LastDiffuseSetOn("sun"), 1e-4f);

    SetField(cycle, "m_light", "Moon");
    cycle.fixedUpdate(0.0f);

    Assert.Equal(0.8f, LastDiffuseSetOn("sun"), 1e-4f);
    Assert.Equal(0.16f, LastDiffuseSetOn("moon"), 1e-4f);
  }

  [Fact]
  public void Stopping_RestoresTheLightsDiffuse()
  {
    var cycle = NewCycle(0.75f);
    cycle.fixedUpdate(0.0f);
    Assert.Equal(0.16f, LastDiffuseSetOn("sun"), 1e-4f);

    cycle.stop();

    Assert.Equal(0.8f, LastDiffuseSetOn("sun"), 1e-4f);
  }

  [Fact]
  public void ANonPositivePeriod_HoldsTheStartTimeInsteadOfDividingByZero()
  {
    var cycle = NewCycle(0.25f);
    SetField(cycle, "m_periodSeconds", 0.0f);

    cycle.fixedUpdate(10.0f);

    AssertVec(new Vector3(0f, 40f, 0f), _world.PositionOf("sun"));
  }

  [Fact]
  public void ALightWithARigidBody_WarnsOnceThatPhysicsAndTheScriptFight()
  {
    var cycle = NewCycle(0.25f);
    FakeNatives.Set("RigidBody.has", true);

    for (var tick = 0; tick < 3; tick++)
    {
      cycle.fixedUpdate(0.02f);
    }

    var warnings = _world.Warnings;
    Assert.Single(warnings);
    Assert.Contains("RigidBody", warnings[0]);
  }

  [Fact]
  public void ALightObjectWithoutALightRenderer_StillMovesAndWarnsOnce()
  {
    var cycle = NewCycle(0.25f);
    FakeNatives.Set("LightRenderer.has", false);

    cycle.fixedUpdate(0.02f);
    cycle.fixedUpdate(0.02f);

    Assert.Equal(2, _world.Moves.Count);
    Assert.Single(_world.Warnings);
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.set", StringComparison.Ordinal)));
  }
}
