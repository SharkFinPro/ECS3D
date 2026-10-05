using System;
using System.Globalization;
using System.Linq;
using System.Numerics;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// World's native-backed calls, against FakeNatives. NativeBindings is static, so this shares the
// BridgeInstances collection and runs serially.
[Collection("BridgeInstances")]
public class WorldWrapperTests : IDisposable
{
  private readonly IDisposable _natives = FakeNatives.Install();

  public void Dispose() => _natives.Dispose();

  [Fact]
  public void FindObjectByName_PassesNameAndReturnsNativeString()
  {
    FakeNatives.SetString("World.findObjectByName", "uuid-7");

    Assert.Equal("uuid-7", World.findObjectByName("café"));
    Assert.Equal(new[] { "World.findObjectByName(café)" }, FakeNatives.Calls);
  }

  [Fact]
  public void FindObjectByName_NullFromNativeIsEmptyString()
  {
    FakeNatives.SetString("World.findObjectByName", "found");
    Assert.Equal("found", World.findObjectByName("nobody"));

    FakeNatives.SetString("World.findObjectByName", null);
    Assert.Equal("", World.findObjectByName("nobody"));
  }

  [Fact]
  public void GetObjectName_PassesUuidAndReturnsName()
  {
    FakeNatives.SetString("World.getObjectName", "Player");

    Assert.Equal("Player", World.getObjectName("u-1"));
    Assert.Equal(new[] { "World.getObjectName(u-1)" }, FakeNatives.Calls);
  }

  [Fact]
  public void ObjectExists_ReflectsNativeAnswer()
  {
    FakeNatives.Set("World.objectExists", true);
    Assert.True(World.objectExists("u-1"));

    FakeNatives.Set("World.objectExists", false);
    Assert.False(World.objectExists("u-1"));
  }

  [Fact]
  public void GetAllObjects_SplitsCommaList()
  {
    FakeNatives.SetString("World.getAllObjectUuids", "a,b,c");

    Assert.Equal(new[] { "a", "b", "c" }, World.getAllObjects());
  }

  [Fact]
  public void GetAllObjects_EmptyOrNullIsEmptyArray()
  {
    FakeNatives.SetString("World.getAllObjectUuids", "solo");
    Assert.Equal(new[] { "solo" }, World.getAllObjects());

    FakeNatives.SetString("World.getAllObjectUuids", "");
    Assert.Empty(World.getAllObjects());

    FakeNatives.SetString("World.getAllObjectUuids", null);
    Assert.Empty(World.getAllObjects());
  }

  [Fact]
  public void TryGetTransform_FalseWithoutComponentAndTrueWithIt()
  {
    FakeNatives.Set("Transform.has", false);
    Assert.False(World.tryGetTransform("u-1", out var missing));
    Assert.Null(missing);

    FakeNatives.Set("Transform.has", true);
    Assert.True(World.tryGetTransform("u-1", out var found));
    Assert.NotNull(found);

    found.move(1f, 2f, 3f);
    Assert.Equal("Transform.move(u-1,1,2,3)", FakeNatives.Calls[^1]);
  }

  [Fact]
  public void TryGetRigidBody_FalseWithoutComponentAndTrueWithIt()
  {
    FakeNatives.Set("RigidBody.has", false);
    Assert.False(World.tryGetRigidBody("u-2", out var missing));
    Assert.Null(missing);

    FakeNatives.Set("RigidBody.has", true);
    Assert.True(World.tryGetRigidBody("u-2", out var found));

    found.setMass(4f);
    Assert.Equal("RigidBody.setMass(u-2,4)", FakeNatives.Calls[^1]);
  }

  [Fact]
  public void TryGetCollider_FalseWithoutComponentAndTrueWithIt()
  {
    FakeNatives.Set("Collider.has", false);
    Assert.False(World.tryGetCollider("u-3", out var missing));
    Assert.Null(missing);

    FakeNatives.Set("Collider.has", true);
    Assert.True(World.tryGetCollider("u-3", out var found));

    FakeNatives.Set("Collider.getShape", (int)ColliderShape.Sphere);
    Assert.Equal(ColliderShape.Sphere, found.getShape());
    Assert.Equal("Collider.getShape(u-3)", FakeNatives.Calls[^1]);
  }

