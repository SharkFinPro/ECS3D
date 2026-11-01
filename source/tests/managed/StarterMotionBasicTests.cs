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

// Spinner, Bobber and Orbiter against FakeNatives. Warnings come through a recording Log binding.
[Collection("BridgeInstances")]
public unsafe class StarterMotionBasicTests : IDisposable
{
  private readonly IDisposable _natives = FakeNatives.Install();
  private readonly LogBindings _originalLog = NativeBindings.Log;

  private static readonly List<(int Level, string Message)> LogCalls = new();

  [UnmanagedCallersOnly]
  private static void RecordLog(int level, IntPtr message)
    => LogCalls.Add((level, Marshal.PtrToStringUTF8(message) ?? ""));

  public StarterMotionBasicTests()
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

  private static string[] Warnings() => LogCalls.Where(c => c.Level == 3).Select(c => c.Message).ToArray();

  private static T NewScript<T>(params (string Field, object Value)[] fields) where T : ScriptBase, new()
  {
    var script = new T { EntityId = "u-self" };
    script.initComponents();
    foreach (var (field, value) in fields)
    {
      typeof(T).GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(script, value);
    }

    return script;
  }

  private static void AssertVec(Vector3 expected, Vector3 actual, float tolerance = 1e-3f)
  {
    Assert.True(Vector3.Distance(expected, actual) <= tolerance, $"expected {expected} but got {actual}");
  }

  // The vector arguments of every call recorded as "Struct.fn(uuid,x,y,z)", after the uuid.
  private static List<Vector3> Vec3Calls(string name) =>
    FakeNatives.Calls
      .Where(c => c.StartsWith(name + "(", StringComparison.Ordinal))
      .Select(c => c.Substring(c.IndexOf('(') + 1).TrimEnd(')').Split(',').Skip(1)
        .Select(a => float.Parse(a, CultureInfo.InvariantCulture)).ToArray())
      .Select(a => new Vector3(a[0], a[1], a[2]))
      .ToList();

  private static void Present(string name, string uuid)
  {
    FakeNatives.SetString($"World.findObjectByName:{name}", uuid);
    FakeNatives.Set($"World.objectExists:{uuid}", true);
  }

  private static Vector3 Rodrigues(Vector3 v, Vector3 axis, float degrees)
  {
    var k = Vector3.Normalize(axis);
    var theta = degrees * MathF.PI / 180.0f;
    return v * MathF.Cos(theta) + Vector3.Cross(k, v) * MathF.Sin(theta) +
           k * (Vector3.Dot(k, v) * (1.0f - MathF.Cos(theta)));
  }

  // Runs fixedUpdate ticks, feeding each written rotation back as the next read, as the engine would. A nonzero
  // readNoise offsets every read component, standing in for a store that does not return exactly what was set.
  private static void SpinTicks(Spinner spinner, int ticks, float dt) => SpinTicks(spinner, ticks, dt, 0f);

  private static void SpinTicks(Spinner spinner, int ticks, float dt, float readNoise)
  {
    for (var tick = 0; tick < ticks; tick++)
    {
      FakeNatives.Calls.Clear();
      spinner.fixedUpdate(dt);
      var written = Vec3Calls("Transform.setRotation");
      Assert.Single(written);
      FakeNatives.Set("Transform.getLocalRotation", written[0] + new Vector3(readNoise));
    }
  }

  private static Vector3 CurrentRotation() => FakeNatives.Get<Vector3>("Transform.getLocalRotation");

  [Fact]
  public void Spinner_TurnsAboutWorldYByDegreesPerSecondTimesElapsedTime()
  {
    var spinner = NewScript<Spinner>(("m_degreesPerSecond", 60.0f));

    SpinTicks(spinner, 10, 0.1f);

    var forward = new Vector3(0f, 0f, -1f);
    AssertVec(new Vector3(-MathF.Sqrt(0.75f), 0f, -0.5f), StarterUtils.rotate(CurrentRotation(), forward));
    AssertVec(new Vector3(0f, 1f, 0f), StarterUtils.rotate(CurrentRotation(), new Vector3(0f, 1f, 0f)));
  }

  [Fact]
  public void Spinner_TurnsAboutATiltedAxisGivenUnnormalized()
  {
    var axis = new Vector3(2f, 2f, 0f);
    var spinner = NewScript<Spinner>(("m_axis", axis), ("m_degreesPerSecond", 60.0f));

    SpinTicks(spinner, 20, 0.1f);

    var v = new Vector3(1f, 2f, 3f);
    AssertVec(Rodrigues(v, axis, 120.0f), StarterUtils.rotate(CurrentRotation(), v));
  }

