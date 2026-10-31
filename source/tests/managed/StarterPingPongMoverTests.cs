using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Reflection;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// PingPongMover against FakeNatives.
[Collection("BridgeInstances")]
public class StarterPingPongMoverTests : StarterMotionTargetsFixture
{
  private static PingPongMover Platform(bool ease, bool carry = true, Vector3? offset = null)
  {
    AddObject("me", Vector3.Zero, new Vector3(2f, 0.5f, 2f));
    var script = Make<PingPongMover>(
      ("m_offset", offset ?? new Vector3(0f, 4f, 0f)), ("m_ease", ease), ("m_carryRiders", carry));
    script.start();
    return script;
  }

  [Fact]
  public void PingPongMover_MovesAtSpeedAlongTheOffsetWithoutEase()
  {
    var script = Platform(false);

    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(0f, 1f, 0f), Positions["me"]);
  }

  [Fact]
  public void PingPongMover_EaseAppliesSmoothstepToTheLegProgress()
  {
    var script = Platform(true);

    Ticks(script, 2, 0.25f);
    // A quarter of the way: smoothstep(0.25) = 0.15625 of the 4-unit offset.
    AssertVec(new Vector3(0f, 0.625f, 0f), Positions["me"]);

    Ticks(script, 2, 0.25f);
    AssertVec(new Vector3(0f, 2f, 0f), Positions["me"]);
  }

  [Fact]
  public void PingPongMover_PausesAtTheEndThenReturnsToTheStart()
  {
    var script = Platform(false);

    Ticks(script, 8, 0.25f);
    AssertVec(new Vector3(0f, 4f, 0f), Positions["me"]);

    // The hold lasts the full second: four ticks at the end after the one that arrives.
    Ticks(script, 4, 0.25f);
    AssertVec(new Vector3(0f, 4f, 0f), Positions["me"]);

    Tick(script, 0.25f);
    AssertVec(new Vector3(0f, 3.5f, 0f), Positions["me"]);

    Ticks(script, 7, 0.25f);
    AssertVec(Vector3.Zero, Positions["me"]);

    Ticks(script, 4, 0.25f);
    AssertVec(Vector3.Zero, Positions["me"]);

    Tick(script, 0.25f);
    AssertVec(new Vector3(0f, 0.5f, 0f), Positions["me"]);
  }

  [Fact]
  public void PingPongMover_StartsFromWhereItWasPlaced()
  {
    AddObject("me", new Vector3(5f, 1f, -2f), new Vector3(2f, 0.5f, 2f));
    var script = Make<PingPongMover>(("m_ease", false));
    script.start();

    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(5f, 2f, -2f), Positions["me"]);
  }

  [Fact]
  public void PingPongMover_OnARigidBodyWarnsOnceAndOtherwiseDoesNot()
  {
    Platform(false);
    Assert.Empty(Warnings());

    AddBody("me");
    var script = Make<PingPongMover>();
    script.start();

    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("RigidBody", warnings[0]);
  }

  private static PingPongMover SidewaysPlatform(bool carry)
  {
    var script = Platform(false, carry, new Vector3(4f, 0f, 0f));
    AddObject("above", new Vector3(0f, 1.5f, 0f));
    AddBody("above");
    AddObject("below", new Vector3(0f, 0.2f, 0f));
    AddBody("below");
    AddObject("bodyless", new Vector3(0f, 1.5f, 0f));
    script.onCollisionEnter("above");
    script.onCollisionEnter("below");
    script.onCollisionEnter("bodyless");
    return script;
  }

  [Fact]
  public void PingPongMover_CarriesAnAboveTopRiderByThePlatformDisplacement()
  {
    var script = SidewaysPlatform(true);

    Tick(script, 0.25f);
    AssertVec(new Vector3(0.5f, 0f, 0f), Positions["me"]);
    AssertVec(new Vector3(0.5f, 1.5f, 0f), Positions["above"]);

    Tick(script, 0.25f);
    AssertVec(new Vector3(1f, 1.5f, 0f), Positions["above"]);
  }

  [Fact]
  public void PingPongMover_IgnoresARiderBelowTheTopAndOneWithoutARigidBody()
  {
    var script = SidewaysPlatform(true);

    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(1f, 1.5f, 0f), Positions["above"]);
    AssertVec(new Vector3(0f, 0.2f, 0f), Positions["below"]);
    AssertVec(new Vector3(0f, 1.5f, 0f), Positions["bodyless"]);
  }

  [Fact]
  public void PingPongMover_StopsCarryingARiderAfterItLeaves()
  {
    var script = SidewaysPlatform(true);
    Tick(script, 0.25f);
    AssertVec(new Vector3(0.5f, 1.5f, 0f), Positions["above"]);

    script.onCollisionExit("above");
    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(1.5f, 0f, 0f), Positions["me"]);
    AssertVec(new Vector3(0.5f, 1.5f, 0f), Positions["above"]);
  }

  [Fact]
  public void PingPongMover_CarryRidersOffMovesNobody()
  {
    var script = SidewaysPlatform(false);

    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(1f, 0f, 0f), Positions["me"]);
    AssertVec(new Vector3(0f, 1.5f, 0f), Positions["above"]);
  }

  [Fact]
  public void PingPongMover_DoesNotCarryRidersWhilePaused()
  {
    var script = SidewaysPlatform(true);

    Ticks(script, 8, 0.25f);
    AssertVec(new Vector3(4f, 1.5f, 0f), Positions["above"]);

    Ticks(script, 4, 0.25f);
    AssertVec(new Vector3(4f, 1.5f, 0f), Positions["above"]);

    Tick(script, 0.25f);
    AssertVec(new Vector3(3.5f, 1.5f, 0f), Positions["above"]);
  }

  [Fact]
  public void PingPongMover_CarriesARiderUpAVerticalLeg()
  {
    var script = Platform(false);
    AddObject("above", new Vector3(0f, 1.5f, 0f));
    AddBody("above");
    script.onCollisionEnter("above");

    Ticks(script, 2, 0.25f);

    AssertVec(new Vector3(0f, 1f, 0f), Positions["me"]);
    AssertVec(new Vector3(0f, 2.5f, 0f), Positions["above"]);
  }

  [Fact]
  public void PingPongMover_ADestroyedRiderIsDroppedAndTheOthersCarryOn()
  {
    var script = SidewaysPlatform(true);
    AddObject("second", new Vector3(0f, 1.5f, 1f));
    AddBody("second");
    script.onCollisionEnter("second");
    Tick(script, 0.25f);
    AssertVec(new Vector3(0.5f, 1.5f, 0f), Positions["above"]);

    Positions.Remove("above");
    Tick(script, 0.25f);

    AssertVec(new Vector3(1f, 1.5f, 1f), Positions["second"]);
    Assert.False(Positions.ContainsKey("above"));
  }

  [Fact]
  public void PingPongMover_ARiderThatIsAChildIsNotCarried()
  {
    var script = SidewaysPlatform(true);
    LocalPositions["above"] = new Vector3(0f, 0.5f, 0f);

    Tick(script, 0.25f);

    AssertVec(new Vector3(0.5f, 0f, 0f), Positions["me"]);
    AssertVec(new Vector3(0f, 1.5f, 0f), Positions["above"]);
  }

  [Fact]
  public void PingPongMover_TheTopComesFromABoxColliderOffsetAndSize()
  {
    var script = SidewaysPlatform(true);
    FakeNatives.Set("Collider.has", true);
    FakeNatives.Set("Collider.getShape", (int)ColliderShape.Box);
    FakeNatives.Set("Collider.getBoxOffset", new Vector3(0f, 1f, 0f));
    FakeNatives.Set("Collider.getBoxOffset.ok", true);
    FakeNatives.Set("Collider.getBoxSize", new Vector3(1f, 2f, 1f));
    FakeNatives.Set("Collider.getBoxSize.ok", true);
    AddObject("low", new Vector3(0f, 1.8f, 0f));
    AddBody("low");
    script.onCollisionEnter("low");

    Tick(script, 0.25f);

    // Top is 0 + offset 1 + size 2 * scale 0.5 = 2: the riders at 1.5 and 1.8 sit inside it, not on it.
    AssertVec(new Vector3(0f, 1.5f, 0f), Positions["above"]);
    AssertVec(new Vector3(0f, 1.8f, 0f), Positions["low"]);

    AddObject("high", new Vector3(0f, 2.2f, 0f));
    AddBody("high");
    script.onCollisionEnter("high");
    Tick(script, 0.25f);
    AssertVec(new Vector3(0.5f, 2.2f, 0f), Positions["high"]);
  }

  [Fact]
  public void PingPongMover_AnotherColliderShapeFallsBackToTheYScale()
  {
    var script = SidewaysPlatform(true);
    FakeNatives.Set("Collider.has", true);
    FakeNatives.Set("Collider.getShape", (int)ColliderShape.Sphere);
    FakeNatives.Set("Collider.getBoxOffset", new Vector3(0f, 1f, 0f));
    FakeNatives.Set("Collider.getBoxOffset.ok", true);
    FakeNatives.Set("Collider.getBoxSize", new Vector3(1f, 2f, 1f));
    FakeNatives.Set("Collider.getBoxSize.ok", true);

    Tick(script, 0.25f);

    AssertVec(new Vector3(0.5f, 1.5f, 0f), Positions["above"]);
    AssertVec(new Vector3(0f, 0.2f, 0f), Positions["below"]);
  }

  [Fact]
  public void PingPongMover_ZeroPauseTurnsAroundAtOnce()
  {
    var script = Platform(false);
    typeof(PingPongMover).GetField("m_pauseSeconds", BindingFlags.Instance | BindingFlags.NonPublic)!
      .SetValue(script, 0f);

    Ticks(script, 8, 0.25f);
    AssertVec(new Vector3(0f, 4f, 0f), Positions["me"]);

    Tick(script, 0.25f);
    AssertVec(new Vector3(0f, 3.5f, 0f), Positions["me"]);
  }

  [Fact]
  public void PingPongMover_ZeroSpeedHoldsStillWithoutThrowing()
  {
    var script = Platform(false);
    var speed = typeof(PingPongMover).GetField("m_speed", BindingFlags.Instance | BindingFlags.NonPublic)!;
    speed.SetValue(script, 0f);

    Ticks(script, 4, 0.25f);
    AssertVec(Vector3.Zero, Positions["me"]);

    speed.SetValue(script, 2f);
    Ticks(script, 2, 0.25f);
    AssertVec(new Vector3(0f, 1f, 0f), Positions["me"]);
  }
}
