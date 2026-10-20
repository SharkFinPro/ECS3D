using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Shared FakeNatives setup for the motion-target starter tests. Transforms and rigid bodies are stateful here
// (positions persist between ticks and per uuid), so movement can be followed over several ticks. Warnings come
// through a recording Log binding.
public abstract unsafe class StarterMotionTargetsFixture : IDisposable
{
  private readonly IDisposable _natives = FakeNatives.Install();
  private readonly LogBindings _originalLog = NativeBindings.Log;

  protected static readonly List<(int Level, string Message)> LogCalls = new();
  protected static readonly Dictionary<string, Vector3> Positions = new();
  protected static readonly Dictionary<string, Vector3> Scales = new();
  protected static readonly Dictionary<string, Vector3> Rotations = new();
  protected static readonly Dictionary<string, Vector3> Velocities = new();
  protected static readonly Dictionary<string, Vector3> LocalPositions = new();
  private static readonly HashSet<string> Bodies = new();
  protected static readonly List<(string Uuid, Vector3 Delta)> Moves = new();
  protected static readonly List<(string Uuid, Vector3 Force, int Mode)> Forces = new();

  protected StarterMotionTargetsFixture()
  {
    LogCalls.Clear();
    Positions.Clear();
    Scales.Clear();
    Rotations.Clear();
    Velocities.Clear();
    LocalPositions.Clear();
    Bodies.Clear();
    Moves.Clear();
    Forces.Clear();

    NativeBindings.Log = new LogBindings { write = &RecordLog };
    NativeBindings.Transform = new TransformBindings
    {
      getPosition = &TransformGetPosition,
      getScale = &TransformGetScale,
      getRotation = &TransformGetRotation,
      getLocalPosition = &TransformGetLocalPosition,
      getLocalRotation = &TransformGetRotation,
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
  private static void TransformGetLocalPosition(IntPtr u, float* x, float* y, float* z) =>
    Out(LocalPositions.ContainsKey(Id(u)) ? LocalPositions : Positions, u, x, y, z);

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

  protected static string[] Warnings() =>
    LogCalls.Where(c => c.Level == 3).Select(c => c.Message).ToArray();

  protected static void AssertVec(Vector3 expected, Vector3 actual, float tolerance = 1e-3f)
  {
    Assert.True(Vector3.Distance(expected, actual) <= tolerance, $"expected {expected} but got {actual}");
  }

  protected static void AddObject(string uuid, Vector3 position, Vector3? scale = null)
  {
    Positions[uuid] = position;
    Scales[uuid] = scale ?? Vector3.One;
    Rotations[uuid] = Vector3.Zero;
  }

  protected static void AddBody(string uuid, Vector3? velocity = null)
  {
    Bodies.Add(uuid);
    Velocities[uuid] = velocity ?? Vector3.Zero;
  }

  protected static void AddNamed(string name, string uuid, Vector3 position)
  {
    AddObject(uuid, position);
    FakeNatives.SetString($"World.findObjectByName:{name}", uuid);
    FakeNatives.Set($"World.objectExists:{uuid}", true);
  }

  protected static void AddPlayers(params (string Uuid, int Slot, Vector3 Position)[] players)
  {
    FakeNatives.SetString("World.getAllObjectUuids", string.Join(",", players.Select(p => p.Uuid)));
    foreach (var (uuid, slot, position) in players)
    {
      AddObject(uuid, position);
      FakeNatives.Set($"PlayerController.has:{uuid}", true);
      FakeNatives.Set($"PlayerController.getPlayerSlot:{uuid}", slot);
    }
  }

  protected static T Make<T>(params (string Field, object Value)[] fields) where T : ScriptBase, new()
  {
    var script = new T { EntityId = "me" };
    script.initComponents();
    foreach (var (field, value) in fields)
    {
      typeof(T).GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(script, value);
    }

    return script;
  }

  protected static void Tick(ScriptBase script, float dt)
  {
    script.tickTimers(dt);
    script.fixedUpdate(dt);
  }

  protected static void Ticks(ScriptBase script, int count, float dt)
  {
    for (var tick = 0; tick < count; tick++)
    {
      Tick(script, dt);
    }
  }

  protected static Vector3 Facing(string uuid) => StarterUtils.rotate(Rotations[uuid], new Vector3(0f, 0f, -1f));

  protected static int PlayerScans() => FakeNatives.Calls.Count(c => c == "World.getAllObjectUuids()");
}
