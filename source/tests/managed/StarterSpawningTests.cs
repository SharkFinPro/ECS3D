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

// BouncePad, Spawner and Lifetime against FakeNatives. Timers are ticked the way the bridge does: before fixedUpdate.
[Collection("BridgeInstances")]
public unsafe class StarterSpawningTests : IDisposable
{
  private readonly IDisposable _natives = FakeNatives.Install();
  private readonly LogBindings _originalLog = NativeBindings.Log;

  private static readonly List<(int Level, string Message)> LogCalls = new();

  [UnmanagedCallersOnly]
  private static void RecordLog(int level, IntPtr message)
    => LogCalls.Add((level, Marshal.PtrToStringUTF8(message) ?? ""));

  public StarterSpawningTests()
  {
    LogCalls.Clear();
    NativeBindings.Log = new LogBindings { write = &RecordLog };
  }

  public void Dispose()
  {
    NativeBindings.Log = _originalLog;
    LogCalls.Clear();
    var error = FakeNatives.LastError;
    _natives.Dispose();
    Assert.Null(error);
  }

  private static List<string> Warnings() => LogCalls.Where(c => c.Level == 3).Select(c => c.Message).ToList();

  private static T Make<T>(string entityId) where T : ScriptBase, new()
  {
    var script = new T { EntityId = entityId };
    script.initComponents();
    return script;
  }

  private static void SetField(object script, string name, object value) =>
    script.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(script, value);

  private static void Step(ScriptBase script, float dt)
  {
    script.tickTimers(dt);
    script.fixedUpdate(dt);
  }

  // The numeric arguments of a recorded call, after its leading uuid.
  private static float[] Args(string call) =>
    call.Substring(call.IndexOf('(') + 1).TrimEnd(')').Split(',').Skip(1)
      .Select(a => float.Parse(a, CultureInfo.InvariantCulture)).ToArray();

  private static List<string> CallsTo(string name) =>
    FakeNatives.Calls.Where(c => c.StartsWith(name + "(", StringComparison.Ordinal)).ToList();

  private static void AssertVec(Vector3 expected, float[] actual, float tolerance = 1e-4f)
  {
    var v = new Vector3(actual[0], actual[1], actual[2]);
    Assert.True(Vector3.Distance(expected, v) <= tolerance, $"expected {expected} but got {v}");
  }

  private static void Velocity(float x, float y, float z) =>
    FakeNatives.Set("RigidBody.getVelocity", new Vector3(x, y, z));

  private static BouncePad NewPad(float strength = 12f, Vector3? direction = null, float cooldown = 0.2f)
  {
    var pad = Make<BouncePad>("pad");
    SetField(pad, "m_strength", strength);
    SetField(pad, "m_direction", direction ?? new Vector3(0f, 1f, 0f));
    SetField(pad, "m_cooldown", cooldown);
    return pad;
  }

  [Fact]
  public void BouncePad_SetsVelocityOnceOnEnterForABodyAndIgnoresAnObjectWithoutOne()
  {
    var pad = NewPad();
    Velocity(1f, -3f, 2f);
    Step(pad, 0.1f);

    FakeNatives.Set("RigidBody.has", false);
    pad.onCollisionEnter("wall");
    Assert.Empty(CallsTo("RigidBody.setVelocity"));

    FakeNatives.Set("RigidBody.has", true);
    pad.onCollisionEnter("ball");
    var sets = CallsTo("RigidBody.setVelocity");
    Assert.Single(sets);
    Assert.StartsWith("RigidBody.setVelocity(ball,", sets[0]);
    AssertVec(new Vector3(1f, 1.2f, 2f), Args(sets[0]));
  }

  [Fact]
  public void BouncePad_CooldownIsPerBodyAndLapsesAfterTheConfiguredTime()
  {
    var pad = NewPad();
    FakeNatives.Set("RigidBody.has", true);
    Velocity(0f, -1f, 0f);
    Step(pad, 0.1f);

    pad.onCollisionEnter("ball");
    Assert.Single(CallsTo("RigidBody.setVelocity"));

    Step(pad, 0.1f);
    pad.onCollisionEnter("ball");
    Assert.Single(CallsTo("RigidBody.setVelocity"));

    pad.onCollisionEnter("other");
    Assert.Equal(2, CallsTo("RigidBody.setVelocity").Count);

    Step(pad, 0.15f);
    pad.onCollisionEnter("ball");
    Assert.Equal(3, CallsTo("RigidBody.setVelocity").Count);
  }