  [Fact]
  public void Spinner_TurnsAboutTheWorldAxisNotTheObjectsOwn()
  {
    var start = new Vector3(90f, 0f, 0f);
    FakeNatives.Set("Transform.getLocalRotation", start);
    var spinner = NewScript<Spinner>(("m_degreesPerSecond", 45.0f));

    SpinTicks(spinner, 10, 0.1f);

    // Pitch 90 sends +Y to +Z; a world-Y turn of 45 degrees then sends +Z toward +X. A turn about the object's
    // own Y axis (world Z by then) would leave +Y at +Z's side instead.
    var up = new Vector3(0f, 1f, 0f);
    var expected = Rodrigues(StarterUtils.rotate(start, up), new Vector3(0f, 1f, 0f), 45.0f);
    AssertVec(new Vector3(MathF.Sqrt(0.5f), 0f, MathF.Sqrt(0.5f)), expected);
    AssertVec(expected, StarterUtils.rotate(CurrentRotation(), up));
  }

  [Fact]
  public void Spinner_KeepsTurningCleanlyThroughYawNinety()
  {
    var spinner = NewScript<Spinner>(("m_degreesPerSecond", 45.0f));

    SpinTicks(spinner, 30, 0.1f);

    var v = new Vector3(1f, 2f, 3f);
    AssertVec(Rodrigues(v, new Vector3(0f, 1f, 0f), 135.0f), StarterUtils.rotate(CurrentRotation(), v));
  }

  [Fact]
  public void Spinner_KeepsItsOwnOrientationWhenTheReadBackIsOnlyWithinToleranceOfWhatItWrote()
  {
    var spinner = NewScript<Spinner>(("m_degreesPerSecond", 45.0f));

    SpinTicks(spinner, 16, 0.1f, 9e-4f);

    // Reseeding from the noisy read every tick would add its offset to the turn each time (about 1e-3 here).
    var v = new Vector3(1f, 2f, 3f);
    AssertVec(Rodrigues(v, new Vector3(0f, 1f, 0f), 72.0f), StarterUtils.rotate(CurrentRotation(), v), 3e-4f);
  }

  [Fact]
  public void Spinner_PicksUpARotationChangedByAnotherSourceMidSpin()
  {
    var spinner = NewScript<Spinner>(("m_degreesPerSecond", 45.0f));
    SpinTicks(spinner, 10, 0.1f);

    var external = new Vector3(0f, 0f, 90f);
    FakeNatives.Set("Transform.getLocalRotation", external);
    SpinTicks(spinner, 10, 0.1f);

    // 45 degrees about world Y applied to the externally set roll of 90.
    var v = new Vector3(1f, 0f, 0f);
    AssertVec(Rodrigues(StarterUtils.rotate(external, v), new Vector3(0f, 1f, 0f), 45.0f),
      StarterUtils.rotate(CurrentRotation(), v));
  }

  [Fact]
  public void Spinner_OnARigidBodySetsAngularVelocityInDegreesPerSecondAndNeverSetsRotation()
  {
    FakeNatives.Set("RigidBody.has", true);
    var spinner = NewScript<Spinner>(("m_axis", new Vector3(0f, 2f, 0f)), ("m_degreesPerSecond", 45.0f));

    spinner.fixedUpdate(0.1f);

    Assert.Equal(new[] { new Vector3(0f, 45f, 0f) }, Vec3Calls("RigidBody.setAngularVelocity"));
    Assert.Empty(Vec3Calls("Transform.setRotation"));
    Assert.Empty(Warnings());
  }

  [Fact]
  public void Spinner_ZeroAxisDoesNothing()
  {
    var idle = NewScript<Spinner>(("m_axis", Vector3.Zero));
    idle.fixedUpdate(0.1f);
    Assert.Empty(Vec3Calls("Transform.setRotation"));

    FakeNatives.Set("RigidBody.has", true);
    idle.fixedUpdate(0.1f);
    Assert.Empty(Vec3Calls("RigidBody.setAngularVelocity"));

    FakeNatives.Set("RigidBody.has", false);
    var turning = NewScript<Spinner>();
    turning.fixedUpdate(0.1f);
    Assert.Single(Vec3Calls("Transform.setRotation"));
  }

