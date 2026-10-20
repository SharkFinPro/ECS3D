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

// DayNightCycle against a WorldModel and FakeNatives.
[Collection("BridgeInstances")]
public class DayNightCycleTests : StarterWorldFixture
{
  private DayNightCycle NewCycle(float startTime, bool spot = false, float diffuse = 0.8f)
  {
    _world.Add("sun", "Sun", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("LightRenderer.has", true);
    FakeNatives.Set("LightRenderer.getDiffuse", diffuse);
    FakeNatives.Set("LightRenderer.getIsSpotLight", spot);
    var cycle = new DayNightCycle();
    SetField(cycle, "m_startTime", startTime);
    return cycle;
  }

  [Fact]
  public void Noon_PutsTheLightAtTheTopOfTheArcAroundTheCenter()
  {
    var cycle = NewCycle(0.25f);
    SetField(cycle, "m_arcCenter", new Vector3(10f, 5f, -3f));

    cycle.fixedUpdate(0.0f);

    AssertVec(new Vector3(10f, 45f, -3f), _world.PositionOf("sun"));
    Assert.Empty(_world.Warnings);
  }

  [Fact]
  public void TheArcStartsAtTheEasternHorizonAndReachesNoonAfterAQuarterPeriod()
  {
    var cycle = NewCycle(0.0f);

    cycle.fixedUpdate(0.0f);
    AssertVec(new Vector3(40f, 0f, 0f), _world.PositionOf("sun"));

    cycle.fixedUpdate(30.0f);
    AssertVec(new Vector3(0f, 40f, 0f), _world.PositionOf("sun"));

    cycle.fixedUpdate(60.0f);
    AssertVec(new Vector3(0f, -40f, 0f), _world.PositionOf("sun"));
  }

  [Fact]
  public void TheDayWrapsAfterAFullPeriod()
  {
    var cycle = NewCycle(0.0f);

    cycle.fixedUpdate(150.0f);

    AssertVec(new Vector3(0f, 40f, 0f), _world.PositionOf("sun"));
  }

  [Fact]
  public void Noon_UsesTheDayColorAndAmbient_AndKeepsTheLightsOwnDiffuse()
  {
    var cycle = NewCycle(0.25f);

    cycle.fixedUpdate(0.0f);

    var color = ArgsOf("LightRenderer.setColor");
    AssertVec(new Vector3(1.0f, 0.96f, 0.88f), new Vector3(color[0], color[1], color[2]), 1e-4f);
    Assert.Equal(0.3f, ArgsOf("LightRenderer.setAmbient")[0], 1e-4f);
    Assert.Equal(0.8f, ArgsOf("LightRenderer.setDiffuse")[0], 1e-4f);
  }

  [Fact]
  public void Midnight_UsesTheNightColorAndAmbient_AndDimsTheDiffuseWithIt()
  {
    var cycle = NewCycle(0.75f);

    cycle.fixedUpdate(0.0f);

    AssertVec(new Vector3(0f, -40f, 0f), _world.PositionOf("sun"));
    var color = ArgsOf("LightRenderer.setColor");
    AssertVec(new Vector3(0.1f, 0.15f, 0.4f), new Vector3(color[0], color[1], color[2]), 1e-4f);
    Assert.Equal(0.06f, ArgsOf("LightRenderer.setAmbient")[0], 1e-4f);
    Assert.Equal(0.16f, ArgsOf("LightRenderer.setDiffuse")[0], 1e-4f);
  }

  [Fact]
  public void AtTheHorizon_TheColorIsDusk_AndTheAmbientStaysAtTheNightFloor()
  {
    var cycle = NewCycle(0.5f);

    cycle.fixedUpdate(0.0f);

    var color = ArgsOf("LightRenderer.setColor");
    AssertVec(new Vector3(1.0f, 0.5f, 0.2f), new Vector3(color[0], color[1], color[2]));
    Assert.Equal(0.06f, ArgsOf("LightRenderer.setAmbient")[0], 1e-4f);
  }

  [Fact]
  public void MidMorning_BlendsDuskTowardDayAndAmbientTowardDay()
  {
    // Height 0.15 is halfway between the dusk (0) and day (0.3) thresholds.
    var phase = MathF.Asin(0.15f) / (2.0f * MathF.PI);
    var cycle = NewCycle(phase);

    cycle.fixedUpdate(0.0f);

    var color = ArgsOf("LightRenderer.setColor");
    var expected = Vector3.Lerp(new Vector3(1.0f, 0.5f, 0.2f), new Vector3(1.0f, 0.96f, 0.88f), 0.5f);
    AssertVec(expected, new Vector3(color[0], color[1], color[2]));
    Assert.Equal(0.06f + (0.3f - 0.06f) * 0.15f, ArgsOf("LightRenderer.setAmbient")[0], 1e-3f);
  }

  [Fact]
  public void ASpotLight_IsAimedAtTheCenter_AndAPointLightIsNotGivenADirection()
  {
    var spot = NewCycle(0.25f, spot: true);
    spot.fixedUpdate(0.0f);
    var down = ArgsOf("LightRenderer.setDirection");
    AssertVec(new Vector3(0f, -1f, 0f), new Vector3(down[0], down[1], down[2]));

    FakeNatives.Calls.Clear();
    spot.fixedUpdate(30.0f);
    var west = ArgsOf("LightRenderer.setDirection");
    var toCenter = Vector3.Normalize(-_world.PositionOf("sun"));
    AssertVec(toCenter, new Vector3(west[0], west[1], west[2]));

    FakeNatives.Calls.Clear();
    FakeNatives.Set("LightRenderer.getIsSpotLight", false);
    spot.fixedUpdate(0.0f);
    Assert.True(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.setAmbient(", StringComparison.Ordinal)));
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.setDirection(", StringComparison.Ordinal)));
  }

  [Fact]
  public void MissingLight_WarnsOnceAndTouchesNothing_ThenWorksOnceItAppears()
  {
    var cycle = new DayNightCycle();
    SetField(cycle, "m_startTime", 0.25f);
    FakeNatives.Set("LightRenderer.has", true);

    for (var tick = 0; tick < 12; tick++)
    {
      cycle.fixedUpdate(0.25f);
    }

    var warnings = _world.Warnings;
    Assert.Single(warnings);
    Assert.Contains("Sun", warnings[0]);
    Assert.Empty(_world.Moves);
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.set", StringComparison.Ordinal)));

    _world.Add("sun", "Sun", Vector3.Zero);
    for (var tick = 0; tick < 4; tick++)
    {
      cycle.fixedUpdate(0.25f);
    }

    Assert.NotEmpty(_world.Moves);
    Assert.True(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.setColor(", StringComparison.Ordinal)));
  }

  private static float LastDiffuseSetOn(string uuid)
  {
    var call = FakeNatives.Calls.Last(c => c.StartsWith($"LightRenderer.setDiffuse({uuid},", StringComparison.Ordinal));
    return float.Parse(call.Substring(call.LastIndexOf(',') + 1).TrimEnd(')'), CultureInfo.InvariantCulture);
  }

  [Fact]
  public void TheBaseDiffuseIsReadOncePerLight_SoDimmingDoesNotCompound()
  {
    var cycle = NewCycle(0.75f);
    cycle.fixedUpdate(0.0f);
    Assert.Equal(0.16f, LastDiffuseSetOn("sun"), 1e-4f);

    FakeNatives.Set("LightRenderer.getDiffuse", 0.16f);
    cycle.fixedUpdate(60.0f);

    Assert.Equal(0.8f, LastDiffuseSetOn("sun"), 1e-4f);
    Assert.Equal(1, FakeNatives.Calls.Count(c => c == "LightRenderer.getDiffuse(sun)"));
  }

  [Fact]
  public void ChangingTheLightName_RestoresThePreviousLightsDiffuse()
  {
    var cycle = NewCycle(0.75f);
    _world.Add("moon", "Moon", Vector3.Zero);
    cycle.fixedUpdate(0.0f);
    Assert.Equal(0.16f, LastDiffuseSetOn("sun"), 1e-4f);

    SetField(cycle, "m_light", "Moon");
    cycle.fixedUpdate(0.0f);

    Assert.Equal(0.8f, LastDiffuseSetOn("sun"), 1e-4f);
    Assert.Equal(0.16f, LastDiffuseSetOn("moon"), 1e-4f);
  }

  [Fact]
  public void Stopping_RestoresTheLightsDiffuse()
  {
    var cycle = NewCycle(0.75f);
    cycle.fixedUpdate(0.0f);
    Assert.Equal(0.16f, LastDiffuseSetOn("sun"), 1e-4f);

    cycle.stop();

    Assert.Equal(0.8f, LastDiffuseSetOn("sun"), 1e-4f);
  }

  [Fact]
  public void ANonPositivePeriod_HoldsTheStartTimeInsteadOfDividingByZero()
  {
    var cycle = NewCycle(0.25f);
    SetField(cycle, "m_periodSeconds", 0.0f);

    cycle.fixedUpdate(10.0f);

    AssertVec(new Vector3(0f, 40f, 0f), _world.PositionOf("sun"));
  }

  [Fact]
  public void ALightWithARigidBody_WarnsOnceThatPhysicsAndTheScriptFight()
  {
    var cycle = NewCycle(0.25f);
    FakeNatives.Set("RigidBody.has", true);

    for (var tick = 0; tick < 3; tick++)
    {
      cycle.fixedUpdate(0.02f);
    }

    var warnings = _world.Warnings;
    Assert.Single(warnings);
    Assert.Contains("RigidBody", warnings[0]);
  }

  [Fact]
  public void ALightObjectWithoutALightRenderer_StillMovesAndWarnsOnce()
  {
    var cycle = NewCycle(0.25f);
    FakeNatives.Set("LightRenderer.has", false);

    cycle.fixedUpdate(0.02f);
    cycle.fixedUpdate(0.02f);

    Assert.Equal(2, _world.Moves.Count);
    Assert.Single(_world.Warnings);
    Assert.False(FakeNatives.Calls.Any(c => c.StartsWith("LightRenderer.set", StringComparison.Ordinal)));
  }
}
