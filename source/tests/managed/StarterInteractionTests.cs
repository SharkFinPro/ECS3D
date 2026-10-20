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

// Explosion, Collectible, LaserPointer and ColorOnHit against FakeNatives. Log lines come through a recording
// Log binding.
[Collection("BridgeInstances")]
public unsafe class StarterInteractionTests : IDisposable
{
  private const string PaletteRed = "7c8c01db-771f-4048-94c2-004b8d19e7aa";

  private readonly IDisposable _natives = FakeNatives.Install();
  private readonly LogBindings _originalLog = NativeBindings.Log;
  private readonly List<Subscription> _subscriptions = new();

  private static readonly List<(int Level, string Message)> LogCalls = new();

  [UnmanagedCallersOnly]
  private static void RecordLog(int level, IntPtr message)
    => LogCalls.Add((level, Marshal.PtrToStringUTF8(message) ?? ""));

  public StarterInteractionTests()
  {
    LogCalls.Clear();
    NativeBindings.Log = new LogBindings { write = &RecordLog };
  }

  public void Dispose()
  {
    foreach (var subscription in _subscriptions)
    {
      subscription.unsubscribe();
    }

    NativeBindings.Log = _originalLog;
    LogCalls.Clear();
    var error = FakeNatives.LastError;
    _natives.Dispose();
    Assert.Null(error);
  }

  private static string[] Warnings() => LogCalls.Where(c => c.Level == 3).Select(c => c.Message).ToArray();

  private static string[] Infos() => LogCalls.Where(c => c.Level == 2).Select(c => c.Message).ToArray();

  private static void SetField(object script, string name, object value) =>
    script.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(script, value);

  private static void Present(string name, string uuid)
  {
    FakeNatives.SetString($"World.findObjectByName:{name}", uuid);
    FakeNatives.Set($"World.objectExists:{uuid}", true);
  }

  private static string[] Args(string call) => call.Substring(call.IndexOf('(') + 1).TrimEnd(')').Split(',');

  // The numbers after the leading uuid.
  private static float[] Numbers(string call) =>
    Args(call).Skip(1).Select(a => float.Parse(a, CultureInfo.InvariantCulture)).ToArray();

  private static List<string> CallsTo(string prefix) =>
    FakeNatives.Calls.Where(c => c.StartsWith(prefix + "(", StringComparison.Ordinal)).ToList();

  private static void AssertVec(Vector3 expected, Vector3 actual, float tolerance = 1e-4f) =>
    Assert.True(Vector3.Distance(expected, actual) <= tolerance, $"expected {expected} but got {actual}");

  // ---- Explosion ----

  private static Explosion NewExplosion(float cooldown = 0.0f, float fireEvery = 0.0f)
  {
    var explosion = new Explosion { EntityId = "boom" };
    explosion.initComponents();
    SetField(explosion, "m_radius", 8.0f);
    SetField(explosion, "m_cooldown", cooldown);
    SetField(explosion, "m_fireEvery", fireEvery);
    FakeNatives.Set("Transform.getPosition:boom", new Vector3(10f, 0f, 0f));
    FakeNatives.Set("Transform.has", true);
    return explosion;
  }

  private static void Body(string uuid, Vector3 position)
  {
    FakeNatives.Set($"RigidBody.has:{uuid}", true);
    FakeNatives.Set($"Transform.getPosition:{uuid}", position);
  }

  private static void Hold(bool down) => FakeNatives.Set("InputUtils.keyIsPressed", down);

  private static int Blasts(string uuid) =>
    CallsTo("RigidBody.applyForce").Count(c => c.StartsWith($"RigidBody.applyForce({uuid},", StringComparison.Ordinal));

  [Fact]
  public void Explosion_FreshPressFiresOnceWhileHeldAndAgainAfterRelease()
  {
    var explosion = NewExplosion();
    Body("a", new Vector3(14f, 0f, 0f));
    FakeNatives.SetString("World.overlapSphere", "a");

    Hold(false);
    explosion.fixedUpdate(0.1f);
    Assert.Equal(0, Blasts("a"));

    Hold(true);
    for (var tick = 0; tick < 5; tick++)
    {
      explosion.fixedUpdate(0.1f);
    }

    Assert.Equal(1, Blasts("a"));

    Hold(false);
    explosion.fixedUpdate(0.1f);
    Hold(true);
    explosion.fixedUpdate(0.1f);

    Assert.Equal(2, Blasts("a"));
  }

