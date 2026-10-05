using System;
using System.Numerics;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Each component wrapper routes a call to the matching native function with its own uuid, in order, and
// reads vectors, scalars and bools back from what native answered. Values are chosen distinct per axis so a
// swapped or dropped component shows up.
[Collection("BridgeInstances")]
public class ComponentWrapperTests : IDisposable
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

  // ---- Transform ----

  [Fact]
  public void Transform_ReadsVectorsFromTheMatchingNative()
  {
    var transform = new Transform(Id);
    FakeNatives.Set("Transform.getPosition", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("Transform.getScale", new Vector3(4f, 5f, 6f));
    FakeNatives.Set("Transform.getRotation", new Vector3(7f, 8f, 9f));
    FakeNatives.Set("Transform.getLocalPosition", new Vector3(10f, 11f, 12f));
    FakeNatives.Set("Transform.getLocalScale", new Vector3(13f, 14f, 15f));
    FakeNatives.Set("Transform.getLocalRotation", new Vector3(16f, 17f, 18f));

    Assert.Equal(new Vector3(1f, 2f, 3f), transform.getPosition());
    Assert.Equal(new Vector3(4f, 5f, 6f), transform.getScale());
    Assert.Equal(new Vector3(7f, 8f, 9f), transform.getRotation());
    Assert.Equal(new Vector3(10f, 11f, 12f), transform.getLocalPosition());
    Assert.Equal(new Vector3(13f, 14f, 15f), transform.getLocalScale());
    Assert.Equal(new Vector3(16f, 17f, 18f), transform.getLocalRotation());
    Assert.Equal(new[]
    {
      "Transform.getPosition(obj-1)",
      "Transform.getScale(obj-1)",
      "Transform.getRotation(obj-1)",
      "Transform.getLocalPosition(obj-1)",
      "Transform.getLocalScale(obj-1)",
      "Transform.getLocalRotation(obj-1)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void Transform_WritesRouteToTheMatchingNative()
  {
    var transform = new Transform(Id);

    transform.setScale(1f, 2f, 3f);
    transform.setRotation(4f, 5f, 6f);
    transform.move(7f, 8f, 9f);
    transform.setPosition(10f, 11f, 12f);
    transform.setPosition(new Vector3(13f, 14f, 15f));
    transform.start();
    transform.stop();

    Assert.Equal(new[]
    {
      "Transform.setScale(obj-1,1,2,3)",
      "Transform.setRotation(obj-1,4,5,6)",
      "Transform.move(obj-1,7,8,9)",
      "Transform.setPosition(obj-1,10,11,12)",
      "Transform.setPosition(obj-1,13,14,15)",
      "Transform.start(obj-1)",
      "Transform.stop(obj-1)"
    }, FakeNatives.Calls);
  }

  // ---- RigidBody ----

  [Fact]
  public void RigidBody_ApplyForceSendsForcePositionAndMode()
  {
    var body = new RigidBody(Id);

    body.applyForce(1f, 2f, 3f, 4f, 5f, 6f);
    body.applyForce(new Vector3(1f, 2f, 3f), new Vector3(4f, 5f, 6f), ForceMode.Impulse);
    body.applyForce(7f, 8f, 9f, 0f, 0f, 0f, ForceMode.VelocityChange);

    Assert.Equal(new[]
    {
      "RigidBody.applyForce(obj-1,1,2,3,4,5,6,0)",
      "RigidBody.applyForce(obj-1,1,2,3,4,5,6,2)",
      "RigidBody.applyForce(obj-1,7,8,9,0,0,0,3)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void RigidBody_VelocitySettersRoute()
  {
    var body = new RigidBody(Id);

    body.setVelocity(1f, 2f, 3f);
    body.setVelocity(new Vector3(4f, 5f, 6f));
    body.setAngularVelocity(7f, 8f, 9f);
    body.setAngularVelocity(new Vector3(10f, 11f, 12f));

    Assert.Equal(new[]
    {
      "RigidBody.setVelocity(obj-1,1,2,3)",
      "RigidBody.setVelocity(obj-1,4,5,6)",
      "RigidBody.setAngularVelocity(obj-1,7,8,9)",
      "RigidBody.setAngularVelocity(obj-1,10,11,12)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void RigidBody_GettersReadNativeValues()
  {
    var body = new RigidBody(Id);
    FakeNatives.Set("RigidBody.getVelocity", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("RigidBody.getAngularVelocity", new Vector3(4f, 5f, 6f));
    FakeNatives.Set("RigidBody.getMass", 7f);
    FakeNatives.Set("RigidBody.getFriction", 0.25f);
    FakeNatives.Set("RigidBody.getGravity", -9.5f);

    Assert.Equal(new Vector3(1f, 2f, 3f), body.getVelocity());
    Assert.Equal(new Vector3(4f, 5f, 6f), body.getAngularVelocity());
    Assert.Equal(7f, body.getMass());
    Assert.Equal(0.25f, body.getFriction());
    Assert.Equal(-9.5f, body.getGravity());
    Assert.Equal("RigidBody.getGravity(obj-1)", Last);
  }

  [Fact]
  public void RigidBody_ScalarSettersRoute()
  {
    var body = new RigidBody(Id);

    body.setMass(2f);
    body.setFriction(0.5f);
    body.setGravity(-3f);

    Assert.Equal(new[]
    {
      "RigidBody.setMass(obj-1,2)",
      "RigidBody.setFriction(obj-1,0.5)",
      "RigidBody.setGravity(obj-1,-3)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void RigidBody_BoolsAreReadAndWrittenAsBytes()
  {
    var body = new RigidBody(Id);

    FakeNatives.Set("RigidBody.isFalling", true);
    FakeNatives.Set("RigidBody.getDoGravity", true);
    Assert.True(body.isFalling());
    Assert.True(body.getDoGravity());

    FakeNatives.Set("RigidBody.isFalling", false);
    FakeNatives.Set("RigidBody.getDoGravity", false);
    Assert.False(body.isFalling());
    Assert.False(body.getDoGravity());

    FakeNatives.Calls.Clear();
    body.setDoGravity(true);
    body.setDoGravity(false);
    Assert.Equal(new[] { "RigidBody.setDoGravity(obj-1,1)", "RigidBody.setDoGravity(obj-1,0)" }, FakeNatives.Calls);
  }

  // ---- Camera ----

  [Fact]
  public void Camera_ReadsDirectionAndProjection()
  {
    var camera = new Camera(Id);
    FakeNatives.Set("Camera.getDirection", new Vector3(0.5f, 1f, -2f));
    FakeNatives.Set("Camera.getFov", 60f);
    FakeNatives.Set("Camera.getNearPlane", 0.5f);
    FakeNatives.Set("Camera.getFarPlane", 500f);

    Assert.Equal(new Vector3(0.5f, 1f, -2f), camera.getDirection());
    Assert.Equal(60f, camera.getFov());
    Assert.Equal(0.5f, camera.getNearPlane());
    Assert.Equal(500f, camera.getFarPlane());
    Assert.Equal("Camera.getFarPlane(obj-1)", Last);
  }

  [Fact]
  public void Camera_WritesRoute()
  {
    var camera = new Camera(Id);

    camera.setDirection(1f, 2f, 3f);
    camera.setDirection(new Vector3(4f, 5f, 6f));
    camera.setFov(70f);
    camera.setNearPlane(0.25f);
    camera.setFarPlane(900f);
    camera.setActive(true);
    camera.setActive(false);

    Assert.Equal(new[]
    {
      "Camera.setDirection(obj-1,1,2,3)",
      "Camera.setDirection(obj-1,4,5,6)",
      "Camera.setFov(obj-1,70)",
      "Camera.setNearPlane(obj-1,0.25)",
      "Camera.setFarPlane(obj-1,900)",
      "Camera.setActive(obj-1,1)",
      "Camera.setActive(obj-1,0)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void Camera_HasAndIsActiveReadBytes()
  {
    var camera = new Camera(Id);

    FakeNatives.Set("Camera.has", true);
    FakeNatives.Set("Camera.isActive", false);
    Assert.True(camera.has());
    Assert.False(camera.isActive());

    FakeNatives.Set("Camera.has", false);
    FakeNatives.Set("Camera.isActive", true);
    Assert.False(camera.has());
    Assert.True(camera.isActive());
  }

  // ---- Collider ----

  [Fact]
  public void Collider_ShapeTriggerLayerAndMask()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.getShape", 1);
    FakeNatives.Set("Collider.getIsTrigger", true);
    FakeNatives.Set("Collider.getLayer", 0x10u);
    FakeNatives.Set("Collider.getMask", 0xF0F0F0F0u);

    Assert.Equal(ColliderShape.Box, collider.getShape());
    Assert.True(collider.getIsTrigger());
    Assert.Equal(0x10u, collider.getLayer());
    Assert.Equal(0xF0F0F0F0u, collider.getMask());

    FakeNatives.Set("Collider.getShape", 0);
    Assert.Equal(ColliderShape.None, collider.getShape());
  }

  [Fact]
  public void Collider_SettersSendValueAndReturnNativeResult()
  {
    var collider = new Collider(Id);

    FakeNatives.Set("Collider.setIsTrigger", true);
    FakeNatives.Set("Collider.setLayer", true);
    FakeNatives.Set("Collider.setMask", false);
    Assert.True(collider.setIsTrigger(true));
    Assert.True(collider.setLayer(4u));
    Assert.False(collider.setMask(0xFFFFFFFFu));

    Assert.Equal(new[]
    {
      "Collider.setIsTrigger(obj-1,1)",
      "Collider.setLayer(obj-1,4)",
      "Collider.setMask(obj-1,4294967295)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void Collider_BoxAccessorsReturnVectorOnlyWhenNativeSucceeds()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.getBoxOffset", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("Collider.getBoxSize", new Vector3(4f, 5f, 6f));

    FakeNatives.Set("Collider.getBoxOffset.ok", true);
    FakeNatives.Set("Collider.getBoxSize.ok", true);
    Assert.True(collider.tryGetBoxOffset(out var offset));
    Assert.Equal(new Vector3(1f, 2f, 3f), offset);
    Assert.True(collider.tryGetBoxSize(out var size));
    Assert.Equal(new Vector3(4f, 5f, 6f), size);

    FakeNatives.Set("Collider.getBoxOffset.ok", false);
    FakeNatives.Set("Collider.getBoxSize.ok", false);
    Assert.False(collider.tryGetBoxOffset(out offset));
    Assert.Equal(Vector3.Zero, offset);
    Assert.False(collider.tryGetBoxSize(out size));
    Assert.Equal(Vector3.Zero, size);
  }

  [Fact]
  public void Collider_BoxSettersSendComponentsAndReturnNativeResult()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.setBoxOffset", true);
    FakeNatives.Set("Collider.setBoxSize", false);

    Assert.True(collider.setBoxOffset(1f, 2f, 3f));
    Assert.True(collider.setBoxOffset(new Vector3(4f, 5f, 6f)));
    Assert.False(collider.setBoxSize(7f, 8f, 9f));
    Assert.False(collider.setBoxSize(new Vector3(10f, 11f, 12f)));

    Assert.Equal(new[]
    {
      "Collider.setBoxOffset(obj-1,1,2,3)",
      "Collider.setBoxOffset(obj-1,4,5,6)",
      "Collider.setBoxSize(obj-1,7,8,9)",
      "Collider.setBoxSize(obj-1,10,11,12)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void Collider_SphereAccessorsReturnValuesOnlyWhenNativeSucceeds()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.getSphereOffset", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("Collider.getSphereRadius", 2.5f);

    FakeNatives.Set("Collider.getSphereOffset.ok", true);
    FakeNatives.Set("Collider.getSphereRadius.ok", true);
    Assert.True(collider.tryGetSphereOffset(out var offset));
    Assert.Equal(new Vector3(1f, 2f, 3f), offset);
    Assert.True(collider.tryGetSphereRadius(out var radius));
    Assert.Equal(2.5f, radius);

    FakeNatives.Set("Collider.getSphereOffset.ok", false);
    FakeNatives.Set("Collider.getSphereRadius.ok", false);
    Assert.False(collider.tryGetSphereOffset(out offset));
    Assert.Equal(Vector3.Zero, offset);
    Assert.False(collider.tryGetSphereRadius(out radius));
    Assert.Equal(0f, radius);
  }

  [Fact]
  public void Collider_SphereSettersSendValuesAndReturnNativeResult()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.setSphereOffset", true);
    FakeNatives.Set("Collider.setSphereRadius", false);

    Assert.True(collider.setSphereOffset(1f, 2f, 3f));
    Assert.True(collider.setSphereOffset(new Vector3(4f, 5f, 6f)));
    Assert.False(collider.setSphereRadius(1.5f));

    Assert.Equal(new[]
    {
      "Collider.setSphereOffset(obj-1,1,2,3)",
      "Collider.setSphereOffset(obj-1,4,5,6)",
      "Collider.setSphereRadius(obj-1,1.5)"
    }, FakeNatives.Calls);
  }

  // ---- ModelRenderer ----

  [Fact]
  public void ModelRenderer_ReadsAssetUuidsAsUtf8AndNullAsEmpty()
  {
    var renderer = new ModelRenderer(Id);
    FakeNatives.SetString("ModelRenderer.getModelUUID", "model-é");
    FakeNatives.SetString("ModelRenderer.getTextureUUID", null);
    FakeNatives.Set("ModelRenderer.getShouldRender", true);

    Assert.Equal("model-é", renderer.getModelUUID());
    Assert.Equal("", renderer.getTextureUUID());
    Assert.True(renderer.getShouldRender());

    FakeNatives.SetString("ModelRenderer.getTextureUUID", "tex-1");
    FakeNatives.Set("ModelRenderer.getShouldRender", false);
    Assert.Equal("tex-1", renderer.getTextureUUID());
    Assert.False(renderer.getShouldRender());
  }

  [Fact]
  public void ModelRenderer_SetModelAndTexturePassAssetUuidAndReturnNativeResult()
  {
    var renderer = new ModelRenderer(Id);
    FakeNatives.Set("ModelRenderer.setModelUUID", true);
    FakeNatives.Set("ModelRenderer.setTextureUUID", false);

    Assert.True(renderer.setModel("model-é"));
    Assert.False(renderer.setTexture("tex-1"));
    renderer.setShouldRender(true);
    renderer.setShouldRender(false);

    Assert.Equal(new[]
    {
      "ModelRenderer.setModelUUID(obj-1,model-é)",
      "ModelRenderer.setTextureUUID(obj-1,tex-1)",
      "ModelRenderer.setShouldRender(obj-1,1)",
      "ModelRenderer.setShouldRender(obj-1,0)"
    }, FakeNatives.Calls);
  }

  // ---- LightRenderer ----

  [Fact]
  public void LightRenderer_ReadsEveryField()
  {
    var light = new LightRenderer(Id);
    FakeNatives.Set("LightRenderer.getIsSpotLight", true);
    FakeNatives.Set("LightRenderer.getColor", new Vector3(0.25f, 0.5f, 0.75f));
    FakeNatives.Set("LightRenderer.getAmbient", 0.125f);
    FakeNatives.Set("LightRenderer.getDiffuse", 0.5f);
    FakeNatives.Set("LightRenderer.getSpecular", 2f);
    FakeNatives.Set("LightRenderer.getDirection", new Vector3(1f, -2f, 3f));
    FakeNatives.Set("LightRenderer.getConeAngle", 30f);

    Assert.True(light.getIsSpotLight());
    Assert.Equal(new Vector3(0.25f, 0.5f, 0.75f), light.getColor());
    Assert.Equal(0.125f, light.getAmbient());
    Assert.Equal(0.5f, light.getDiffuse());
    Assert.Equal(2f, light.getSpecular());
    Assert.Equal(new Vector3(1f, -2f, 3f), light.getDirection());
    Assert.Equal(30f, light.getConeAngle());

    FakeNatives.Set("LightRenderer.getIsSpotLight", false);
    Assert.False(light.getIsSpotLight());
  }

  [Fact]
  public void LightRenderer_WritesRoute()
  {
    var light = new LightRenderer(Id);

    light.setSpotLight(true);
    light.setSpotLight(false);
    light.setColor(0.25f, 0.5f, 0.75f);
    light.setColor(new Vector3(1f, 0f, 0.5f));
    light.setAmbient(0.125f);
    light.setDiffuse(0.5f);
    light.setSpecular(2f);
    light.setDirection(1f, 2f, 3f);
    light.setDirection(new Vector3(4f, 5f, 6f));
    light.setConeAngle(45f);

    Assert.Equal(new[]
    {
      "LightRenderer.setSpotLight(obj-1,1)",
      "LightRenderer.setSpotLight(obj-1,0)",
      "LightRenderer.setColor(obj-1,0.25,0.5,0.75)",
      "LightRenderer.setColor(obj-1,1,0,0.5)",
      "LightRenderer.setAmbient(obj-1,0.125)",
      "LightRenderer.setDiffuse(obj-1,0.5)",
      "LightRenderer.setSpecular(obj-1,2)",
      "LightRenderer.setDirection(obj-1,1,2,3)",
      "LightRenderer.setDirection(obj-1,4,5,6)",
      "LightRenderer.setConeAngle(obj-1,45)"
    }, FakeNatives.Calls);
  }

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
    unsafe
    {
      Assert.True(NativeBindings.Transform.has != null);
    }

    _natives.Dispose();

    unsafe
    {
      Assert.True(NativeBindings.Transform.has == null);
    }
  }
}
