using System;
using System.Globalization;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// The shared starter helpers and PlayerScript's key bindings, against FakeNatives. Warnings come through
// a recording Log binding.
[Collection("BridgeInstances")]
public unsafe class StarterUtilsTests : IDisposable
{
  private readonly IDisposable _natives = FakeNatives.Install();
  private readonly LogBindings _originalLog = NativeBindings.Log;

  private static readonly List<(int Level, string Message)> LogCalls = new();

  [UnmanagedCallersOnly]
  private static void RecordLog(int level, IntPtr message)
    => LogCalls.Add((level, Marshal.PtrToStringUTF8(message) ?? ""));

  public StarterUtilsTests()
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

  private string[] Warnings() =>
    LogCalls.Where(call => call.Level == 3).Select(call => call.Message).ToArray();

  private static int Lookups() => FakeNatives.Calls.Count(c => c.StartsWith("World.findObjectByName(", StringComparison.Ordinal));

  private static void Present(string name, string uuid)
  {
    FakeNatives.SetString($"World.findObjectByName:{name}", uuid);
    FakeNatives.Set($"World.objectExists:{uuid}", true);
  }

  private static void AssertVec(Vector3 expected, Vector3 actual, float tolerance = 1e-4f)
  {
    Assert.True(Vector3.Distance(expected, actual) <= tolerance, $"expected {expected} but got {actual}");
  }

  [Fact]
  public void NamedObject_ResolvesPresentObjectAndCachesIt()
  {
    Present("Sun", "u-sun");
    var sun = new NamedObject("Test script") { Name = "Sun" };

    Assert.True(sun.tryResolve(0.25f, out var first));
    Assert.True(sun.tryResolve(0.25f, out var second));

    Assert.Equal("u-sun", first);
    Assert.Equal("u-sun", second);
    Assert.Equal(1, Lookups());
    Assert.Empty(Warnings());
  }

  [Fact]
  public void NamedObject_MissingObjectWarnsOnceAndRetriesOncePerSecond()
  {
    var ghost = new NamedObject("Test script on Door") { Name = "Ghost" };

    var lookupsAfterTick = new List<int>();
    for (var tick = 0; tick < 24; tick++)
    {
      Assert.False(ghost.tryResolve(0.125f, out var uuid));
      Assert.Equal("", uuid);
      lookupsAfterTick.Add(Lookups());
    }

    Assert.Equal(new[] { 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3 }, lookupsAfterTick);
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("Ghost", warnings[0]);
    Assert.Contains("Test script on Door", warnings[0]);
  }

  [Fact]
  public void NamedObject_ResolvesOnceTheObjectAppears()
  {
    var late = new NamedObject("Test script") { Name = "Late" };
    for (var tick = 0; tick < 2; tick++)
    {
      Assert.False(late.tryResolve(0.25f, out _));
    }

    Present("Late", "u-late");
    var resolved = false;
    var uuid = "";
    for (var tick = 0; tick < 4 && !resolved; tick++)
    {
      resolved = late.tryResolve(0.25f, out uuid);
    }

    Assert.True(resolved);
    Assert.Equal("u-late", uuid);
    Assert.Single(Warnings());
  }

  [Fact]
  public void NamedObject_ReResolvesAfterCachedObjectStopsExisting()
  {
    Present("Sun", "u-sun");
    var sun = new NamedObject("Test script") { Name = "Sun" };
    Assert.True(sun.tryResolve(0.25f, out var before));
    Assert.Equal("u-sun", before);

    FakeNatives.Set("World.objectExists:u-sun", false);
    Present("Sun", "u-sun-2");

    Assert.True(sun.tryResolve(0.25f, out var after));
    Assert.Equal("u-sun-2", after);
    Assert.Equal(2, Lookups());
    Assert.Empty(Warnings());
  }

  [Fact]
  public void NamedObject_WarnsAgainOnlyAfterResolvingAndLosingTheObject()
  {
    var sun = new NamedObject("Test script") { Name = "Sun" };
    for (var tick = 0; tick < 6; tick++)
    {
      sun.tryResolve(0.25f, out _);
    }

    Assert.Single(Warnings());

    Present("Sun", "u-sun");
    var resolved = false;
    for (var tick = 0; tick < 4 && !resolved; tick++)
    {
      resolved = sun.tryResolve(0.25f, out _);
    }

    Assert.True(resolved);

    FakeNatives.Set("World.objectExists:u-sun", false);
    FakeNatives.SetString("World.findObjectByName:Sun", null);
    for (var tick = 0; tick < 6; tick++)
    {
      sun.tryResolve(0.25f, out _);
    }

    Assert.Equal(2, Warnings().Length);
  }

  [Fact]
  public void NamedObject_EmptyNameLooksNothingUpAndLogsNothing()
  {
    var unnamed = new NamedObject("Test script");

    for (var tick = 0; tick < 8; tick++)
    {
      Assert.False(unnamed.tryResolve(0.25f, out var uuid));
      Assert.Equal("", uuid);
    }

    Assert.Equal(0, Lookups());
    Assert.Empty(Warnings());

    Present("Sun", "u-sun");
    unnamed.Name = "Sun";
    Assert.True(unnamed.tryResolve(0.25f, out var found));
    Assert.Equal("u-sun", found);
    Assert.Equal(1, Lookups());
  }

