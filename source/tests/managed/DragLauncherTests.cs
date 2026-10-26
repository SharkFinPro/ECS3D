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

// DragLauncher against FakeNatives: the cursor ray, the raycast hit and the player's mouse are canned per tick.
[Collection("BridgeInstances")]
public unsafe class DragLauncherTests : IDisposable
{
  private readonly IDisposable _natives = FakeNatives.Install();
  private readonly LogBindings _originalLog = NativeBindings.Log;

  private static readonly List<(int Level, string Message)> LogCalls = new();

  [UnmanagedCallersOnly]
  private static void RecordLog(int level, IntPtr message)
    => LogCalls.Add((level, Marshal.PtrToStringUTF8(message) ?? ""));

  public DragLauncherTests()
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

  private static string[] Infos() => LogCalls.Where(c => c.Level == 2).Select(c => c.Message).ToArray();

  private static void SetField(object script, string name, object value) =>
    script.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(script, value);

  private static List<string> Launches() =>
    FakeNatives.Calls.Where(c => c.StartsWith("RigidBody.setVelocity(", StringComparison.Ordinal)).ToList();

  private static float[] Velocity(string call) =>
    call.Substring(call.IndexOf('(') + 1).TrimEnd(')').Split(',').Skip(1)
      .Select(a => float.Parse(a, CultureInfo.InvariantCulture)).ToArray();

  private static void AssertVec(Vector3 expected, float[] actual, float tolerance = 1e-3f)
  {
    var v = new Vector3(actual[0], actual[1], actual[2]);
    Assert.True(Vector3.Distance(expected, v) <= tolerance, $"expected {expected} but got {v}");
  }

  // The camera looks down -Z from z = 10, so a cursor ray at x runs straight down through (x, y, 0).
  private static void Cursor(float x, float y = 0f)
  {
    FakeNatives.Set("InputUtils.cursorRayForObject", true);
    FakeNatives.Set("InputUtils.cursorRayForObject.origin", new Vector3(x, y, 10f));
    FakeNatives.Set("InputUtils.cursorRayForObject.direction", new Vector3(0f, 0f, -1f));
  }

  private static void Mouse(bool down) => FakeNatives.Set("InputUtils.mouseButtonForObject", down);

  private static void Focus(bool focused) => FakeNatives.Set("InputUtils.windowIsFocusedForObject", focused);

  private static void HitBall(bool rigid = true)
  {
    FakeNatives.SetString("World.raycast", "ball,10,0,0,0,0,0,1");
    FakeNatives.SetString("World.getObjectName", "Ball");
    FakeNatives.Set("World.objectExists:ball", true);
    FakeNatives.Set("RigidBody.has:ball", rigid);
  }

  private static DragLauncher NewLauncher()
  {
    var launcher = new DragLauncher { EntityId = "player" };
    launcher.initComponents();
    Focus(true);
    Mouse(false);
    Cursor(0f);
    HitBall();
    return launcher;
  }

  // Press on the ball with the cursor at x = 0, move the cursor to dragX and let go.
  private static void Drag(DragLauncher launcher, float dragX)
  {
    Mouse(true);
    Cursor(0f);
    launcher.fixedUpdate(0.02f);
    Cursor(dragX);
    launcher.fixedUpdate(0.02f);
    Mouse(false);
    launcher.fixedUpdate(0.02f);
  }

  private static void Press(DragLauncher launcher, float dragX)
  {
    Mouse(true);
    Cursor(0f);
    launcher.fixedUpdate(0.02f);
    Cursor(dragX);
    launcher.fixedUpdate(0.02f);
  }

  [Fact]
  public void Release_LaunchesOppositeTheDragWithSpeedProportionalToItsLength()
  {
    var launcher = NewLauncher();

    Drag(launcher, -2f);
    Drag(launcher, 3f);

    var launches = Launches();
    Assert.Equal(2, launches.Count);
    AssertVec(new Vector3(8f, 0f, 0f), Velocity(launches[0]));
    AssertVec(new Vector3(-12f, 0f, 0f), Velocity(launches[1]));
    Assert.Equal(-1.5f, Velocity(launches[1])[0] / Velocity(launches[0])[0], 1e-3f);
  }

