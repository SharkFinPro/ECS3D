using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// LookAtTarget, Follower and PingPongMover against FakeNatives. Transforms and rigid bodies are stateful here
// (positions persist between ticks and per uuid), so movement can be followed over several ticks. Warnings come
// through a recording Log binding.
[Collection("BridgeInstances")]
public unsafe class StarterMotionTargetsTests : IDisposable
{
  private readonly IDisposable _natives = FakeNatives.Install();
  private readonly LogBindings _originalLog = NativeBindings.Log;

  private static readonly List<(int Level, string Message)> LogCalls = new();
  private static readonly Dictionary<string, Vector3> Positions = new();
  private static readonly Dictionary<string, Vector3> Scales = new();
  private static readonly Dictionary<string, Vector3> Rotations = new();
  private static readonly Dictionary<string, Vector3> Velocities = new();
  private static readonly HashSet<string> Bodies = new();
  private static readonly List<(string Uuid, Vector3 Delta)> Moves = new();
  private static readonly List<(string Uuid, Vector3 Force, int Mode)> Forces = new();

  public StarterMotionTargetsTests()
  {
    LogCalls.Clear();
    Positions.Clear();
    Scales.Clear();
    Rotations.Clear();
    Velocities.Clear();
    Bodies.Clear();
    Moves.Clear();
    Forces.Clear();

    NativeBindings.Log = new LogBindings { write = &RecordLog };
    NativeBindings.Transform = new TransformBindings
    {
      getPosition = &TransformGetPosition,
      getScale = &TransformGetScale,
      getRotation = &TransformGetRotation,
      setRotation = &TransformSetRotation,
      move = &TransformMove,
      setPosition = &TransformSetPosition,
      has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&TransformHas
    };
    NativeBindings.RigidBody = new RigidBodyBindings
    {
      applyForce = &RigidBodyApplyForce,
      getVelocity = &RigidBodyGetVelocity,
      has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&RigidBodyHas
    };
  }

  public void Dispose()
  {
    NativeBindings.Log = _originalLog;
    LogCalls.Clear();
    var error = FakeNatives.LastError;
    _natives.Dispose();
    Assert.Null(error);
  }

  [UnmanagedCallersOnly]
  private static void RecordLog(int level, IntPtr message)
    => LogCalls.Add((level, Marshal.PtrToStringUTF8(message) ?? ""));

  private static string Id(IntPtr uuid) => Marshal.PtrToStringUTF8(uuid) ?? "";

  private static void Out(Dictionary<string, Vector3> values, IntPtr uuid, float* x, float* y, float* z)
  {
    values.TryGetValue(Id(uuid), out var v);
    *x = v.X;
    *y = v.Y;
    *z = v.Z;
  }

