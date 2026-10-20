using System;
using System.Numerics;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// LookAtTarget and TargetRef against FakeNatives.
[Collection("BridgeInstances")]
public class StarterLookAtTargetTests : StarterMotionTargetsFixture
{
  [Fact]
  public void LookAtTarget_SnapsToFaceTheTargetWhenTurnSpeedIsZero()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 0f));
    AssertVec(new Vector3(0f, 0f, -1f), Facing("me"));

    Tick(script, 0.1f);

    AssertVec(new Vector3(1f, 0f, 0f), Facing("me"));
  }

  [Fact]
  public void LookAtTarget_TurnsNoFasterThanTurnSpeedAndArrivesOnTheTarget()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 90f));

    Tick(script, 0.1f);
    Assert.Equal(-9f, Rotations["me"].Y, 0.05f);
    Assert.Equal(0f, Rotations["me"].X, 0.05f);
    Assert.Equal(0f, Rotations["me"].Z, 0.05f);

    Tick(script, 0.1f);
    Assert.Equal(-18f, Rotations["me"].Y, 0.05f);

    Ticks(script, 12, 0.1f);
    AssertVec(new Vector3(1f, 0f, 0f), Facing("me"));
    Assert.Equal(-90f, Rotations["me"].Y, 0.01f);
  }

  [Fact]
  public void LookAtTarget_TakesTheShortWayRoundWhenTheTargetIsBehind()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(-1f, 0f, 1f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 90f));
    Rotations["me"] = new Vector3(0f, 170f, 0f);

    Tick(script, 0.1f);

    // The goal yaw is 135 degrees, 35 away, so one step turns the 9 degrees toward it, never the long way. Compared
    // as a facing because an Euler yaw past 90 degrees reads back as an equivalent pitch/roll flip.
    AssertVec(StarterUtils.rotate(new Vector3(0f, 161f, 0f), new Vector3(0f, 0f, -1f)), Facing("me"));
  }

  [Fact]
  public void LookAtTarget_PlayerSlotWinsOverTheNamedTarget()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 0f, 0f));
    AddPlayers(("p1", 1, new Vector3(-10f, 0f, 0f)));
    var bySlot = Make<LookAtTarget>(("m_target", "Target"), ("m_targetPlayerSlot", 1), ("m_turnSpeed", 0f));

    Tick(bySlot, 0.1f);
    AssertVec(new Vector3(-1f, 0f, 0f), Facing("me"));

    Rotations["me"] = Vector3.Zero;
    var byName = Make<LookAtTarget>(("m_target", "Target"), ("m_targetPlayerSlot", -1), ("m_turnSpeed", 0f));
    Tick(byName, 0.1f);
    AssertVec(new Vector3(1f, 0f, 0f), Facing("me"));
  }

  [Fact]
  public void TargetRef_MissingPlayerSlotScansOncePerSecondAndWarnsOnce()
  {
    AddPlayers(("p0", 0, Vector3.Zero));
    var target = new TargetRef("Test script") { Slot = 3 };

    for (var tick = 0; tick < 8; tick++)
    {
      Assert.False(target.tryResolve(0.25f, out _));
    }

    Assert.Equal(2, PlayerScans());
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("slot 3", warnings[0]);

    AddPlayers(("p0", 0, Vector3.Zero), ("p3", 3, Vector3.One));
    var found = false;
    var uuid = "";
    for (var tick = 0; tick < 4 && !found; tick++)
    {
      found = target.tryResolve(0.25f, out uuid);
    }

    Assert.True(found);
    Assert.Equal("p3", uuid);
  }

  [Fact]
  public void LookAtTarget_KeepsItsYawWhenTheTargetIsStraightAbove()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(0.0005f, 5f, 0f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 0f), ("m_yawOnly", false));
    Rotations["me"] = new Vector3(0f, 40f, 0f);

    Tick(script, 0.1f);

    Assert.Equal(40f, Rotations["me"].Y, 1e-3f);
    Assert.Equal(90f, Rotations["me"].X, 1e-2f);
  }

  [Fact]
  public void LookAtTarget_YawOnlyKeepsPitchAndRollAtZero()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 5f, 0f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 0f));

    Tick(script, 0.1f);

    AssertVec(new Vector3(1f, 0f, 0f), Facing("me"));
    Assert.Equal(0f, Rotations["me"].X, 1e-3f);
    Assert.Equal(0f, Rotations["me"].Z, 1e-3f);
  }

  [Fact]
  public void LookAtTarget_WithoutYawOnlyPitchesTowardTheTarget()
  {
    AddObject("me", Vector3.Zero);
    AddNamed("Target", "t1", new Vector3(10f, 5f, 0f));
    var script = Make<LookAtTarget>(("m_target", "Target"), ("m_turnSpeed", 0f), ("m_yawOnly", false));

    Tick(script, 0.1f);

    AssertVec(Vector3.Normalize(new Vector3(10f, 5f, 0f)), Facing("me"));
  }

  [Fact]
  public void LookAtTarget_MissingTargetNeverThrowsAndLeavesTheRotation()
  {
    AddObject("me", Vector3.Zero);
    var script = Make<LookAtTarget>(("m_target", "Ghost"), ("m_turnSpeed", 0f));

    Ticks(script, 6, 0.25f);

    Assert.Equal(Vector3.Zero, Rotations["me"]);
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("Ghost", warnings[0]);
  }

  [Fact]
  public void LookAtTarget_OnARigidBodyWarnsOnceAndOtherwiseDoesNot()
  {
    AddObject("me", Vector3.Zero);
    var free = Make<LookAtTarget>();
    free.start();
    Assert.Empty(Warnings());

    AddBody("me");
    var withBody = Make<LookAtTarget>();
    withBody.start();

    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("RigidBody", warnings[0]);
  }
}
