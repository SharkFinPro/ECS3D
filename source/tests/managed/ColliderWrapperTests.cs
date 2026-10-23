using System;
using System.Numerics;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Each component wrapper routes a call to the matching native function with its own uuid, in order, and
// reads vectors, scalars and bools back from what native answered. Values are chosen distinct per axis so a
// swapped or dropped component shows up.
[Collection("BridgeInstances")]
public class ColliderWrapperTests : IDisposable
{
  private const string Id = "obj-1";

  private readonly IDisposable _natives = FakeNatives.Install();

  public void Dispose()
  {
    var error = FakeNatives.LastError;
    _natives.Dispose();
    Assert.Null(error);
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
  public void Collider_ConvexMeshShapeIsNamed()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.getShape", 4);

    Assert.Equal(ColliderShape.ConvexMesh, collider.getShape());
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

  [Fact]
  public void Collider_CapsuleAccessorsReturnValuesOnlyWhenNativeSucceeds()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.getCapsuleOffset", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("Collider.getCapsuleRadius", 0.75f);
    FakeNatives.Set("Collider.getCapsuleHeight", 3.5f);

    FakeNatives.Set("Collider.getCapsuleOffset.ok", true);
    FakeNatives.Set("Collider.getCapsuleRadius.ok", true);
    FakeNatives.Set("Collider.getCapsuleHeight.ok", true);
    Assert.True(collider.tryGetCapsuleOffset(out var offset));
    Assert.Equal(new Vector3(1f, 2f, 3f), offset);
    Assert.True(collider.tryGetCapsuleRadius(out var radius));
    Assert.Equal(0.75f, radius);
    Assert.True(collider.tryGetCapsuleHeight(out var height));
    Assert.Equal(3.5f, height);

    FakeNatives.Set("Collider.getCapsuleOffset.ok", false);
    FakeNatives.Set("Collider.getCapsuleRadius.ok", false);
    FakeNatives.Set("Collider.getCapsuleHeight.ok", false);
    Assert.False(collider.tryGetCapsuleOffset(out offset));
    Assert.Equal(Vector3.Zero, offset);
    Assert.False(collider.tryGetCapsuleRadius(out radius));
    Assert.Equal(0f, radius);
    Assert.False(collider.tryGetCapsuleHeight(out height));
    Assert.Equal(0f, height);
  }

  [Fact]
  public void Collider_CapsuleSettersSendValuesAndReturnNativeResult()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.setCapsuleOffset", true);
    FakeNatives.Set("Collider.setCapsuleRadius", false);
    FakeNatives.Set("Collider.setCapsuleHeight", true);

    Assert.True(collider.setCapsuleOffset(1f, 2f, 3f));
    Assert.True(collider.setCapsuleOffset(new Vector3(4f, 5f, 6f)));
    Assert.False(collider.setCapsuleRadius(1.5f));
    Assert.True(collider.setCapsuleHeight(4f));

    Assert.Equal(new[]
    {
      "Collider.setCapsuleOffset(obj-1,1,2,3)",
      "Collider.setCapsuleOffset(obj-1,4,5,6)",
      "Collider.setCapsuleRadius(obj-1,1.5)",
      "Collider.setCapsuleHeight(obj-1,4)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void Collider_ConvexMeshAccessorsReturnValuesOnlyWhenNativeSucceeds()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.getConvexMeshOffset", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("Collider.getConvexMeshVertexCount", 12u);

    FakeNatives.Set("Collider.getConvexMeshOffset.ok", true);
    FakeNatives.Set("Collider.getConvexMeshVertexCount.ok", true);
    Assert.True(collider.tryGetConvexMeshOffset(out var offset));
    Assert.Equal(new Vector3(1f, 2f, 3f), offset);
    Assert.True(collider.tryGetConvexMeshVertexCount(out var count));
    Assert.Equal(12u, count);

    FakeNatives.Set("Collider.getConvexMeshOffset.ok", false);
    FakeNatives.Set("Collider.getConvexMeshVertexCount.ok", false);
    Assert.False(collider.tryGetConvexMeshOffset(out offset));
    Assert.Equal(Vector3.Zero, offset);
    Assert.False(collider.tryGetConvexMeshVertexCount(out count));
    Assert.Equal(0u, count);
  }

  [Fact]
  public void Collider_ConvexMeshSettersSendComponentsAndReturnNativeResult()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.setConvexMeshOffset", true);

    Assert.True(collider.setConvexMeshOffset(1f, 2f, 3f));
    Assert.True(collider.setConvexMeshOffset(new Vector3(4f, 5f, 6f)));

    Assert.Equal(new[]
    {
      "Collider.setConvexMeshOffset(obj-1,1,2,3)",
      "Collider.setConvexMeshOffset(obj-1,4,5,6)"
    }, FakeNatives.Calls);

    FakeNatives.Set("Collider.setConvexMeshOffset", false);
    Assert.False(collider.setConvexMeshOffset(7f, 8f, 9f));
  }

  [Fact]
  public void Collider_ShapeReportsCapsule()
  {
    var collider = new Collider(Id);
    FakeNatives.Set("Collider.getShape", (int)ColliderShape.Capsule);

    Assert.Equal(ColliderShape.Capsule, collider.getShape());
    Assert.Equal(3, (int)ColliderShape.Capsule);
  }
}