  [Fact]
  public void Explosion_CooldownSwallowsAPressInsideItAndAllowsOneAfter()
  {
    var explosion = NewExplosion(cooldown: 1.5f);
    Body("a", new Vector3(14f, 0f, 0f));
    FakeNatives.SetString("World.overlapSphere", "a");
    var dt = 0.5f;

    Hold(true);
    explosion.fixedUpdate(dt);
    Assert.Equal(1, Blasts("a"));

    Hold(false);
    explosion.fixedUpdate(dt);
    Hold(true);
    explosion.fixedUpdate(dt);
    Assert.Equal(1, Blasts("a"));

    Hold(false);
    explosion.fixedUpdate(dt);
    Hold(true);
    explosion.fixedUpdate(dt);
    Assert.Equal(2, Blasts("a"));
  }

  [Fact]
  public void Explosion_FireEveryFiresOnItsOwnAndZeroTurnsItOff()
  {
    var explosion = NewExplosion();
    SetField(explosion, "m_key", "");
    Body("a", new Vector3(14f, 0f, 0f));
    FakeNatives.SetString("World.overlapSphere", "a");
    Hold(false);

    for (var tick = 0; tick < 8; tick++)
    {
      explosion.fixedUpdate(0.25f);
    }

    Assert.Equal(0, Blasts("a"));

    SetField(explosion, "m_fireEvery", 1.0f);
    for (var tick = 0; tick < 3; tick++)
    {
      explosion.fixedUpdate(0.25f);
    }

    Assert.Equal(0, Blasts("a"));
    explosion.fixedUpdate(0.25f);
    Assert.Equal(1, Blasts("a"));

    for (var tick = 0; tick < 4; tick++)
    {
      explosion.fixedUpdate(0.25f);
    }

    Assert.Equal(2, Blasts("a"));
    Assert.Empty(Warnings());
  }

  [Fact]
  public void Explosion_PushesAwayAndTiltedUpWithLinearFalloff()
  {
    var explosion = NewExplosion();
    var halfWay = new Vector3(14f, 0f, 0f);
    var closeUnder = new Vector3(10f, 0f, 6f);
    Body("half", halfWay);
    Body("far", closeUnder);
    Body("middle", new Vector3(10f, 0f, 0f));
    Body("edge", new Vector3(18f, 0f, 0f));
    FakeNatives.SetString("World.overlapSphere", "half,far,middle,edge");

    Hold(true);
    explosion.fixedUpdate(0.1f);

    // Half of Radius 8: 18 * 0.5 = 9 units a second, 0.9 a tick, along (1, 0.3, 0) normalized.
    var half = Numbers(CallsTo("RigidBody.applyForce").Single(c => c.StartsWith("RigidBody.applyForce(half,")));
    AssertVec(Vector3.Normalize(new Vector3(1f, 0.3f, 0f)) * 0.9f, new Vector3(half[0], half[1], half[2]));
    AssertVec(halfWay, new Vector3(half[3], half[4], half[5]));
    Assert.Equal((float)ForceMode.VelocityChange, half[6]);

    // Distance 6 of 8: 18 * 0.25 = 4.5 a second, 0.45 a tick, along (0, 0.3, 1) normalized.
    var far = Numbers(CallsTo("RigidBody.applyForce").Single(c => c.StartsWith("RigidBody.applyForce(far,")));
    AssertVec(Vector3.Normalize(new Vector3(0f, 0.3f, 1f)) * 0.45f, new Vector3(far[0], far[1], far[2]));

    // On the center: straight up, full strength (18 * 0.1 = 1.8 a tick).
    var middle = Numbers(CallsTo("RigidBody.applyForce").Single(c => c.StartsWith("RigidBody.applyForce(middle,")));
    AssertVec(new Vector3(0f, 1.8f, 0f), new Vector3(middle[0], middle[1], middle[2]));

    // At the edge the falloff reaches zero, so nothing is applied and the body is not counted.
    Assert.Equal(0, Blasts("edge"));
    Assert.Equal(new[] { "Explosion hit 3 bodies" }, Infos());
  }

  [Fact]
  public void Explosion_SkipsItselfAndBodiesWithoutARigidBodyAndCountsTheRest()
  {
    var explosion = NewExplosion();
    Body("a", new Vector3(14f, 0f, 0f));
    FakeNatives.Set("RigidBody.has:boom", true);
    FakeNatives.SetString("World.overlapSphere", "boom,rock,a");

    Hold(true);
    explosion.fixedUpdate(0.1f);

    var pushed = CallsTo("RigidBody.applyForce").Select(c => c.Split(new[] { '(', ',' })[1]).ToArray();
    Assert.Equal(new[] { "a" }, pushed);
    Assert.Equal(new[] { "Explosion hit 1 bodies" }, Infos());
  }