  [Fact]
  public void Launch_SpeedIsCappedAtMaxSpeedKeepingTheDirection()
  {
    var launcher = NewLauncher();
    SetField(launcher, "m_maxSpeed", 6f);

    Drag(launcher, -2f);
    Drag(launcher, -1f);

    var launches = Launches();
    AssertVec(new Vector3(6f, 0f, 0f), Velocity(launches[0]));
    AssertVec(new Vector3(4f, 0f, 0f), Velocity(launches[1]));
  }

  [Fact]
  public void Release_LogsOneInfoLineUnlessLogLaunchesIsOff()
  {
    var launcher = NewLauncher();
    Drag(launcher, -2f);
    Assert.Equal(new[] { "DragLauncher launched 'Ball' at 8.0" }, Infos());

    LogCalls.Clear();
    SetField(launcher, "m_logLaunches", false);
    Drag(launcher, -2f);
    Assert.Empty(Infos());
    Assert.Equal(2, Launches().Count);
  }

  [Fact]
  public void ShortDragIsAClickAndLaunchesNothing()
  {
    var launcher = NewLauncher();

    Drag(launcher, -0.05f);
    Assert.Empty(Launches());

    Drag(launcher, -0.5f);
    Assert.Single(Launches());
  }

  [Fact]
  public void PressOnEmptySpaceGrabsNothing()
  {
    var launcher = NewLauncher();
    FakeNatives.SetString("World.raycast", null);

    Drag(launcher, -2f);
    Assert.Empty(Launches());

    HitBall();
    Drag(launcher, -2f);
    Assert.Single(Launches());
  }

  [Fact]
  public void PressOnAnObjectWithoutARigidBodyGrabsNothing()
  {
    var launcher = NewLauncher();
    HitBall(rigid: false);

    Drag(launcher, -2f);
    Assert.Empty(Launches());

    HitBall();
    Drag(launcher, -2f);
    Assert.Single(Launches());
  }

  [Fact]
  public void PressWithNoCursorRayGrabsNothing()
  {
    var launcher = NewLauncher();
    FakeNatives.Set("InputUtils.cursorRayForObject", false);
    Mouse(true);
    launcher.fixedUpdate(0.02f);

    Cursor(-2f);
    launcher.fixedUpdate(0.02f);
    Mouse(false);
    launcher.fixedUpdate(0.02f);

    Assert.Empty(Launches());
  }

  [Fact]
  public void LosingFocusDropsTheGrab()
  {
    var launcher = NewLauncher();
    Press(launcher, -2f);

    Focus(false);
    Mouse(false);
    launcher.fixedUpdate(0.02f);
    Focus(true);
    launcher.fixedUpdate(0.02f);

    Assert.Empty(Launches());
  }

  [Fact]
  public void TargetDisappearingDropsTheGrab()
  {
    var launcher = NewLauncher();
    Press(launcher, -2f);

    FakeNatives.Set("World.objectExists:ball", false);
    Mouse(false);
    launcher.fixedUpdate(0.02f);

    Assert.Empty(Launches());
  }

  [Fact]
  public void CursorRayLostMidDragKeepsTheLastGoodDragPoint()
  {
    var launcher = NewLauncher();
    Press(launcher, -2f);

    FakeNatives.Set("InputUtils.cursorRayForObject", false);
    Mouse(false);
    launcher.fixedUpdate(0.02f);

    AssertVec(new Vector3(8f, 0f, 0f), Velocity(Assert.Single(Launches())));
  }

  [Fact]
  public void GrabRayIsCastFromTheCursorAndSkipsThePlayersOwnObject()
  {
    var launcher = NewLauncher();
    Cursor(1.5f, 0.5f);
    Mouse(true);
    launcher.fixedUpdate(0.02f);

    var args = Assert.Single(FakeNatives.Calls, c => c.StartsWith("World.raycast(", StringComparison.Ordinal))
      .Substring("World.raycast(".Length).TrimEnd(')').Split(',');
    var ray = args.Take(7).Select(a => float.Parse(a, CultureInfo.InvariantCulture)).ToArray();
    Assert.Equal(new[] { 1.5f, 0.5f, 10f, 0f, 0f, -1f, 100f }, ray);
    Assert.Equal("player", args[8]);
  }
}
