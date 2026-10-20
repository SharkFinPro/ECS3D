using System;
using System.Numerics;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Follower against FakeNatives.
[Collection("BridgeInstances")]
public class StarterFollowerTests : StarterMotionTargetsFixture
{
  [Fact]
  public void Follower_WithoutARigidBodyStepsAndStopsAtTheStopDistance()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    var script = Make<Follower>(("m_target", "Target"));

    Ticks(script, 8, 0.25f);

    Assert.Equal(8, Moves.Count);
    Assert.All(Moves, move => AssertVec(new Vector3(1f, 0f, 0f), move.Delta));
    Assert.Equal(8f, Positions["me"].X, 1e-3f);

    Ticks(script, 4, 0.25f);
    Assert.Equal(8, Moves.Count);
    Assert.Equal(8f, Positions["me"].X, 1e-3f);
  }

  [Fact]
  public void Follower_WithoutARigidBodyNeverOvershootsTheStopDistance()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(3f, 0f, 0f));
    var script = Make<Follower>(("m_target", "Target"));

    Tick(script, 1.0f);

    Assert.Single(Moves);
    AssertVec(new Vector3(1f, 0f, 0f), Moves[0].Delta);
  }

  [Fact]
  public void Follower_IgnoreHeightKeepsItOnTheGroundAndOffClosesTheGapInThreeD()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 5f, 0f));
    var flat = Make<Follower>(("m_target", "Target"));
    Ticks(flat, 30, 0.25f);
    Assert.Equal(0f, Positions["me"].Y, 1e-4f);
    Assert.Equal(8f, Positions["me"].X, 1e-3f);

    Positions["me"] = Vector3.Zero;
    var flying = Make<Follower>(("m_target", "Target"), ("m_ignoreHeight", false));
    Ticks(flying, 40, 0.25f);
    Assert.True(Positions["me"].Y > 0.5f);
    Assert.Equal(2f, Vector3.Distance(Positions["me"], new Vector3(10f, 5f, 0f)), 1e-3f);
  }

  [Fact]
  public void Follower_WithARigidBodySteersItsVelocityInsteadOfMoving()
  {
    AddObject("me", Vector3.Zero);
    AddBody("me");
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    var script = Make<Follower>(("m_target", "Target"));

    Tick(script, 0.1f);

    Assert.Empty(Moves);
    var force = Assert.Single(Forces);
    Assert.Equal("me", force.Uuid);
    Assert.Equal((int)ForceMode.VelocityChange, force.Mode);
    AssertVec(new Vector3(0.04f, 0f, 0f), force.Force, 1e-4f);
  }

  [Fact]
  public void Follower_WithARigidBodyBrakesInsideTheStopDistanceAndLeavesGravityAlone()
  {
    AddObject("me", Vector3.Zero);
    AddBody("me");
    AddNamed("Target", "t1", new Vector3(1f, 0f, 0f));
    var script = Make<Follower>(("m_target", "Target"));

    Tick(script, 0.1f);
    Assert.Empty(Forces);

    Velocities["me"] = new Vector3(0.4f, -0.3f, 0f);
    Tick(script, 0.1f);
    var force = Assert.Single(Forces);
    AssertVec(new Vector3(-0.04f, 0f, 0f), force.Force, 1e-4f);
  }

  [Fact]
  public void Follower_PlayerSlotWinsOverTheNamedTarget()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    AddPlayers(("p1", 1, new Vector3(0f, 0f, 10f)));
    var script = Make<Follower>(("m_target", "Target"), ("m_targetPlayerSlot", 1));

    Tick(script, 0.25f);

    var move = Assert.Single(Moves);
    AssertVec(new Vector3(0f, 0f, 1f), move.Delta);
  }

  [Fact]
  public void Follower_MissingTargetNeverThrowsAndStaysPut()
  {
    AddObject("me", Vector3.Zero);
    var script = Make<Follower>(("m_target", "Ghost"));

    Ticks(script, 6, 0.25f);

    Assert.Empty(Moves);
    Assert.Empty(Forces);
    Assert.Single(Warnings());
  }

  [Fact]
  public void Follower_WithARigidBodyBrakesBeforeTheStopDistanceInsteadOfCoastingPast()
  {
    AddObject("me", Vector3.Zero);
    AddBody("me", new Vector3(0.4f, 0f, 0f));
    AddNamed("Target", "t1", new Vector3(3f, 0f, 0f));
    var script = Make<Follower>(("m_target", "Target"));

    // One unit short of the stop point at 0.4 per tick, which carries on for about four units: brake.
    Tick(script, 0.1f);
    AssertVec(new Vector3(-0.04f, 0f, 0f), Assert.Single(Forces).Force, 1e-4f);

    // Control: the same distance at rest is still a push toward the target.
    Forces.Clear();
    Velocities["me"] = Vector3.Zero;
    Tick(script, 0.1f);
    AssertVec(new Vector3(0.04f, 0f, 0f), Assert.Single(Forces).Force, 1e-4f);
  }
}