  [Fact]
  public void TryGetModelRenderer_FalseWithoutComponentAndTrueWithIt()
  {
    FakeNatives.Set("ModelRenderer.has", false);
    Assert.False(World.tryGetModelRenderer("u-4", out var missing));
    Assert.Null(missing);

    FakeNatives.Set("ModelRenderer.has", true);
    Assert.True(World.tryGetModelRenderer("u-4", out var found));

    found.setShouldRender(true);
    Assert.Equal("ModelRenderer.setShouldRender(u-4,1)", FakeNatives.Calls[^1]);
  }

  [Fact]
  public void TryGetLightRenderer_FalseWithoutComponentAndTrueWithIt()
  {
    FakeNatives.Set("LightRenderer.has", false);
    Assert.False(World.tryGetLightRenderer("u-5", out var missing));
    Assert.Null(missing);

    FakeNatives.Set("LightRenderer.has", true);
    Assert.True(World.tryGetLightRenderer("u-5", out var found));

    found.setAmbient(0.5f);
    Assert.Equal("LightRenderer.setAmbient(u-5,0.5)", FakeNatives.Calls[^1]);
  }

  [Fact]
  public void TryGetPlayerController_FalseWithoutComponentAndTrueWithIt()
  {
    FakeNatives.Set("PlayerController.has", false);
    Assert.False(World.tryGetPlayerController("u-6", out var missing));
    Assert.Null(missing);

    FakeNatives.Set("PlayerController.has", true);
    Assert.True(World.tryGetPlayerController("u-6", out var found));

    found.setPlayerSlot(2);
    Assert.Equal("PlayerController.setPlayerSlot(u-6,2)", FakeNatives.Calls[^1]);
  }

  [Fact]
  public void Spawn_PassesNameAndPositionAndReturnsUuid()
  {
    FakeNatives.SetString("World.spawnObject", "new-uuid");

    Assert.Equal("new-uuid", World.spawn("Crate", 1f, 2f, 3f));
    Assert.Equal("new-uuid", World.spawn("Crate", new Vector3(4f, 5f, 6f)));
    Assert.Equal(new[] { "World.spawnObject(Crate,1,2,3)", "World.spawnObject(Crate,4,5,6)" }, FakeNatives.Calls);
  }

  [Fact]
  public void Spawn_NativeFailureIsEmptyString()
  {
    FakeNatives.SetString("World.spawnObject", "ok");
    Assert.Equal("ok", World.spawn("Crate", 0f, 0f, 0f));

    FakeNatives.SetString("World.spawnObject", null);
    Assert.Equal("", World.spawn("Crate", 0f, 0f, 0f));
  }

  [Fact]
  public void SpawnPrefab_PassesUuidAndPositionAndReturnsRoot()
  {
    FakeNatives.SetString("World.spawnPrefab", "root-uuid");

    Assert.Equal("root-uuid", World.spawnPrefab("prefab-1", 1f, 2f, 3f));
    Assert.Equal("root-uuid", World.spawnPrefab("prefab-1", new Vector3(7f, 8f, 9f)));
    Assert.Equal(new[] { "World.spawnPrefab(prefab-1,1,2,3)", "World.spawnPrefab(prefab-1,7,8,9)" },
                 FakeNatives.Calls);
  }

  [Fact]
  public void SpawnPrefab_UnknownPrefabIsEmptyString()
  {
    FakeNatives.SetString("World.spawnPrefab", "root-uuid");
    Assert.Equal("root-uuid", World.spawnPrefab("known", 0f, 0f, 0f));

    FakeNatives.SetString("World.spawnPrefab", "");
    Assert.Equal("", World.spawnPrefab("missing", 0f, 0f, 0f));
  }

  [Fact]
  public void Destroy_PassesUuid()
  {
    World.destroy("u-9");

    Assert.Equal(new[] { "World.destroyObject(u-9)" }, FakeNatives.Calls);
  }

  [Fact]
  public void Raycast_ParsesHitFromNativeString()
  {
    FakeNatives.SetString("World.raycast", "hit-uuid,2.5,1,2,3,0,1,0");

    var ok = World.raycast(new Vector3(1f, 2f, 3f), new Vector3(0f, 0f, -1f), 50f, out var hit, 0x6u, "me");

    Assert.True(ok);
    Assert.Equal("hit-uuid", hit.objectUuid);
    Assert.Equal(2.5f, hit.distance);
    Assert.Equal(new Vector3(1f, 2f, 3f), hit.point);
    Assert.Equal(new Vector3(0f, 1f, 0f), hit.normal);
    Assert.Equal(new[] { "World.raycast(1,2,3,0,0,-1,50,6,me)" }, FakeNatives.Calls);
  }