  [Fact]
  public void Bobber_ReachesAmplitudeAlongAxisAtAQuarterPeriod()
  {
    FakeNatives.Set("Transform.getLocalPosition", new Vector3(1f, 2f, 3f));
    var bobber = NewScript<Bobber>(("m_axis", new Vector3(2f, 0f, 0f)), ("m_amplitude", 2.0f),
      ("m_frequencyHz", 0.25f));
    bobber.start();
    FakeNatives.Set("Transform.getLocalPosition", new Vector3(9f, 9f, 9f));

    bobber.fixedUpdate(0.25f);
    AssertVec(new Vector3(1f, 2f, 3f) + new Vector3(2f * MathF.Sin(MathF.PI / 8.0f), 0f, 0f),
      Vec3Calls("Transform.setPosition").Last());

    for (var tick = 0; tick < 3; tick++)
    {
      bobber.fixedUpdate(0.25f);
    }

    AssertVec(new Vector3(3f, 2f, 3f), Vec3Calls("Transform.setPosition").Last());
  }

  [Fact]
  public void Bobber_PhaseShiftsTheWave()
  {
    var plain = NewScript<Bobber>();
    plain.start();
    plain.fixedUpdate(0.0f);
    AssertVec(Vector3.Zero, Vec3Calls("Transform.setPosition").Last());

    var shifted = NewScript<Bobber>(("m_phaseDegrees", 90.0f));
    shifted.start();
    shifted.fixedUpdate(0.0f);
    AssertVec(new Vector3(0f, 0.5f, 0f), Vec3Calls("Transform.setPosition").Last());
  }

  [Fact]
  public void Bobber_WarnsOnceWithARigidBodyAndNotWithout()
  {
    var free = NewScript<Bobber>();
    free.start();
    free.fixedUpdate(0.1f);
    Assert.Empty(Warnings());

    FakeNatives.Set("RigidBody.has", true);
    var bound = NewScript<Bobber>();
    bound.start();
    for (var tick = 0; tick < 3; tick++)
    {
      bound.fixedUpdate(0.1f);
    }

    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("RigidBody", warnings[0]);
  }

  [Fact]
  public void Orbiter_ReachesTheQuarterPointAfterAQuarterPeriodAroundTheFixedCenter()
  {
    var orbiter = NewScript<Orbiter>(("m_center", new Vector3(10f, 0f, 0f)), ("m_radius", 5.0f),
      ("m_periodSeconds", 8.0f));
    orbiter.start();

    orbiter.fixedUpdate(0.0f);
    AssertVec(new Vector3(15f, 0f, 0f), Vec3Calls("Transform.setPosition").Last());

    for (var tick = 0; tick < 4; tick++)
    {
      orbiter.fixedUpdate(0.5f);
    }

    AssertVec(new Vector3(10f, 0f, -5f), Vec3Calls("Transform.setPosition").Last());
    Assert.DoesNotContain(FakeNatives.Calls, c => c.StartsWith("World.findObjectByName(", StringComparison.Ordinal));
    Assert.Empty(Warnings());
  }

  [Fact]
  public void Orbiter_StartAngleOffsetsTheStartingPoint()
  {
    var orbiter = NewScript<Orbiter>(("m_startAngle", 90.0f), ("m_radius", 2.0f));
    orbiter.start();

    orbiter.fixedUpdate(0.0f);

    AssertVec(new Vector3(0f, 0f, -2f), Vec3Calls("Transform.setPosition").Last());
  }

  [Fact]
  public void Orbiter_FollowsTheNamedCenterAsItMovesAndIgnoresTheFixedCenter()
  {
    Present("Planet", "u-planet");
    FakeNatives.Set("Transform.has", true);
    FakeNatives.Set("Transform.getPosition", new Vector3(0f, 0f, 0f));
    var orbiter = NewScript<Orbiter>(("m_centerObject", "Planet"), ("m_center", new Vector3(50f, 50f, 50f)),
      ("m_radius", 5.0f), ("m_periodSeconds", 8.0f));
    orbiter.start();

    orbiter.fixedUpdate(0.5f);
    var firstAngle = 22.5f * MathF.PI / 180.0f;
    AssertVec(new Vector3(5f * MathF.Cos(firstAngle), 0f, -5f * MathF.Sin(firstAngle)),
      Vec3Calls("Transform.setPosition").Last());

    FakeNatives.Set("Transform.getPosition", new Vector3(100f, 0f, 0f));
    orbiter.fixedUpdate(0.5f);
    var secondAngle = 45.0f * MathF.PI / 180.0f;
    AssertVec(new Vector3(100f + 5f * MathF.Cos(secondAngle), 0f, -5f * MathF.Sin(secondAngle)),
      Vec3Calls("Transform.setPosition").Last());
    Assert.Empty(Warnings());
  }