  [Fact]
  public void BouncePad_ReplacesTheAlongComponentAndKeepsTheRest()
  {
    var pad = NewPad(direction: new Vector3(0f, 2f, 0f));
    FakeNatives.Set("RigidBody.has", true);
    Step(pad, 0.1f);

    Velocity(2f, 5f, -1f);
    pad.onCollisionEnter("fast");
    Velocity(2f, -4f, -1f);
    pad.onCollisionEnter("falling");

    var sets = CallsTo("RigidBody.setVelocity");
    Assert.Equal(2, sets.Count);
    AssertVec(new Vector3(2f, 1.2f, -1f), Args(sets[0]));
    AssertVec(new Vector3(2f, 1.2f, -1f), Args(sets[1]));
  }

  [Fact]
  public void BouncePad_UsesTheStepDefaultBeforeAnyFixedUpdate()
  {
    var pad = NewPad();
    FakeNatives.Set("RigidBody.has", true);
    Velocity(0f, 0f, 0f);

    pad.onCollisionEnter("ball");

    AssertVec(new Vector3(0f, 0.24f, 0f), Args(CallsTo("RigidBody.setVelocity").Single()));
  }

  [Fact]
  public void Lifetime_DestroysItsObjectAfterTheRightNumberOfTicksAndNotBefore()
  {
    var life = Make<Lifetime>("rock");
    SetField(life, "m_seconds", 0.5f);
    life.start();

    for (var tick = 1; tick <= 4; tick++)
    {
      Step(life, 0.1f);
      Assert.Empty(CallsTo("World.destroyObject"));
    }

    Step(life, 0.1f);
    Assert.Equal(new[] { "World.destroyObject(rock)" }, CallsTo("World.destroyObject"));

    Step(life, 0.1f);
    Assert.Single(CallsTo("World.destroyObject"));
  }

  private static Spawner NewSpawner(int max = 10, float radius = 2f, bool onStart = true, int seed = 0)
  {
    var spawner = Make<Spawner>("spawner");
    SetField(spawner, "m_prefab", "prefab-1");
    SetField(spawner, "m_interval", 1f);
    SetField(spawner, "m_maxAlive", max);
    SetField(spawner, "m_offsetRadius", radius);
    SetField(spawner, "m_spawnOnStart", onStart);
    SetField(spawner, "m_seed", seed);
    return spawner;
  }

  private static void Exists(string uuid, bool exists = true) => FakeNatives.Set($"World.objectExists:{uuid}", exists);

  // Lets one interval (1 s) pass, answering the spawn with this uuid.
  private static void NextSpawn(Spawner spawner, string uuid)
  {
    Exists(uuid);
    FakeNatives.SetString("World.spawnPrefab", uuid);
    Step(spawner, 0.5f);
    Step(spawner, 0.5f);
  }

  [Fact]
  public void Spawner_AtTheCapDestroysTheOldestFirst()
  {
    var spawner = NewSpawner(max: 2);
    Exists("s1");
    FakeNatives.SetString("World.spawnPrefab", "s1");
    spawner.start();
    NextSpawn(spawner, "s2");
    Assert.Equal(2, CallsTo("World.spawnPrefab").Count);
    Assert.Empty(CallsTo("World.destroyObject"));

    NextSpawn(spawner, "s3");
    Assert.Equal(new[] { "World.destroyObject(s1)" }, CallsTo("World.destroyObject"));

    NextSpawn(spawner, "s4");
    Assert.Equal(new[] { "World.destroyObject(s1)", "World.destroyObject(s2)" }, CallsTo("World.destroyObject"));
  }

  [Fact]
  public void Spawner_ForgetsObjectsSomethingElseDestroyed()
  {
    var spawner = NewSpawner(max: 2);
    Exists("s1");
    FakeNatives.SetString("World.spawnPrefab", "s1");
    spawner.start();
    NextSpawn(spawner, "s2");

    Exists("s1", false);
    NextSpawn(spawner, "s3");
    Assert.Empty(CallsTo("World.destroyObject"));

    NextSpawn(spawner, "s4");
    Assert.Equal(new[] { "World.destroyObject(s2)" }, CallsTo("World.destroyObject"));
  }