  [UnmanagedCallersOnly]
  private static void TransformGetPosition(IntPtr u, float* x, float* y, float* z) => Out(Positions, u, x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformGetScale(IntPtr u, float* x, float* y, float* z) => Out(Scales, u, x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformGetRotation(IntPtr u, float* x, float* y, float* z) => Out(Rotations, u, x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformSetRotation(IntPtr u, float x, float y, float z) =>
    Rotations[Id(u)] = new Vector3(x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformMove(IntPtr u, float x, float y, float z)
  {
    var delta = new Vector3(x, y, z);
    Positions[Id(u)] = Positions.GetValueOrDefault(Id(u)) + delta;
    Moves.Add((Id(u), delta));
  }

  [UnmanagedCallersOnly]
  private static void TransformSetPosition(IntPtr u, float x, float y, float z) =>
    Positions[Id(u)] = new Vector3(x, y, z);

  [UnmanagedCallersOnly]
  private static byte TransformHas(IntPtr u) => Positions.ContainsKey(Id(u)) ? (byte)1 : (byte)0;

  [UnmanagedCallersOnly]
  private static void RigidBodyApplyForce(IntPtr u, float x, float y, float z, float px, float py, float pz,
                                          int mode) =>
    Forces.Add((Id(u), new Vector3(x, y, z), mode));

  [UnmanagedCallersOnly]
  private static void RigidBodyGetVelocity(IntPtr u, float* x, float* y, float* z) => Out(Velocities, u, x, y, z);

  [UnmanagedCallersOnly]
  private static byte RigidBodyHas(IntPtr u) => Bodies.Contains(Id(u)) ? (byte)1 : (byte)0;

  private static string[] Warnings() =>
    LogCalls.Where(c => c.Level == 3).Select(c => c.Message).ToArray();

  private static void AssertVec(Vector3 expected, Vector3 actual, float tolerance = 1e-3f)
  {
    Assert.True(Vector3.Distance(expected, actual) <= tolerance, $"expected {expected} but got {actual}");
  }

  private static void AddObject(string uuid, Vector3 position, Vector3? scale = null)
  {
    Positions[uuid] = position;
    Scales[uuid] = scale ?? Vector3.One;
    Rotations[uuid] = Vector3.Zero;
  }

  private static void AddBody(string uuid, Vector3? velocity = null)
  {
    Bodies.Add(uuid);
    Velocities[uuid] = velocity ?? Vector3.Zero;
  }

  private static void AddNamed(string name, string uuid, Vector3 position)
  {
    AddObject(uuid, position);
    FakeNatives.SetString($"World.findObjectByName:{name}", uuid);
    FakeNatives.Set($"World.objectExists:{uuid}", true);
  }

  private static void AddPlayers(params (string Uuid, int Slot, Vector3 Position)[] players)
  {
    FakeNatives.SetString("World.getAllObjectUuids", string.Join(",", players.Select(p => p.Uuid)));
    foreach (var (uuid, slot, position) in players)
    {
      AddObject(uuid, position);
      FakeNatives.Set($"PlayerController.has:{uuid}", true);
      FakeNatives.Set($"PlayerController.getPlayerSlot:{uuid}", slot);
    }
  }

  private static T Make<T>(params (string Field, object Value)[] fields) where T : ScriptBase, new()
  {
    var script = new T { EntityId = "me" };
    script.initComponents();
    foreach (var (field, value) in fields)
    {
      typeof(T).GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(script, value);
    }

    return script;
  }

  private static void Tick(ScriptBase script, float dt)
  {
    script.tickTimers(dt);
    script.fixedUpdate(dt);
  }

  private static void Ticks(ScriptBase script, int count, float dt)
  {
    for (var tick = 0; tick < count; tick++)
    {
      Tick(script, dt);
    }
  }

  private static Vector3 Facing(string uuid) => StarterUtils.rotate(Rotations[uuid], new Vector3(0f, 0f, -1f));

  private static int PlayerScans() => FakeNatives.Calls.Count(c => c == "World.getAllObjectUuids()");

  // LookAtTarget

  [Fact]
  public void LookAtTarget_SnapsToFaceTheTargetWhenTurnSpeedIsZero()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 0f));
    AssertVec(new Vector3(0f, 0f, -1f), Facing("me"));

    Tick(script, 0.1f);

    AssertVec(new Vector3(1f, 0f, 0f), Facing("me"));
  }

  [Fact]
  public void LookAtTarget_TurnsNoFasterThanTurnSpeedAndArrivesOnTheTarget()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 90f));

    Tick(script, 0.1f);
    Assert.Equal(-9f, Rotations["me"].Y, 0.05f);
    Assert.Equal(0f, Rotations["me"].X, 0.05f);
    Assert.Equal(0f, Rotations["me"].Z, 0.05f);

    Tick(script, 0.1f);
    Assert.Equal(-18f, Rotations["me"].Y, 0.05f);

    Ticks(script, 12, 0.1f);
    AssertVec(new Vector3(1f, 0f, 0f), Facing("me"));
    Assert.Equal(-90f, Rotations["me"].Y, 0.01f);
  }

  [Fact]
  public void LookAtTarget_TakesTheShortWayRoundWhenTheTargetIsBehind()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(-1f, 0f, 1f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 90f));
    Rotations["me"] = new Vector3(0f, 170f, 0f);

    Tick(script, 0.1f);

    // The goal yaw is 135 degrees, 35 away, so one step turns the 9 degrees toward it, never the long way. Compared
    // as a facing because an Euler yaw past 90 degrees reads back as an equivalent pitch/roll flip.
    AssertVec(StarterUtils.rotate(new Vector3(0f, 161f, 0f), new Vector3(0f, 0f, -1f)), Facing("me"));
  }

  [Fact]
  public void LookAtTarget_PlayerSlotWinsOverTheNamedTarget()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    AddPlayers(("p1", 1, new Vector3(-10f, 0f, 0f)));
    var bySlot = Make<LookAtTarget>(("m_target", "Target"), ("m_targetPlayerSlot", 1), ("m_turnSpeed", 0f));

    Tick(bySlot, 0.1f);
    AssertVec(new Vector3(-1f, 0f, 0f), Facing("me"));

    Rotations["me"] = Vector3.Zero;
    var byName = Make<LookAtTarget>(("m_target", "Target"), ("m_targetPlayerSlot", -1), ("m_turnSpeed", 0f));
    Tick(byName, 0.1f);
    AssertVec(new Vector3(1f, 0f, 0f), Facing("me"));
  }

  [Fact]
  public void LookAtTarget_MissingPlayerSlotScansOncePerSecondAndWarnsOnce()
  {
    AddObject("me", Vector3.Zero);
    AddPlayers(("p0", 0, new Vector3(0f, 0f, 10f)));
    var script = Make<LookAtTarget>(("m_targetPlayerSlot", 3), ("m_turnSpeed", 0f));

    Ticks(script, 8, 0.25f);

    Assert.Equal(2, PlayerScans());
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("slot 3", warnings[0]);
    AssertVec(new Vector3(0f, 0f, -1f), Facing("me"));

    AddPlayers(("p0", 0, new Vector3(0f, 0f, 10f)), ("p3", 3, new Vector3(10f, 0f, 0f)));
    Ticks(script, 4, 0.25f);
    AssertVec(new Vector3(1f, 0f, 0f), Facing("me"));
  }

  [Fact]
  public void LookAtTarget_YawOnlyKeepsPitchAndRollAtZero()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 5f, 0f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 0f));

    Tick(script, 0.1f);

    AssertVec(new Vector3(1f, 0f, 0f), Facing("me"));
    Assert.Equal(0f, Rotations["me"].X, 1e-3f);
    Assert.Equal(0f, Rotations["me"].Z, 1e-3f);
  }

  [Fact]
  public void LookAtTarget_WithoutYawOnlyPitchesTowardTheTarget()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 5f, 0f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 0f), ("m_yawOnly", false));

    Tick(script, 0.1f);

    AssertVec(Vector3.Normalize(new Vector3(10f, 5f, 0f)), Facing("me"));
  }

  [Fact]
  public void LookAtTarget_MissingTargetNeverThrowsAndLeavesTheRotation()
  {
    AddObject("me", Vector3.Zero);
    var script = Make<LookAtTarget>(("m_target", "Ghost"), ("m_turnSpeed", 0f));

    Ticks(script, 6, 0.25f);

    Assert.Equal(Vector3.Zero, Rotations["me"]);
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("Ghost", warnings[0]);
  }

  [Fact]
  public void LookAtTarget_OnARigidBodyWarnsOnceAndOtherwiseDoesNot()
  {
    AddObject("me", Vector3.Zero);
    var free = Make<LookAtTarget>();
    free.start();
    Assert.Empty(Warnings());

    AddBody("me");
    var withBody = Make<LookAtTarget>();
    withBody.start();

    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("RigidBody", warnings[0]);
  }

  // Follower

  [Fact]
  public void Follower_WithoutARigidBodyStepsAndStopsAtTheStopDistance()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    var script = Make<Follower>(("m_target", "Target"));

    Ticks(script, 8, 0.25f);

    Assert.Equal(8, Moves.Count);
    Assert.All(Moves, move => AssertVec(new Vector3(1f, 0f, 0f), move.Delta));
    Assert.Equal(8f, Positions["me"].X, 1e-3f);

    Ticks(script, 4, 0.25f);
    Assert.Equal(8, Moves.Count);
    Assert.Equal(8f, Positions["me"].X, 1e-3f);
  }

  [Fact]
  public void Follower_WithoutARigidBodyNeverOvershootsTheStopDistance()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(3f, 0f, 0f));
    var script = Make<Follower>(("m_target", "Target"));

    Tick(script, 1.0f);

    Assert.Single(Moves);
    AssertVec(new Vector3(1f, 0f, 0f), Moves[0].Delta);
  }

  [Fact]
  public void Follower_IgnoreHeightKeepsItOnTheGroundAndOffClosesTheGapInThreeD()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 5f, 0f));
    var flat = Make<Follower>(("m_target", "Target"));
    Ticks(flat, 30, 0.25f);
    Assert.Equal(0f, Positions["me"].Y, 1e-4f);
    Assert.Equal(8f, Positions["me"].X, 1e-3f);

    Positions["me"] = Vector3.Zero;
    var flying = Make<Follower>(("m_target", "Target"), ("m_ignoreHeight", false));
    Ticks(flying, 40, 0.25f);
    Assert.True(Positions["me"].Y > 0.5f);
    Assert.Equal(2f, Vector3.Distance(Positions["me"], new Vector3(10f, 5f, 0f)), 1e-3f);
  }

  [Fact]
  public void Follower_WithARigidBodySteersItsVelocityInsteadOfMoving()
  {
    AddObject("me", Vector3.Zero);
    AddBody("me");
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    var script = Make<Follower>(("m_target", "Target"));

    Tick(script, 0.1f);

    Assert.Empty(Moves);
    var force = Assert.Single(Forces);
    Assert.Equal("me", force.Uuid);
    Assert.Equal((int)ForceMode.VelocityChange, force.Mode);
    AssertVec(new Vector3(0.04f, 0f, 0f), force.Force, 1e-4f);
  }

  [Fact]
  public void Follower_WithARigidBodyBrakesInsideTheStopDistanceAndLeavesGravityAlone()
  {
    AddObject("me", Vector3.Zero);
    AddBody("me");
    AddNamed("Target", "t1", new Vector3(1f, 0f, 0f));
    var script = Make<Follower>(("m_target", "Target"));

    Tick(script, 0.1f);
    Assert.Empty(Forces);

    Velocities["me"] = new Vector3(0.4f, -0.3f, 0f);
    Tick(script, 0.1f);
    var force = Assert.Single(Forces);
    AssertVec(new Vector3(-0.04f, 0f, 0f), force.Force, 1e-4f);
  }

  [Fact]
  public void Follower_PlayerSlotWinsOverTheNamedTarget()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    AddPlayers(("p1", 1, new Vector3(0f, 0f, 10f)));
    var script = Make<Follower>(("m_target", "Target"), ("m_targetPlayerSlot", 1));

    Tick(script, 0.25f);

    var move = Assert.Single(Moves);
    AssertVec(new Vector3(0f, 0f, 1f), move.Delta);
  }

  [Fact]
  public void Follower_MissingTargetNeverThrowsAndStaysPut()
  {
    AddObject("me", Vector3.Zero);
    var script = Make<Follower>(("m_target", "Ghost"));

    Ticks(script, 6, 0.25f);

    Assert.Empty(Moves);
    Assert.Empty(Forces);
    Assert.Single(Warnings());
  }

  // PingPongMover

  private static PingPongMover Platform(bool ease, bool carry = true, Vector3? offset = null)
  {
    AddObject("me", Vector3.Zero, new Vector3(2f, 0.5f, 2f));
    var script = Make<PingPongMover>(
      ("m_offset", offset ?? new Vector3(0f, 4f, 0f)), ("m_ease", ease), ("m_carryRiders", carry));
    script.start();
    return script;
  }

  [Fact]
  public void PingPongMover_MovesAtSpeedAlongTheOffsetWithoutEase()
  {
    var script = Platform(false);

    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(0f, 1f, 0f), Positions["me"]);
  }

  [Fact]
  public void PingPongMover_EaseAppliesSmoothstepToTheLegProgress()
  {
    var script = Platform(true);

    Ticks(script, 2, 0.25f);
    // A quarter of the way: smoothstep(0.25) = 0.15625 of the 4-unit offset.
    AssertVec(new Vector3(0f, 0.625f, 0f), Positions["me"]);

    Ticks(script, 2, 0.25f);
    AssertVec(new Vector3(0f, 2f, 0f), Positions["me"]);
  }

  [Fact]
  public void PingPongMover_PausesAtTheEndThenReturnsToTheStart()
  {
    var script = Platform(false);

    Ticks(script, 8, 0.25f);
    AssertVec(new Vector3(0f, 4f, 0f), Positions["me"]);

    Ticks(script, 3, 0.25f);
    AssertVec(new Vector3(0f, 4f, 0f), Positions["me"]);

    Tick(script, 0.25f);
    AssertVec(new Vector3(0f, 3.5f, 0f), Positions["me"]);

    Ticks(script, 7, 0.25f);
    AssertVec(Vector3.Zero, Positions["me"]);

    Ticks(script, 3, 0.25f);
    AssertVec(Vector3.Zero, Positions["me"]);

    Tick(script, 0.25f);
    AssertVec(new Vector3(0f, 0.5f, 0f), Positions["me"]);
  }

  [Fact]
  public void PingPongMover_StartsFromWhereItWasPlaced()
  {
    AddObject("me", new Vector3(5f, 1f, -2f), new Vector3(2f, 0.5f, 2f));
    var script = Make<PingPongMover>(("m_ease", false));
    script.start();

    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(5f, 2f, -2f), Positions["me"]);
  }

  [Fact]
  public void PingPongMover_OnARigidBodyWarnsOnceAndOtherwiseDoesNot()
  {
    Platform(false);
    Assert.Empty(Warnings());

    AddBody("me");
    var script = Make<PingPongMover>();
    script.start();

    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("RigidBody", warnings[0]);
  }

  private static PingPongMover SidewaysPlatform(bool carry)
  {
    var script = Platform(false, carry, new Vector3(4f, 0f, 0f));
    AddObject("above", new Vector3(0f, 1.5f, 0f));
    AddBody("above");
    AddObject("below", new Vector3(0f, 0.2f, 0f));
    AddBody("below");
    AddObject("bodyless", new Vector3(0f, 1.5f, 0f));
    script.onCollisionEnter("above");
    script.onCollisionEnter("below");
    script.onCollisionEnter("bodyless");
    return script;
  }

  [Fact]
  public void PingPongMover_CarriesAnAboveTopRiderByThePlatformDisplacement()
  {
    var script = SidewaysPlatform(true);

    Tick(script, 0.25f);
    AssertVec(new Vector3(0.5f, 0f, 0f), Positions["me"]);
    AssertVec(new Vector3(0.5f, 1.5f, 0f), Positions["above"]);

    Tick(script, 0.25f);
    AssertVec(new Vector3(1f, 1.5f, 0f), Positions["above"]);
  }

  [Fact]
  public void PingPongMover_IgnoresARiderBelowTheTopAndOneWithoutARigidBody()
  {
    var script = SidewaysPlatform(true);

    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(1f, 1.5f, 0f), Positions["above"]);
    AssertVec(new Vector3(0f, 0.2f, 0f), Positions["below"]);
    AssertVec(new Vector3(0f, 1.5f, 0f), Positions["bodyless"]);
  }

  [Fact]
  public void PingPongMover_StopsCarryingARiderAfterItLeaves()
  {
    var script = SidewaysPlatform(true);
    Tick(script, 0.25f);
    AssertVec(new Vector3(0.5f, 1.5f, 0f), Positions["above"]);

    script.onCollisionExit("above");
    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(1.5f, 0f, 0f), Positions["me"]);
    AssertVec(new Vector3(0.5f, 1.5f, 0f), Positions["above"]);
  }

  [Fact]
  public void PingPongMover_CarryRidersOffMovesNobody()
  {
    var script = SidewaysPlatform(false);

    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(1f, 0f, 0f), Positions["me"]);
    AssertVec(new Vector3(0f, 1.5f, 0f), Positions["above"]);
  }

  [Fact]
  public void PingPongMover_DoesNotCarryRidersWhilePaused()
  {
    var script = SidewaysPlatform(true);

    Ticks(script, 8, 0.25f);
    AssertVec(new Vector3(4f, 1.5f, 0f), Positions["above"]);

    Ticks(script, 3, 0.25f);
    AssertVec(new Vector3(4f, 1.5f, 0f), Positions["above"]);

    Tick(script, 0.25f);
    AssertVec(new Vector3(3.5f, 1.5f, 0f), Positions["above"]);
  }
}