  [Fact]
  public void Orbiter_HoldsPositionAndWarnsOnceWhileTheCenterObjectIsMissing()
  {
    var orbiter = NewScript<Orbiter>(("m_centerObject", "Ghost"), ("m_center", new Vector3(7f, 7f, 7f)));
    orbiter.start();
    FakeNatives.Set("Transform.has", true);
    FakeNatives.Set("Transform.getPosition", new Vector3(20f, 0f, 0f));

    orbiter.fixedUpdate(0.5f);
    orbiter.fixedUpdate(0.5f);

    Assert.Empty(Vec3Calls("Transform.setPosition"));
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("Ghost", warnings[0]);

    Present("Ghost", "u-ghost");
    orbiter.fixedUpdate(0.5f);
    orbiter.fixedUpdate(0.5f);

    Assert.NotEmpty(Vec3Calls("Transform.setPosition"));
    Assert.True(Vec3Calls("Transform.setPosition").Last().X > 20f);
    Assert.Single(Warnings());
  }

  [Fact]
  public void Orbiter_FaceTravelTurnsForwardAlongThePath()
  {
    var facing = NewScript<Orbiter>(("m_faceTravel", true), ("m_periodSeconds", 8.0f));
    facing.start();

    facing.fixedUpdate(0.0f);
    var atStart = Vec3Calls("Transform.setRotation").Last();
    AssertVec(new Vector3(0f, 0f, -1f), StarterUtils.rotate(atStart, new Vector3(0f, 0f, -1f)));

    // An eighth of the way round, forward has turned 45 degrees toward -X.
    facing.fixedUpdate(1.0f);
    var eighth = Vec3Calls("Transform.setRotation").Last();
    var half = MathF.Sqrt(0.5f);
    AssertVec(new Vector3(-half, 0f, -half), StarterUtils.rotate(eighth, new Vector3(0f, 0f, -1f)));
    AssertVec(new Vector3(0f, 1f, 0f), StarterUtils.rotate(eighth, new Vector3(0f, 1f, 0f)));

    FakeNatives.Calls.Clear();
    var still = NewScript<Orbiter>(("m_periodSeconds", 8.0f));
    still.start();
    still.fixedUpdate(1.0f);
    Assert.NotEmpty(Vec3Calls("Transform.setPosition"));
    Assert.Empty(Vec3Calls("Transform.setRotation"));
  }

  [Fact]
  public void Orbiter_FaceTravelKeepsTheOrbitAxisAsUpForATiltedPlane()
  {
    var orbiter = NewScript<Orbiter>(("m_faceTravel", true), ("m_axis", new Vector3(0f, 0f, 1f)));
    orbiter.start();

    orbiter.fixedUpdate(0.0f);

    // At angle 0 the object sits at +X and travels along +Y with the axis (+Z) as up.
    AssertVec(new Vector3(5f, 0f, 0f), Vec3Calls("Transform.setPosition").Last());
    var rotation = Vec3Calls("Transform.setRotation").Last();
    AssertVec(new Vector3(0f, 1f, 0f), StarterUtils.rotate(rotation, new Vector3(0f, 0f, -1f)));
    AssertVec(new Vector3(0f, 0f, 1f), StarterUtils.rotate(rotation, new Vector3(0f, 1f, 0f)));
  }

  [Fact]
  public void Orbiter_WarnsOnceWhenItHasAParentAndNotWhenItIsARoot()
  {
    var root = NewScript<Orbiter>();
    root.start();
    root.fixedUpdate(0.1f);
    Assert.Empty(Warnings());

    FakeNatives.Set("Transform.getPosition", new Vector3(1f, 0f, 0f));
    var child = NewScript<Orbiter>();
    child.start();
    child.fixedUpdate(0.1f);
    child.fixedUpdate(0.1f);

    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("parent", warnings[0]);
  }

  [Fact]
  public void Orbiter_WarnsOnceWithARigidBodyAndNotWithout()
  {
    var free = NewScript<Orbiter>();
    free.start();
    free.fixedUpdate(0.1f);
    Assert.Empty(Warnings());

    FakeNatives.Set("RigidBody.has", true);
    var bound = NewScript<Orbiter>();
    bound.start();
    bound.fixedUpdate(0.1f);
    bound.fixedUpdate(0.1f);

    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("RigidBody", warnings[0]);
  }
}
