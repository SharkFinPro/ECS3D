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

// PlayerSpawnManager against a WorldModel and FakeNatives.
[Collection("BridgeInstances")]
public class PlayerSpawnManagerTests : StarterWorldFixture
{
  private void AddSpawnPoints()
  {
    _world.Add("crate", "Crate", new Vector3(99f, 0f, 99f));
    _world.Add("sp3", "SpawnPoint3", new Vector3(30f, 0f, 3f));
    _world.Add("sp1", "SpawnPoint1", new Vector3(10f, 0f, 1f));
    _world.Add("sp4", "SpawnPoint4", new Vector3(40f, 0f, 4f));
    _world.Add("sp2", "SpawnPoint2", new Vector3(20f, 0f, 2f));
  }

  private static PlayerSpawnManager NewManager() => new();

  [Fact]
  public void Spawns_AtPointsRoundRobinSortedByName_OfThePlayerPrefab()
  {
    AddSpawnPoints();
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);
    manager.onPlayerJoined(2);

    Assert.Equal(3, _world.Spawns.Count);
    Assert.All(_world.Spawns, s => Assert.Equal(PlayerPrefabUuid, s.Prefab));
    AssertVec(new Vector3(10f, 1f, 1f), _world.Spawns[0].At);
    AssertVec(new Vector3(20f, 1f, 2f), _world.Spawns[1].At);
    AssertVec(new Vector3(30f, 1f, 3f), _world.Spawns[2].At);
    Assert.Empty(_world.Warnings);
  }

  [Fact]
  public void Spawns_SetTheSlotOnTheNewBodyDuringTheJoin()
  {
    AddSpawnPoints();
    var manager = NewManager();

    manager.onPlayerJoined(0);
    Assert.True(Called("PlayerController.setPlayerSlot(spawned-1,0)"));
    Assert.False(Called("PlayerController.setPlayerSlot(spawned-2,1)"));

    manager.onPlayerJoined(1);
    Assert.True(Called("PlayerController.setPlayerSlot(spawned-2,1)"));
  }

  [Fact]
  public void SpawnedBodies_AreNamedAfterTheirSlot()
  {
    AddSpawnPoints();
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.onPlayerJoined(3);

    Assert.Equal(new[] { ("spawned-1", "Player 0"), ("spawned-2", "Player 3") }, _world.Renames);
    Assert.Equal("Player 3", World.getObjectName("spawned-2"));
  }

  [Fact]
  public void AdoptedBody_KeepsItsName_WhileASpawnedOneInTheSameSceneIsRenamed()
  {
    AddSpawnPoints();
    _world.Add("placed", "Hero", new Vector3(10f, 1f, 1f));
    FakeNatives.Set("PlayerController.has:placed", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:placed", 0);
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);

    Assert.Equal(new[] { ("spawned-1", "Player 1") }, _world.Renames);
    Assert.Equal("Hero", World.getObjectName("placed"));
  }

  [Fact]
  public void FifthPlayer_SharesAPointThreeUnitsHigher()
  {
    AddSpawnPoints();
    var manager = NewManager();

    for (var slot = 0; slot < 5; slot++)
    {
      manager.onPlayerJoined(slot);
    }

    AssertVec(new Vector3(10f, 1f, 1f), _world.Spawns[0].At);
    AssertVec(new Vector3(40f, 1f, 4f), _world.Spawns[3].At);
    AssertVec(new Vector3(10f, 4f, 1f), _world.Spawns[4].At);
  }

  [Fact]
  public void SpawnHeight_IsAddedToThePointAndTheSpawnPrefixIsConfigurable()
  {
    _world.Add("home", "Start A", new Vector3(5f, 2f, 5f));
    _world.Add("decoy", "SpawnPoint1", new Vector3(50f, 0f, 50f));
    var manager = NewManager();
    SetField(manager, "m_spawnPointPrefix", "Start");
    SetField(manager, "m_spawnHeight", 2.5f);

    manager.onPlayerJoined(0);

    AssertVec(new Vector3(5f, 4.5f, 5f), _world.Spawns[0].At);
  }

  [Fact]
  public void AdoptsAPlacedBody_NeverSpawnsOrDestroysIt_AndTakesUpAPoint()
  {
    AddSpawnPoints();
    _world.Add("placed", "Player 0", new Vector3(10f, 1f, 1f));
    FakeNatives.Set("PlayerController.has:placed", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:placed", 0);
    var manager = NewManager();

    manager.onPlayerJoined(0);
    Assert.Empty(_world.Spawns);

    manager.onPlayerJoined(1);
    Assert.Single(_world.Spawns);
    AssertVec(new Vector3(20f, 1f, 2f), _world.Spawns[0].At);

    manager.onPlayerLeft(0);
    Assert.Empty(_world.Destroyed);

    manager.onPlayerLeft(1);
    Assert.Equal(new[] { "spawned-1" }, _world.Destroyed);
  }

  [Fact]
  public void Leaving_DestroysTheSpawnedBodyOnce_AndTheSlotCanJoinAgain()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);

    manager.onPlayerLeft(1);
    Assert.Equal(new[] { "spawned-2" }, _world.Destroyed);

    manager.onPlayerLeft(1);
    Assert.Single(_world.Destroyed);

    manager.onPlayerJoined(1);
    Assert.Equal(3, _world.Spawns.Count);
  }

  [Fact]
  public void ReconnectingRepeatedly_AlwaysLandsOnTheGround()
  {
    AddSpawnPoints();
    var manager = NewManager();

    for (var round = 0; round < 8; round++)
    {
      manager.onPlayerJoined(0);
      manager.onPlayerLeft(0);
    }

    Assert.Equal(8, _world.Spawns.Count);
    Assert.All(_world.Spawns, s => AssertVec(new Vector3(10f, 1f, 1f), s.At));
  }

  [Fact]
  public void FivePlayersAtOnce_OnlyTheFifthIsLifted_AndALeaverFreesItsPoint()
  {
    AddSpawnPoints();
    var manager = NewManager();

    for (var slot = 0; slot < 5; slot++)
    {
      manager.onPlayerJoined(slot);
    }

    Assert.Equal(new[] { 1f, 1f, 1f, 1f, 4f }, _world.Spawns.Select(s => s.At.Y).ToArray());

    manager.onPlayerLeft(1);
    manager.onPlayerJoined(5);

    AssertVec(new Vector3(20f, 1f, 2f), _world.Spawns[5].At);
  }

  [Fact]
  public void ASlotSwitch_LeavesThenJoins_DestroyingAndRespawningTheBody()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);

    manager.onPlayerLeft(0);
    manager.onPlayerJoined(1);

    Assert.Equal(new[] { "spawned-1" }, _world.Destroyed);
    Assert.Equal(2, _world.Spawns.Count);
    AssertVec(new Vector3(10f, 1f, 1f), _world.Spawns[1].At);
    Assert.True(Called("PlayerController.setPlayerSlot(spawned-2,1)"));
  }

  [Fact]
  public void RejoiningWhileTheBodyLives_DoesNotSpawnASecondOne()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);

    manager.onPlayerJoined(0);

    Assert.Single(_world.Spawns);
  }

  [Fact]
  public void FallingBelowTheLimit_MovesTheBodyHomeWithZeroVelocity()
  {
    AddSpawnPoints();
    FakeNatives.Set("RigidBody.has", true);
    var manager = NewManager();
    manager.onPlayerJoined(0);

    _world.Place("spawned-1", new Vector3(3f, -19f, 3f));
    manager.fixedUpdate(0.02f);
    Assert.Empty(_world.Moves);
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("RigidBody.setVelocity(", StringComparison.Ordinal)));

    _world.Place("spawned-1", new Vector3(3f, -25f, 3f));
    manager.fixedUpdate(0.02f);

    Assert.Single(_world.Moves);
    Assert.Equal("spawned-1", _world.Moves[0].Uuid);
    AssertVec(new Vector3(10f, 1f, 1f), _world.Moves[0].To);
    Assert.True(Called("RigidBody.setVelocity(spawned-1,0,0,0)"));
    Assert.True(Called("RigidBody.setAngularVelocity(spawned-1,0,0,0)"));
  }

  [Fact]
  public void FallingAdoptedBody_ReturnsToWhereItWasPlaced_AndABodyWithoutPhysicsIsStillMoved()
  {
    _world.Add("placed", "Player 0", new Vector3(5f, 2f, 5f));
    FakeNatives.Set("PlayerController.has:placed", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:placed", 0);
    var manager = NewManager();
    manager.onPlayerJoined(0);

    _world.Place("placed", new Vector3(0f, -50f, 0f));
    manager.fixedUpdate(0.02f);

    AssertVec(new Vector3(5f, 2f, 5f), _world.PositionOf("placed"));
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("RigidBody.setVelocity(", StringComparison.Ordinal)));
  }

  [Fact]
  public void RespawnBelowY_IsConfigurable()
  {
    AddSpawnPoints();
    var manager = NewManager();
    SetField(manager, "m_respawnBelowY", 5.0f);
    manager.onPlayerJoined(0);

    _world.Place("spawned-1", new Vector3(3f, 6f, 3f));
    manager.fixedUpdate(0.02f);
    Assert.Empty(_world.Moves);

    _world.Place("spawned-1", new Vector3(3f, 4f, 3f));
    manager.fixedUpdate(0.02f);
    Assert.Single(_world.Moves);
  }

  [Fact]
  public void DestroyedBody_IsRespawnedAfterTwoSeconds_AtItsSpawnPoint()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);
    _world.Remove("spawned-2");

    manager.fixedUpdate(0.02f);
    manager.tickTimers(1.9f);
    manager.fixedUpdate(0.02f);
    Assert.Equal(2, _world.Spawns.Count);

    manager.tickTimers(0.2f);

    Assert.Equal(3, _world.Spawns.Count);
    AssertVec(new Vector3(20f, 1f, 2f), _world.Spawns[2].At);
    Assert.True(Called("PlayerController.setPlayerSlot(spawned-3,1)"));

    manager.fixedUpdate(0.02f);
    manager.tickTimers(5f);
    Assert.Equal(3, _world.Spawns.Count);
  }

  [Fact]
  public void DestroyedBody_IsNotRespawnedWhenThePlayerLeavesFirst()
  {
    AddSpawnPoints();
    var manager = NewManager();
    manager.onPlayerJoined(0);
    _world.Remove("spawned-1");
    manager.fixedUpdate(0.02f);

    manager.onPlayerLeft(0);
    manager.tickTimers(3f);

    Assert.Single(_world.Spawns);
  }

  [Fact]
  public void TintPlayers_SetsTheSlotColorTexture_CyclingFromSlotFour()
  {
    AddSpawnPoints();
    FakeNatives.Set("ModelRenderer.has", true);
    var manager = NewManager();
    SetField(manager, "m_tintPlayers", true);

    for (var slot = 0; slot < 5; slot++)
    {
      manager.onPlayerJoined(slot);
    }

    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-1,{PaletteRed})"));
    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-2,{PaletteBlue})"));
    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-3,{PaletteGreen})"));
    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-4,{PaletteOrange})"));
    Assert.True(Called($"ModelRenderer.setTextureUUID(spawned-5,{PaletteRed})"));
  }

  [Fact]
  public void TintPlayersOff_LeavesTheTextureAlone()
  {
    AddSpawnPoints();
    FakeNatives.Set("ModelRenderer.has", true);
    var manager = NewManager();

    manager.onPlayerJoined(0);

    Assert.Single(_world.Spawns);
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("ModelRenderer.setTextureUUID(", StringComparison.Ordinal)));
  }

  [Fact]
  public void NoSpawnPoints_SpawnsAtTheOriginRaisedBySpawnHeight_AndWarnsOnce()
  {
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.onPlayerJoined(1);

    Assert.Equal(2, _world.Spawns.Count);
    AssertVec(new Vector3(0f, 1f, 0f), _world.Spawns[0].At);
    AssertVec(new Vector3(0f, 1f, 0f), _world.Spawns[1].At);
    var warnings = _world.Warnings;
    Assert.Single(warnings);
    Assert.Contains("SpawnPoint", warnings[0]);
  }

  [Fact]
  public void FailedSpawn_WarnsAndIsNotRetried()
  {
    AddSpawnPoints();
    _world.FailSpawns = true;
    var manager = NewManager();

    manager.onPlayerJoined(0);
    manager.fixedUpdate(0.02f);
    manager.tickTimers(5f);
    manager.fixedUpdate(0.02f);
    manager.tickTimers(5f);

    Assert.Single(_world.Spawns);
    Assert.Single(_world.Warnings);
  }

  private StarterWorldListener ListenForSpawns()
  {
    var listener = new StarterWorldListener();
    Bridge.AddInstance("listener-scene", nameof(StarterWorldListener), listener);
    listener.Listen();
    return listener;
  }

  [Fact]
  public void PublishesPlayerSpawned_ForASpawnedAndAnAdoptedBody()
  {
    AddSpawnPoints();
    _world.Add("placed", "Player 0", new Vector3(10f, 1f, 1f));
    FakeNatives.Set("PlayerController.has:placed", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:placed", 0);
    var listener = ListenForSpawns();
    var manager = NewManager();
    Assert.Empty(listener.Spawned);

    manager.onPlayerJoined(2);
    manager.onPlayerJoined(0);

    Assert.Equal(2, listener.Spawned.Count);
    Assert.Equal(2, listener.Spawned[0].Slot);
    Assert.Equal("spawned-1", listener.Spawned[0].Uuid);
    Assert.Equal(0, listener.Spawned[1].Slot);
    Assert.Equal("placed", listener.Spawned[1].Uuid);
  }
}
