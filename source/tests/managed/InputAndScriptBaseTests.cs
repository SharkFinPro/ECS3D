using System;
using System.Numerics;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Each component wrapper routes a call to the matching native function with its own uuid, in order, and
// reads vectors, scalars and bools back from what native answered. Values are chosen distinct per axis so a
// swapped or dropped component shows up.
[Collection("BridgeInstances")]
public class InputAndScriptBaseTests : IDisposable
{
  private const string Id = "obj-1";

  private readonly IDisposable _natives = FakeNatives.Install();

  public void Dispose()
  {
    var error = FakeNatives.LastError;
    _natives.Dispose();
    Assert.Null(error);
  }

  private static string Last => FakeNatives.Calls[^1];

  // ---- PlayerController ----

  [Fact]
  public void PlayerController_GetsAndSetsSlot()
  {
    var controller = new PlayerController(Id);
    FakeNatives.Set("PlayerController.getPlayerSlot", 3);

    Assert.Equal(3, controller.getPlayerSlot());
    controller.setPlayerSlot(-1);

    Assert.Equal(new[] { "PlayerController.getPlayerSlot(obj-1)", "PlayerController.setPlayerSlot(obj-1,-1)" },
                 FakeNatives.Calls);
  }

  // ---- InputUtils / PlayerInput ----

  [Fact]
  public void InputUtils_ReadsAggregateKeyAndFocus()
  {
    FakeNatives.Set("InputUtils.keyIsPressed", true);
    FakeNatives.Set("InputUtils.windowIsFocused", true);

    Assert.True(InputUtils.keyIsPressed(Key.W));
    Assert.True(InputUtils.windowIsFocused());
    Assert.Equal(new[] { "InputUtils.keyIsPressed(87)", "InputUtils.windowIsFocused()" }, FakeNatives.Calls);

    FakeNatives.Set("InputUtils.keyIsPressed", false);
    FakeNatives.Set("InputUtils.windowIsFocused", false);
    Assert.False(InputUtils.keyIsPressed(Key.UP));
    Assert.False(InputUtils.windowIsFocused());
    Assert.Equal("InputUtils.windowIsFocused()", Last);
  }

  [Fact]
  public void PlayerInput_KeyAndEdgeQueriesPassUuidAndKeyCode()
  {
    var input = new PlayerInput(Id);
    FakeNatives.Set("InputUtils.keyIsPressedForObject", true);
    FakeNatives.Set("InputUtils.wasKeyPressedThisTickForObject", true);
    FakeNatives.Set("InputUtils.wasKeyReleasedThisTickForObject", false);
    FakeNatives.Set("InputUtils.windowIsFocusedForObject", true);

    Assert.True(input.keyIsPressed(Key.A));
    Assert.True(input.wasPressedThisTick(Key.LEFT));
    Assert.False(input.wasReleasedThisTick(Key.RIGHT));
    Assert.True(input.windowIsFocused());

    Assert.Equal(new[]
    {
      "InputUtils.keyIsPressedForObject(obj-1,65)",
      "InputUtils.wasKeyPressedThisTickForObject(obj-1,263)",
      "InputUtils.wasKeyReleasedThisTickForObject(obj-1,262)",
      "InputUtils.windowIsFocusedForObject(obj-1)"
    }, FakeNatives.Calls);

    FakeNatives.Set("InputUtils.keyIsPressedForObject", false);
    Assert.False(input.keyIsPressed(Key.A));
  }