  [Fact]
  public void Explosion_UnknownKeyWarnsOnceAndNeverFiresByKey()
  {
    var broken = NewExplosion();
    SetField(broken, "m_key", "Banana");
    Body("a", new Vector3(14f, 0f, 0f));
    FakeNatives.SetString("World.overlapSphere", "a");

    Hold(true);
    for (var tick = 0; tick < 5; tick++)
    {
      broken.fixedUpdate(0.1f);
    }

    Assert.Equal(0, Blasts("a"));
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("Banana", warnings[0]);

    var working = NewExplosion();
    Hold(false);
    working.fixedUpdate(0.1f);
    Hold(true);
    working.fixedUpdate(0.1f);
    Assert.Equal(1, Blasts("a"));
    Assert.Single(Warnings());
  }

  // ---- Collectible ----

  private static Collectible NewCollectible(int value = 1)
  {
    var coin = new Collectible { EntityId = "coin" };
    SetField(coin, "m_value", value);
    FakeNatives.Set("PlayerController.has:p1", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:p1", 2);
    FakeNatives.Set("PlayerController.has:p2", true);
    FakeNatives.Set("PlayerController.getPlayerSlot:p2", 3);
    return coin;
  }

  private List<object?> Listen(string eventName)
  {
    var seen = new List<object?>();
    _subscriptions.Add(Bridge.Events.subscribe("interaction-test", eventName, payload => seen.Add(payload)));
    return seen;
  }

  [Fact]
  public void Collectible_PublishesOnceWithTheRightPayloadAndDestroysItself()
  {
    var coin = NewCollectible(value: 5);
    var collected = Listen(StarterEvents.Collected);

    coin.onCollisionEnter("p1");

    var payload = Assert.IsType<CollectedEvent>(Assert.Single(collected));
    Assert.Equal(2, payload.Slot);
    Assert.Equal(5, payload.Value);
    Assert.Equal("p1", payload.CollectorUuid);
    Assert.Equal(new[] { "World.destroyObject(coin)" }, CallsTo("World.destroyObject"));

    coin.onCollisionEnter("p1");
    coin.onCollisionEnter("p2");

    Assert.Single(collected);
    Assert.Single(CallsTo("World.destroyObject"));
  }

  [Fact]
  public void Collectible_IgnoresObjectsWithoutAPlayerController()
  {
    var coin = NewCollectible();
    var collected = Listen(StarterEvents.Collected);

    coin.onCollisionEnter("rock");

    Assert.Empty(collected);
    Assert.Empty(CallsTo("World.destroyObject"));

    coin.onCollisionEnter("p2");

    Assert.Single(collected);
    Assert.Single(CallsTo("World.destroyObject"));
  }

  [Fact]
  public void Collectible_PublishesUnderItsEventNameField()
  {
    var coin = NewCollectible();
    SetField(coin, "m_eventName", "gem");
    var gems = Listen("gem");
    var collected = Listen(StarterEvents.Collected);

    coin.onCollisionEnter("p1");

    Assert.Single(gems);
    Assert.Empty(collected);
  }

  // ---- LaserPointer ----

  private static LaserPointer NewLaser(string marker = "Marker", bool logChanges = true)
  {
    var laser = new LaserPointer { EntityId = "laser" };
    laser.initComponents();
    SetField(laser, "m_marker", marker);
    SetField(laser, "m_logChanges", logChanges);
    FakeNatives.Set("Transform.getPosition:laser", new Vector3(1f, 2f, 3f));
    FakeNatives.Set("Transform.getRotation", new Vector3(0f, 90f, 0f));
    FakeNatives.Set("Transform.has", true);
    FakeNatives.Set("ModelRenderer.has", true);
    Present("Marker", "mk");
    return laser;
  }

  private static void Aim(string hitUuid, string hitName)
  {
    FakeNatives.SetString("World.raycast", $"{hitUuid},5,1,2,-2,0,0,1");
    FakeNatives.SetString("World.getObjectName", hitName);
  }

  private static void Miss() => FakeNatives.SetString("World.raycast", null);

  [Fact]
  public void LaserPointer_CastsAlongTheTurnedDirectionIgnoringItselfAndMovesTheMarkerToTheHit()
  {
    var laser = NewLaser();
    Aim("target", "Target");

    laser.fixedUpdate(0.1f);

    var args = Args(Assert.Single(CallsTo("World.raycast")));
    var ray = args.Take(7).Select(a => float.Parse(a, CultureInfo.InvariantCulture)).ToArray();
    AssertVec(new Vector3(1f, 2f, 3f), new Vector3(ray[0], ray[1], ray[2]));
    AssertVec(new Vector3(-1f, 0f, 0f), new Vector3(ray[3], ray[4], ray[5]));
    Assert.Equal(50f, ray[6]);
    Assert.Equal("laser", args[8]);
    Assert.Equal(new[] { "Transform.setPosition(mk,1,2,-2)" }, CallsTo("Transform.setPosition"));
    Assert.Equal(new[] { "ModelRenderer.setShouldRender(mk,1)" }, CallsTo("ModelRenderer.setShouldRender"));
  }

  [Fact]
  public void LaserPointer_CombinedRotationFollowsTheEngineEulerConvention()
  {
    var laser = NewLaser();
    FakeNatives.Set("Transform.getRotation", new Vector3(30f, 45f, 20f));
    Aim("target", "Target");

    laser.fixedUpdate(0.1f);

    // (0, 0, -1) turned by glm::quat(radians(30, 45, 20)), i.e. Rz * Ry * Rx, worked out in Python.
    var args = Args(Assert.Single(CallsTo("World.raycast")));
    var direction = new Vector3(
      float.Parse(args[3], CultureInfo.InvariantCulture),
      float.Parse(args[4], CultureInfo.InvariantCulture),
      float.Parse(args[5], CultureInfo.InvariantCulture));
    AssertVec(new Vector3(-0.7464519f, 0.2604026f, -0.6123724f), direction);
  }

  [Fact]
  public void LaserPointer_LogsTheUuidWhenTheHitObjectHasNoName()
  {
    var laser = NewLaser();
    Aim("unnamed-uuid", "");

    laser.fixedUpdate(0.1f);

    Assert.Equal(new[] { "LaserPointer hit 'unnamed-uuid'" }, Infos());
  }

  [Fact]
  public void LaserPointer_HidesTheMarkerOnAMissAndDoesNotMoveIt()
  {
    var laser = NewLaser();
    Aim("target", "Target");
    laser.fixedUpdate(0.1f);
    Assert.Single(CallsTo("Transform.setPosition"));

    Miss();
    laser.fixedUpdate(0.1f);

    Assert.Single(CallsTo("Transform.setPosition"));
    Assert.Equal("ModelRenderer.setShouldRender(mk,0)", CallsTo("ModelRenderer.setShouldRender").Last());
  }

  [Fact]
  public void LaserPointer_WithNoMarkerStillCastsAndTouchesNothing()
  {
    var laser = NewLaser(marker: "");
    Aim("target", "Target");

    laser.fixedUpdate(0.1f);

    Assert.Single(CallsTo("World.raycast"));
    Assert.Empty(CallsTo("Transform.setPosition"));
    Assert.Empty(CallsTo("ModelRenderer.setShouldRender"));
    Assert.Equal(new[] { "LaserPointer hit 'Target'" }, Infos());
  }

  [Fact]
  public void LaserPointer_LogsOnlyWhenTheHitChanges()
  {
    var laser = NewLaser();

    Miss();
    laser.fixedUpdate(0.1f);
    laser.fixedUpdate(0.1f);
    Assert.Empty(Infos());

    Aim("target", "Target");
    for (var tick = 0; tick < 3; tick++)
    {
      laser.fixedUpdate(0.1f);
    }

    Assert.Equal(new[] { "LaserPointer hit 'Target'" }, Infos());

    Aim("other", "Other");
    laser.fixedUpdate(0.1f);
    laser.fixedUpdate(0.1f);
    Assert.Equal(new[] { "LaserPointer hit 'Target'", "LaserPointer hit 'Other'" }, Infos());

    Miss();
    for (var tick = 0; tick < 3; tick++)
    {
      laser.fixedUpdate(0.1f);
    }

    Assert.Equal(new[] { "LaserPointer hit 'Target'", "LaserPointer hit 'Other'", "LaserPointer hit nothing" },
      Infos());
  }

  [Fact]
  public void LaserPointer_LogChangesOffStaysQuietButStillMovesTheMarker()
  {
    var laser = NewLaser(logChanges: false);
    Aim("target", "Target");

    laser.fixedUpdate(0.1f);
    Miss();
    laser.fixedUpdate(0.1f);

    Assert.Empty(Infos());
    Assert.Single(CallsTo("Transform.setPosition"));
  }

  // ---- ColorOnHit ----

  private static ColorOnHit NewColorOnHit(bool revertOnExit = true, bool playersOnly = false, bool renderer = true)
  {
    var pad = new ColorOnHit { EntityId = "pad" };
    SetField(pad, "m_revertOnExit", revertOnExit);
    SetField(pad, "m_playersOnly", playersOnly);
    FakeNatives.Set("ModelRenderer.has", renderer);
    FakeNatives.SetString("ModelRenderer.getTextureUUID", "tex-orig");
    FakeNatives.Set("ModelRenderer.setTextureUUID", true);
    FakeNatives.Set("PlayerController.has:p1", true);
    pad.start();
    return pad;
  }

  private static string[] Textures() =>
    CallsTo("ModelRenderer.setTextureUUID").Select(c => c.Split('(', ',', ')')[2]).ToArray();

  [Fact]
  public void ColorOnHit_SwapsToPaletteRedOnFirstContactAndRestoresWhenTheLastEnds()
  {
    var pad = NewColorOnHit();

    pad.onCollisionEnter("a");
    pad.onCollisionEnter("b");
    Assert.Equal(new[] { PaletteRed }, Textures());

    pad.onCollisionExit("a");
    Assert.Equal(new[] { PaletteRed }, Textures());

    pad.onCollisionExit("b");
    Assert.Equal(new[] { PaletteRed, "tex-orig" }, Textures());
  }

  [Fact]
  public void ColorOnHit_ARepeatedEnterForOneObjectDoesNotNeedTwoExits()
  {
    var pad = NewColorOnHit();

    pad.onCollisionEnter("a");
    pad.onCollisionEnter("a");
    pad.onCollisionExit("a");

    Assert.Equal(new[] { PaletteRed, "tex-orig" }, Textures());

    pad.onCollisionExit("a");
    Assert.Equal(2, Textures().Length);
  }

  [Fact]
  public void ColorOnHit_PlayersOnlyIgnoresOtherObjectsInBothDirections()
  {
    var pad = NewColorOnHit(playersOnly: true);

    pad.onCollisionEnter("rock");
    Assert.Empty(Textures());

    pad.onCollisionEnter("p1");
    Assert.Equal(new[] { PaletteRed }, Textures());

    pad.onCollisionExit("rock");
    Assert.Equal(new[] { PaletteRed }, Textures());

    pad.onCollisionExit("p1");
    Assert.Equal(new[] { PaletteRed, "tex-orig" }, Textures());
  }

  [Fact]
  public void ColorOnHit_RevertOnExitOffKeepsTheHitTexture()
  {
    var pad = NewColorOnHit(revertOnExit: false);

    pad.onCollisionEnter("a");
    pad.onCollisionExit("a");

    Assert.Equal(new[] { PaletteRed }, Textures());
  }

  [Fact]
  public void ColorOnHit_ReadsTheOriginalTextureJustBeforeTheFirstSwap()
  {
    var pad = NewColorOnHit();
    FakeNatives.SetString("ModelRenderer.getTextureUUID", "tex-later");

    pad.onCollisionEnter("a");
    pad.onCollisionExit("a");

    Assert.Equal(new[] { PaletteRed, "tex-later" }, Textures());
  }

  [Fact]
  public void ColorOnHit_AnEmptyOriginalIsNeverRestoredAndWarnsOnce()
  {
    FakeNatives.SetString("ModelRenderer.getTextureUUID", null);
    var pad = NewColorOnHit();
    FakeNatives.SetString("ModelRenderer.getTextureUUID", null);

    for (var round = 0; round < 2; round++)
    {
      pad.onCollisionEnter("a");
      pad.onCollisionExit("a");
    }

    Assert.Equal(new[] { PaletteRed, PaletteRed }, Textures());
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("texture", warnings[0]);
  }

  [Fact]
  public void ColorOnHit_AFailedSwapWarnsOnce()
  {
    var pad = NewColorOnHit();
    FakeNatives.Set("ModelRenderer.setTextureUUID", false);

    pad.onCollisionEnter("a");
    pad.onCollisionExit("a");
    pad.onCollisionEnter("a");

    Assert.Equal(3, Textures().Length);
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains(PaletteRed, warnings[0]);
  }

  [Fact]
  public void ColorOnHit_WithoutAModelRendererWarnsOnceAndChangesNothing()
  {
    var pad = NewColorOnHit(renderer: false);

    pad.onCollisionEnter("a");
    pad.onCollisionExit("a");

    Assert.Empty(Textures());
    var warnings = Warnings();
    Assert.Single(warnings);
    Assert.Contains("ModelRenderer", warnings[0]);
  }
}
