using System;
using System.Collections.Generic;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Bridge.DeliverSceneEvent: which SceneScript virtual each event code reaches, with what arguments, and
// what it leaves alone (object scripts, unknown codes, scripts that already faulted).
[Collection("BridgeInstances")]
public class SceneEventTests : IDisposable
{
  public class Director : SceneScript
  {
    public List<string> Seen { get; } = new();

    public override void onSceneLoaded() => Seen.Add("loaded");

    public override void onSceneUnloading() => Seen.Add("unloading");

    public override void onObjectSpawned(string uuid) => Seen.Add("spawned:" + uuid);

    public override void onObjectDestroyed(string uuid) => Seen.Add("destroyed:" + uuid);

    public override void onPlayerJoined(int slot) => Seen.Add("joined:" + slot);

    public override void onPlayerLeft(int slot) => Seen.Add("left:" + slot);
  }

  public class Fragile : SceneScript
  {
    public int Calls { get; private set; }

    public override void onSceneLoaded()
    {
      Calls++;
      throw new InvalidOperationException("boom");
    }

    public override void onPlayerJoined(int slot) => Calls++;
  }

  public class Bystander : ScriptBase
  {
    public int Calls { get; private set; }

    public override void start() => Calls++;
  }

  public class Quiet : SceneScript
  {
  }

  private const string SceneUuid = "scene-uuid";
  private const string OtherSceneUuid = "other-scene-uuid";
  private const string ObjectUuid = "object-uuid";

  public SceneEventTests()
  {
    Bridge.ResetForTests();
    Bridge.Events.clear();
  }

  public void Dispose()
  {
    Bridge.ResetForTests();
    Bridge.Events.clear();
  }

  [Fact]
  public void EachEventCodeCallsItsVirtualWithTheRightArgument()
  {
    var director = new Director();
    Bridge.AddInstance(SceneUuid, nameof(Director), director);

    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 0, "", -1);
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 1, "", -1);
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 2, "spawn-1", -1);
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 3, "gone-1", -1);
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 4, "", 3);
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 5, "", 7);

    Assert.Equal(
      new[] { "loaded", "unloading", "spawned:spawn-1", "destroyed:gone-1", "joined:3", "left:7" },
      director.Seen);
  }

  [Fact]
  public void AnUnknownEventCodeIsIgnoredAndLeavesTheScriptHealthy()
  {
    var director = new Director();
    Bridge.AddInstance(SceneUuid, nameof(Director), director);

    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 99, "", -1);
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), -1, "", -1);
    Assert.Empty(director.Seen);
    Assert.True(Bridge.IsHealthy(SceneUuid, nameof(Director)));

    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 0, "", -1);
    Assert.Equal(new[] { "loaded" }, director.Seen);
  }

  [Fact]
  public void AnObjectScriptUnderTheKeyIsAnUntouchedNoOp()
  {
    var bystander = new Bystander();
    Bridge.AddInstance(ObjectUuid, nameof(Bystander), bystander);
    var director = new Director();
    Bridge.AddInstance(SceneUuid, nameof(Director), director);

    var thrown = Record.Exception(() => Bridge.DeliverSceneEvent(ObjectUuid, nameof(Bystander), 0, "", -1));
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 0, "", -1);

    Assert.Null(thrown);
    Assert.True(Bridge.IsHealthy(ObjectUuid, nameof(Bystander)));
    Assert.Equal(new[] { "loaded" }, director.Seen);
  }

  [Fact]
  public void AMissingInstanceIsASilentNoOp()
  {
    var director = new Director();
    Bridge.AddInstance(SceneUuid, nameof(Director), director);

    var thrown = Record.Exception(() => Bridge.DeliverSceneEvent(OtherSceneUuid, nameof(Director), 0, "", -1));
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 0, "", -1);

    Assert.Null(thrown);
    Assert.Equal(new[] { "loaded" }, director.Seen);
  }

  [Fact]
  public void AScriptThatDoesNotOverrideAnEventIgnoresIt()
  {
    Bridge.AddInstance(SceneUuid, nameof(Quiet), new Quiet());
    var director = new Director();
    Bridge.AddInstance(SceneUuid, nameof(Director), director);

    var thrown = Record.Exception(() => Bridge.DeliverSceneEvent(SceneUuid, nameof(Quiet), 2, "x", -1));
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 2, "x", -1);

    Assert.Null(thrown);
    Assert.True(Bridge.IsHealthy(SceneUuid, nameof(Quiet)));
    Assert.Equal(new[] { "spawned:x" }, director.Seen);
  }

  [Fact]
  public void AThrowingHandlerFaultsOnlyThatSceneScript()
  {
    var fragile = new Fragile();
    Bridge.AddInstance(SceneUuid, nameof(Fragile), fragile);
    var director = new Director();
    Bridge.AddInstance(SceneUuid, nameof(Director), director);

    Bridge.DeliverSceneEvent(SceneUuid, nameof(Fragile), 0, "", -1);
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 0, "", -1);

    Assert.False(Bridge.IsHealthy(SceneUuid, nameof(Fragile)));
    Assert.True(Bridge.IsHealthy(SceneUuid, nameof(Director)));

    Bridge.DeliverSceneEvent(SceneUuid, nameof(Fragile), 4, "", 1);
    Bridge.DeliverSceneEvent(SceneUuid, nameof(Director), 4, "", 1);

    Assert.Equal(1, fragile.Calls);
    Assert.Equal(new[] { "loaded", "joined:1" }, director.Seen);
  }
}