  [Fact]
  public void PlayerInput_MouseReadsPositionDeltaScrollAndButtons()
  {
    var input = new PlayerInput(Id);
    FakeNatives.Set("InputUtils.mousePositionForObject", new Vector2(100f, 200f));
    FakeNatives.Set("InputUtils.mouseDeltaForObject", new Vector2(-3f, 4f));
    FakeNatives.Set("InputUtils.scrollForObject", 1.5f);
    FakeNatives.Set("InputUtils.mouseButtonForObject", true);

    Assert.Equal(new Vector2(100f, 200f), input.mousePosition());
    Assert.Equal(new Vector2(-3f, 4f), input.mouseDelta());
    Assert.Equal(1.5f, input.scroll());
    Assert.True(input.mouseButton(MouseButton.Right));
    Assert.Equal("InputUtils.mouseButtonForObject(obj-1,1)", Last);

    FakeNatives.Set("InputUtils.mouseButtonForObject", false);
    Assert.False(input.mouseButton(MouseButton.Middle));
    Assert.Equal("InputUtils.mouseButtonForObject(obj-1,2)", Last);
  }

  [Fact]
  public void PlayerInput_TryGetCursorRayMarshalsTheRayAndTheFoundFlag()
  {
    var input = new PlayerInput(Id);
    FakeNatives.Set("InputUtils.cursorRayForObject", true);
    FakeNatives.Set("InputUtils.cursorRayForObject.origin", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("InputUtils.cursorRayForObject.direction", new Vector3(0f, -1f, 0f));

    Assert.True(input.tryGetCursorRay(out var origin, out var direction));
    Assert.Equal(new Vector3(1f, 2f, 3f), origin);
    Assert.Equal(new Vector3(0f, -1f, 0f), direction);
    Assert.Equal("InputUtils.cursorRayForObject(obj-1)", Last);

    FakeNatives.Set("InputUtils.cursorRayForObject", false);
    FakeNatives.Set("InputUtils.cursorRayForObject.origin", Vector3.Zero);
    FakeNatives.Set("InputUtils.cursorRayForObject.direction", Vector3.Zero);
    Assert.False(input.tryGetCursorRay(out origin, out direction));
    Assert.Equal(Vector3.Zero, origin);
    Assert.Equal(Vector3.Zero, direction);
  }

  // ---- ScriptBase ----

  private sealed class Probe : ScriptBase
  {
    internal Transform Transform => transform;
    internal RigidBody RigidBody => rigidBody;
    internal Camera Camera => camera;
    internal PlayerInput Input => input;

    internal bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit) =>
      raycast(origin, direction, maxDistance, out hit);

    internal string[] Overlap(Vector3 center, float radius) => overlapSphere(center, radius);
  }

  private static Probe MakeProbe(string uuid)
  {
    var probe = new Probe { EntityId = uuid };
    probe.initComponents();
    return probe;
  }

  [Fact]
  public void ScriptBase_InitComponentsBindsEveryWrapperToItsOwnUuid()
  {
    var probe = MakeProbe("self-1");
    var other = MakeProbe("self-2");

    probe.Transform.start();
    probe.RigidBody.setMass(1f);
    probe.Camera.setFov(50f);
    FakeNatives.Set("InputUtils.keyIsPressedForObject", true);
    Assert.True(probe.Input.keyIsPressed(Key.W));
    other.Transform.stop();

    Assert.Equal(new[]
    {
      "Transform.start(self-1)",
      "RigidBody.setMass(self-1,1)",
      "Camera.setFov(self-1,50)",
      "InputUtils.keyIsPressedForObject(self-1,87)",
      "Transform.stop(self-2)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void ScriptBase_QueriesIgnoreTheScriptsOwnObject()
  {
    var probe = MakeProbe("self-1");
    FakeNatives.SetString("World.raycast", "");
    FakeNatives.SetString("World.overlapSphere", "x,y");

    Assert.False(probe.Raycast(Vector3.Zero, Vector3.UnitX, 5f, out _));
    Assert.Equal(new[] { "x", "y" }, probe.Overlap(new Vector3(1f, 2f, 3f), 2f));

    Assert.Equal(new[]
    {
      "World.raycast(0,0,0,1,0,0,5,4294967295,self-1)",
      "World.overlapSphere(1,2,3,2,4294967295,self-1)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void NativeBindings_AreRestoredWhenTheScopeIsDisposed()
  {
    Assert.True(FakeNatives.IsInstalled);

    _natives.Dispose();

    Assert.False(FakeNatives.IsInstalled);
  }
}