  [Fact]
  public void Raycast_DefaultsToAllLayersAndNoIgnore()
  {
    FakeNatives.SetString("World.raycast", "");

    World.raycast(Vector3.Zero, Vector3.UnitX, 1f, out _);

    Assert.Equal(new[] { "World.raycast(0,0,0,1,0,0,1,4294967295,)" }, FakeNatives.Calls);
  }

  [Fact]
  public void Raycast_ParsesFractionsAndNegativesIndependentOfCulture()
  {
    var previous = CultureInfo.CurrentCulture;
    CultureInfo.CurrentCulture = new CultureInfo("de-DE");
    try
    {
      FakeNatives.SetString("World.raycast", "h,0.25,-1.5,2.5,-3.5,0,0,-1");

      Assert.True(World.raycast(Vector3.Zero, Vector3.UnitZ, 9f, out var hit));
      Assert.Equal(0.25f, hit.distance);
      Assert.Equal(new Vector3(-1.5f, 2.5f, -3.5f), hit.point);
      Assert.Equal(new Vector3(0f, 0f, -1f), hit.normal);
    }
    finally
    {
      CultureInfo.CurrentCulture = previous;
    }
  }

  [Fact]
  public void Raycast_EmptyStringIsMissWithDefaultHit()
  {
    FakeNatives.SetString("World.raycast", "hit,1,0,0,0,0,1,0");
    Assert.True(World.raycast(Vector3.Zero, Vector3.UnitX, 1f, out _));

    FakeNatives.SetString("World.raycast", "");
    Assert.False(World.raycast(Vector3.Zero, Vector3.UnitX, 1f, out var hit));
    Assert.Null(hit.objectUuid);
    Assert.Equal(0f, hit.distance);
  }

  [Fact]
  public void Raycast_WrongFieldCountIsMiss()
  {
    FakeNatives.SetString("World.raycast", "hit,1,0,0,0,0,1,0");
    Assert.True(World.raycast(Vector3.Zero, Vector3.UnitX, 1f, out _));

    FakeNatives.SetString("World.raycast", "hit,1,0,0,0,0,1");
    Assert.False(World.raycast(Vector3.Zero, Vector3.UnitX, 1f, out var hit));
    Assert.Null(hit.objectUuid);
  }

  [Fact]
  public void OverlapSphere_ParsesListAndPassesArguments()
  {
    FakeNatives.SetString("World.overlapSphere", "a,b");

    var result = World.overlapSphere(new Vector3(1f, 2f, 3f), 4f, 0x3u, "me");

    Assert.Equal(new[] { "a", "b" }, result);
    Assert.Equal(new[] { "World.overlapSphere(1,2,3,4,3,me)" }, FakeNatives.Calls);
  }

  [Fact]
  public void OverlapSphere_EmptyIsEmptyArray()
  {
    FakeNatives.SetString("World.overlapSphere", "only");
    Assert.Single(World.overlapSphere(Vector3.Zero, 1f));

    FakeNatives.SetString("World.overlapSphere", "");
    Assert.Empty(World.overlapSphere(Vector3.Zero, 1f));
  }

  [Fact]
  public void ComponentOps_PassTypeNameAndUuidAndReturnNativeAnswer()
  {
    FakeNatives.Set("ComponentOps.hasComponent", true);
    FakeNatives.Set("ComponentOps.addComponent", true);
    FakeNatives.Set("ComponentOps.removeComponent", false);

    Assert.True(World.hasComponent("u-1", "RigidBody"));
    Assert.True(World.addComponent("u-1", "Box"));
    Assert.False(World.removeComponent("u-1", "Sphere"));

    Assert.Equal(new[]
    {
      "ComponentOps.hasComponent(u-1,RigidBody)",
      "ComponentOps.addComponent(u-1,Box)",
      "ComponentOps.removeComponent(u-1,Sphere)"
    }, FakeNatives.Calls);
  }

  [Fact]
  public void GetComponentTypes_SplitsListAndEmptyIsEmptyArray()
  {
    FakeNatives.SetString("ComponentOps.getComponentTypes", "Transform,RigidBody");
    Assert.Equal(new[] { "Transform", "RigidBody" }, World.getComponentTypes("u-1"));
    Assert.Equal("ComponentOps.getComponentTypes(u-1)", FakeNatives.Calls.Single());

    FakeNatives.SetString("ComponentOps.getComponentTypes", "");
    Assert.Empty(World.getComponentTypes("u-1"));
  }
}