  [Fact]
  public void ResolveObject_ReturnsUuidForNameAndEmptyForEmptyName()
  {
    Present("Sun", "u-sun");

    Assert.Equal("u-sun", StarterUtils.resolveObject("Sun"));
    Assert.Equal("", StarterUtils.resolveObject(""));
    Assert.Equal("", StarterUtils.resolveObject("Nobody"));
    Assert.Equal(new[] { "World.findObjectByName(Sun)", "World.findObjectByName(Nobody)" }, FakeNatives.Calls);
  }

  [Fact]
  public void FindPlayerBody_FindsTheObjectHoldingTheSlot()
  {
    FakeNatives.SetString("World.getAllObjectUuids", "rock,p0,p1");
    FakeNatives.Set("PlayerController.has:p0", true);
    FakeNatives.Set("PlayerController.has:p1", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:p0", 0);
    FakeNatives.Set("PlayerController.getPlayerSlot:p1", 1);

    Assert.Equal("p1", StarterUtils.findPlayerBody(1));
    Assert.Equal("p0", StarterUtils.findPlayerBody(0));
  }

  [Fact]
  public void FindPlayerBody_IsEmptyForAMissingSlotAndForNegativeSlot()
  {
    FakeNatives.SetString("World.getAllObjectUuids", "p0,p1");
    FakeNatives.Set("PlayerController.has:p0", true);
    FakeNatives.Set("PlayerController.has:p1", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:p0", 0);
    FakeNatives.Set("PlayerController.getPlayerSlot:p1", 1);
    Assert.Equal("p1", StarterUtils.findPlayerBody(1));

    Assert.Equal("", StarterUtils.findPlayerBody(2));
    FakeNatives.Calls.Clear();
    Assert.Equal("", StarterUtils.findPlayerBody(-1));
    Assert.Empty(FakeNatives.Calls);
  }

  [Theory]
  [InlineData("w", Key.W)]
  [InlineData(" Space ", Key.SPACE)]
  [InlineData("LEFT", Key.LEFT)]
  [InlineData("E", Key.E)]
  [InlineData("up", Key.UP)]
  public void ParseKey_AcceptsNamesCaseInsensitivelyAndTrimmed(string name, Key expected)
  {
    Assert.Equal(expected, StarterUtils.parseKey(name));
  }

  [Theory]
  [InlineData("")]
  [InlineData("   ")]
  [InlineData("Banana")]
  [InlineData("65")]
  [InlineData("W,A")]
  public void ParseKey_RejectsEverythingElse(string name)
  {
    Assert.Null(StarterUtils.parseKey(name));
  }

  [Fact]
  public void SpaceKeyIsGlfwSpace()
  {
    Assert.Equal(32, (int)Key.SPACE);
  }

  [Theory]
  [InlineData(30f, 40f, 50f)]
  [InlineData(-20f, 10f, 75f)]
  [InlineData(45f, -60f, 120f)]
  [InlineData(0f, 0f, 0f)]
  public void EulerRoundTripsThroughQuaternion(float x, float y, float z)
  {
    var euler = new Vector3(x, y, z);

    AssertVec(euler, StarterUtils.quaternionToEuler(StarterUtils.eulerToQuaternion(euler)), 1e-3f);
  }

  [Fact]
  public void EulerToQuaternion_NinetyAboutXIsHalfSqrtTwoInXAndW()
  {
    var q = StarterUtils.eulerToQuaternion(new Vector3(90f, 0f, 0f));
    var half = MathF.Sqrt(0.5f);

    Assert.Equal(half, q.X, 1e-5f);
    Assert.Equal(half, q.W, 1e-5f);
    Assert.Equal(0f, q.Y, 1e-5f);
    Assert.Equal(0f, q.Z, 1e-5f);
  }

  [Fact]
  public void QuaternionToEuler_DegeneratePitchTermsUseTwiceAtan2OfXAndW()
  {
    // w=0.5, x=0.5, y=-0.5, z=0.5: both pitch terms are exactly 0, so glm takes 2 * atan2(x, w) = 90; yaw is
    // asin(-1) = -90 and roll is atan2(0, 0) = 0. The ordinary pitch formula would give 0.
    AssertVec(new Vector3(90f, -90f, 0f), StarterUtils.quaternionToEuler(new Quaternion(0.5f, -0.5f, 0.5f, 0.5f)),
      1e-3f);
  }

  [Fact]
  public void Rotate_PitchIsAppliedBeforeYaw()
  {
    AssertVec(new Vector3(1f, 0f, 0f), StarterUtils.rotate(new Vector3(90f, 90f, 0f), new Vector3(0f, 1f, 0f)));
  }

  [Fact]
  public void Rotate_YawIsAppliedBeforeRoll()
  {
    AssertVec(new Vector3(0f, 0f, -1f), StarterUtils.rotate(new Vector3(0f, 90f, 90f), new Vector3(1f, 0f, 0f)));
  }

  [Fact]
  public void Rotate_YawTurnsForwardTowardMinusX()
  {
    AssertVec(new Vector3(-1f, 0f, 0f), StarterUtils.rotate(new Vector3(0f, 90f, 0f), new Vector3(0f, 0f, -1f)));
  }

  [Fact]
  public void PerTick_ScalesARateByTheStep()
  {
    Assert.Equal(0.5f, StarterUtils.perTick(30f, 1f / 60f), 1e-5f);
  }

  private static PlayerScript NewPlayer()
  {
    var player = new PlayerScript { EntityId = "p1" };
    player.initComponents();
    FakeNatives.Set("InputUtils.windowIsFocusedForObject", true);
    return player;
  }

  private static void Press(Key key) => FakeNatives.Set($"InputUtils.keyIsPressedForObject:{(int)key}", true);

  private static void Step(PlayerScript player, float dt = 0.1f)
  {
    player.variableUpdate();
    player.fixedUpdate(dt);
  }

  private static Vector3 LastSteer()
  {
    var call = FakeNatives.Calls.Last(c => c.StartsWith("RigidBody.applyForce(", StringComparison.Ordinal));
    var args = call.Substring(call.IndexOf('(') + 1).TrimEnd(')').Split(',');
    return new Vector3(
      float.Parse(args[1], CultureInfo.InvariantCulture),
      float.Parse(args[2], CultureInfo.InvariantCulture),
      float.Parse(args[3], CultureInfo.InvariantCulture));
  }

  private static void SetField(PlayerScript player, string name, string value) =>
    typeof(PlayerScript).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(player, value);

  [Fact]
  public void PlayerScript_DefaultBindingsMoveWithWasdAndParseWithoutWarnings()
  {
    var player = NewPlayer();

    Press(Key.W);
    Step(player);
    AssertVec(new Vector3(0f, 0f, -0.09f), LastSteer());

    FakeNatives.Calls.Clear();
    Press(Key.D);
    Step(player);
    Assert.True(LastSteer().X > 0f);
    Assert.Empty(Warnings());
  }

  [Theory]
  [InlineData(Key.S, 0f, 0f, 0.09f)]
  [InlineData(Key.DOWN, 0f, 0f, 0.09f)]
  [InlineData(Key.A, -0.09f, 0f, 0f)]
  [InlineData(Key.LEFT, -0.09f, 0f, 0f)]
  [InlineData(Key.UP, 0f, 0f, -0.09f)]
  [InlineData(Key.RIGHT, 0.09f, 0f, 0f)]
  public void PlayerScript_EveryDefaultAndArrowKeySteersItsOwnWay(Key key, float x, float y, float z)
  {
    var player = NewPlayer();

    Press(key);
    Step(player);

    AssertVec(new Vector3(x, y, z), LastSteer());
    Assert.Empty(Warnings());
  }

  [Fact]
  public void PlayerScript_HeadingTurnsWithTheBodysYaw()
  {
    var player = NewPlayer();
    FakeNatives.Set("Transform.getRotation", new Vector3(0f, 90f, 0f));

    Press(Key.W);
    Step(player);

    AssertVec(new Vector3(-0.09f, 0f, 0f), LastSteer());
  }

  [Fact]
  public void PlayerScript_JumpsOnSpaceAndNotOnX()
  {
    var onX = NewPlayer();
    Press(Key.X);
    Step(onX);
    Step(onX);
    Assert.Equal(0f, LastSteer().Y, 1e-5f);

    FakeNatives.Calls.Clear();
    var onSpace = NewPlayer();
    Press(Key.SPACE);
    Step(onSpace);
    Step(onSpace);
    Assert.Equal(1.5f, LastSteer().Y, 1e-4f);
  }

  [Fact]
  public void PlayerScript_UnknownForwardKeyWarnsOnceAndLeavesForwardUnbound()
  {
    var player = NewPlayer();
    SetField(player, "m_forward", "Banana");

    Press(Key.W);
    for (var tick = 0; tick < 3; tick++)
    {
      Step(player);
      AssertVec(Vector3.Zero, LastSteer());
    }

    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("Banana", warnings[0]);

    Press(Key.UP);
    Step(player);
    Assert.True(LastSteer().Z < 0f);

    Assert.Single(Warnings());
  }

  [Fact]
  public void PlayerScript_ReparsesAKeyWhenItsFieldChanges()
  {
    var player = NewPlayer();
    SetField(player, "m_forward", "Q");
    Press(Key.Q);
    Step(player);
    Assert.True(LastSteer().Z < 0f);

    FakeNatives.Calls.Clear();
    SetField(player, "m_forward", "Banana");
    Step(player);
    AssertVec(Vector3.Zero, LastSteer());
    Assert.Single(Warnings());
  }
}