  private static List<Vector3> SpawnPositions(int seed, int count)
  {
    FakeNatives.Calls.Clear();
    var spawner = NewSpawner(max: 100, radius: 3f, onStart: false, seed: seed);
    SetField(spawner, "m_heightOffset", 1f);
    spawner.start();
    for (var i = 0; i < count; i++)
    {
      NextSpawn(spawner, $"seed{seed}-{i}");
    }

    return CallsTo("World.spawnPrefab").Select(c => Args(c)).Select(a => new Vector3(a[0], a[1], a[2])).ToList();
  }

  [Fact]
  public void Spawner_SeededOffsetsRepeatBetweenInstancesAndStayInsideTheDisc()
  {
    FakeNatives.Set("Transform.getPosition", new Vector3(10f, 5f, -3f));

    var first = SpawnPositions(7, 6);
    var second = SpawnPositions(7, 6);
    var other = SpawnPositions(8, 6);

    Assert.Equal(6, first.Count);
    Assert.Equal(first, second);
    Assert.NotEqual(first, other);
    Assert.True(first.Select(p => p.X).Distinct().Count() > 1);
    foreach (var p in first)
    {
      Assert.Equal(6f, p.Y, 1e-4f);
      Assert.True(MathF.Sqrt((p.X - 10f) * (p.X - 10f) + (p.Z + 3f) * (p.Z + 3f)) <= 3f + 1e-4f);
    }
  }

  [Fact]
  public void Spawner_SpawnOnStartFalseWaitsOneInterval()
  {
    var waiting = NewSpawner(onStart: false);
    FakeNatives.SetString("World.spawnPrefab", "w1");
    waiting.start();
    Step(waiting, 0.5f);
    Assert.Empty(CallsTo("World.spawnPrefab"));
    Step(waiting, 0.5f);
    Assert.Single(CallsTo("World.spawnPrefab"));

    FakeNatives.Calls.Clear();
    var eager = NewSpawner(onStart: true);
    eager.start();
    Assert.Single(CallsTo("World.spawnPrefab"));
  }

  [Fact]
  public void Spawner_AppliesInitialVelocityPerTickOnlyToABody()
  {
    var withBody = NewSpawner();
    SetField(withBody, "m_initialVelocity", new Vector3(50f, 0f, -25f));
    FakeNatives.SetString("World.spawnPrefab", "ball");
    FakeNatives.Set("RigidBody.has", true);
    withBody.start();
    AssertVec(new Vector3(1f, 0f, -0.5f), Args(CallsTo("RigidBody.setVelocity").Single()));

    FakeNatives.Calls.Clear();
    FakeNatives.Set("RigidBody.has", false);
    var withoutBody = NewSpawner();
    SetField(withoutBody, "m_initialVelocity", new Vector3(50f, 0f, -25f));
    withoutBody.start();
    Assert.Single(CallsTo("World.spawnPrefab"));
    Assert.Empty(CallsTo("RigidBody.setVelocity"));
  }

  [Fact]
  public void Spawner_ABadPrefabWarnsOnceAndKeepsTrying()
  {
    var spawner = NewSpawner();
    FakeNatives.SetString("World.spawnPrefab", null);
    spawner.start();
    for (var i = 0; i < 3; i++)
    {
      Step(spawner, 0.5f);
      Step(spawner, 0.5f);
    }

    Assert.Equal(4, CallsTo("World.spawnPrefab").Count);
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("prefab-1", warnings[0]);
  }

  [Fact]
  public void Spawner_ABadPrefabDoesNotShrinkThePool()
  {
    var spawner = NewSpawner(max: 1);
    Exists("s1");
    FakeNatives.SetString("World.spawnPrefab", "s1");
    spawner.start();

    FakeNatives.SetString("World.spawnPrefab", null);
    Step(spawner, 0.5f);
    Step(spawner, 0.5f);
    Assert.Empty(CallsTo("World.destroyObject"));

    NextSpawn(spawner, "s2");
    Assert.Equal(new[] { "World.destroyObject(s1)" }, CallsTo("World.destroyObject"));
  }

  [Fact]
  public void Spawner_MaxAliveBelowOneSpawnsNothingAndWarnsOnce()
  {
    var spawner = NewSpawner(max: 0);
    FakeNatives.SetString("World.spawnPrefab", "s1");
    spawner.start();
    Step(spawner, 0.5f);
    Step(spawner, 0.5f);
    Assert.Empty(CallsTo("World.spawnPrefab"));
    Assert.Single(Warnings());

    SetField(spawner, "m_maxAlive", 1);
    Step(spawner, 0.5f);
    Step(spawner, 0.5f);
    Assert.Single(CallsTo("World.spawnPrefab"));
  }
}
