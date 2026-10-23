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
}
