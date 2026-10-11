using System;
using System.Collections.Generic;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// SceneScript beside ScriptBase over Bridge's shared instance table: what the two share (timers, events,
// the fault gate), what an object lookup must never see, and which creation path accepts which kind.
[Collection("BridgeInstances")]
public class SceneScriptTests : IDisposable
{
  public class Round : SceneScript
  {
    public List<string> Events { get; } = new();
    public int StartCalls { get; private set; }
    public int FixedCalls { get; private set; }
    public int TimerCalls { get; private set; }
    public bool ThrowInFixed { get; set; }

    public void ScheduleEvery(float seconds) => every(seconds, () => TimerCalls++);

    public Subscription Listen(string eventName) => subscribe(eventName, payload => Events.Add($"{payload}"));

    public int Announce(string eventName, object? payload) => publish(eventName, payload);

    public void Ping() => FixedCalls += 100;

    public override void start() => StartCalls++;

    public override void fixedUpdate(float dt)
    {
      FixedCalls++;
      if (ThrowInFixed)
      {
        throw new InvalidOperationException("boom");
      }
    }
  }

  public class ObjectListener : ScriptBase
  {
    public List<object?> Received { get; } = new();

    public void Listen(string eventName) => subscribe(eventName, payload => Received.Add(payload));

    public int Announce(string eventName, object? payload) => publish(eventName, payload);

    public void Ping()
    {
    }
  }

  public class ThrowingSceneScript : SceneScript
  {
    public ThrowingSceneScript() => throw new InvalidOperationException("ctor");
  }

  private const string SceneUuid = "scene-uuid";
  private const string ObjectUuid = "object-uuid";

  public SceneScriptTests()
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
  public void AddInstanceAssignsTheSceneIdBeforeTheScriptIsRegistered()
  {
    var round = new Round();
    Assert.Equal("", round.SceneId);

    Bridge.AddInstance(SceneUuid, nameof(Round), round);

    Assert.Equal(SceneUuid, round.SceneId);
    Assert.True(Bridge.IsHealthy(SceneUuid, nameof(Round)));
  }

  [Fact]
  public void FixedUpdateTicksASceneScriptsTimersBeforeItsOwnFixedUpdate()
  {
    var round = new Round();
    Bridge.AddInstance(SceneUuid, nameof(Round), round);
    round.ScheduleEvery(1f);

    Bridge.RunFixedUpdate(SceneUuid, nameof(Round), 0.5f);
    Assert.Equal(0, round.TimerCalls);
    Assert.Equal(1, round.FixedCalls);

    Bridge.RunFixedUpdate(SceneUuid, nameof(Round), 0.5f);

    Assert.Equal(1, round.TimerCalls);
    Assert.Equal(2, round.FixedCalls);
  }

  [Fact]
  public void ASceneScriptCanOnlySubscribeOnceAttached()
  {
    var round = new Round();

    var refused = round.Listen("roundEnded");
    Assert.False(refused.isActive);

    Bridge.AddInstance(SceneUuid, nameof(Round), round);
    var accepted = round.Listen("roundEnded");

    Assert.True(accepted.isActive);
  }

  [Fact]
  public void ASceneScriptAndAnObjectScriptExchangeEventsBothWays()
  {
    var round = new Round();
    var listener = new ObjectListener();
    Bridge.AddInstance(SceneUuid, nameof(Round), round);
    Bridge.AddInstance(ObjectUuid, nameof(ObjectListener), listener);
    round.Listen("playerScored");
    listener.Listen("roundEnded");

    var toScene = listener.Announce("playerScored", "blue");
    var toObject = round.Announce("roundEnded", 7);

    Assert.Equal(1, toScene);
    Assert.Equal(1, toObject);
    Assert.Equal(new[] { "blue" }, round.Events);
    Assert.Equal(new object?[] { 7 }, listener.Received);
  }

  [Fact]
  public void ASceneScriptsSubscriptionsEndWhenItIsRemoved()
  {
    var round = new Round();
    var announcer = new ObjectListener();
    Bridge.AddInstance(SceneUuid, nameof(Round), round);
    Bridge.AddInstance(ObjectUuid, nameof(ObjectListener), announcer);
    round.Listen("tick");
    Assert.Equal(1, announcer.Announce("tick", "a"));

    Bridge.RemoveInstance(SceneUuid, nameof(Round));

    Assert.Equal(0, announcer.Announce("tick", "b"));
    Assert.Equal(new[] { "a" }, round.Events);
  }

  [Fact]
  public void AThrowingSceneScriptFaultsItselfAndStopsReceivingTicks()
  {
    var round = new Round { ThrowInFixed = true };
    Bridge.AddInstance(SceneUuid, nameof(Round), round);

    var thrown = Record.Exception(() => Bridge.RunFixedUpdate(SceneUuid, nameof(Round), 0.1f));
    Bridge.RunFixedUpdate(SceneUuid, nameof(Round), 0.1f);

    Assert.Null(thrown);
    Assert.False(Bridge.IsHealthy(SceneUuid, nameof(Round)));
    Assert.Equal(1, round.FixedCalls);
  }

  [Fact]
  public void ObjectLookupsNeverReturnASceneScriptEvenUnderTheSameUuid()
  {
    var round = new Round();
    var listener = new ObjectListener();
    Bridge.AddInstance(SceneUuid, nameof(Round), round);
    Bridge.AddInstance(SceneUuid, nameof(ObjectListener), listener);

    Assert.Same(listener, Bridge.FindScript<ObjectListener>(SceneUuid));
    Assert.Same(listener, Bridge.FindScript<ScriptBase>(SceneUuid));
    Assert.Equal(new ScriptBase[] { listener }, Bridge.FindScripts(SceneUuid));
    Assert.True(Bridge.TryFindScript(SceneUuid, nameof(ObjectListener), out _));

    Assert.False(Bridge.TryFindScript(SceneUuid, nameof(Round), out var handle));
    Assert.Null(handle);
  }

  [Fact]
  public void ObjectLookupsFindNothingWhenOnlyASceneScriptIsRegistered()
  {
    Bridge.AddInstance(SceneUuid, nameof(Round), new Round());

    Assert.Null(Bridge.FindScript<ScriptBase>(SceneUuid));
    Assert.Empty(Bridge.FindScripts(SceneUuid));
    Assert.False(Bridge.TryFindScript(SceneUuid, nameof(Round), out _));
    Assert.False(Bridge.TryFindScript<ObjectListener>(SceneUuid, out _));
  }

  [Fact]
  public void InvokeByNameRefusesTheSceneScriptLifecycleButRunsItsOwnMethods()
  {
    var round = new Round();
    Bridge.AddInstance(SceneUuid, nameof(Round), round);
    var none = Array.Empty<object?>();

    Assert.True(Bridge.TryInvokeScript(SceneUuid, nameof(Round), round, "Ping", none, out _));
    Assert.Equal(100, round.FixedCalls);

    Assert.False(Bridge.TryInvokeScript(SceneUuid, nameof(Round), round, "start", none, out _));
    Assert.False(Bridge.TryInvokeScript(SceneUuid, nameof(Round), round, "fixedUpdate", new object?[] { 0.1f },
                                        out _));
    Assert.False(Bridge.TryInvokeScript(SceneUuid, nameof(Round), round, "variableUpdate", none, out _));
    Assert.False(Bridge.TryInvokeScript(SceneUuid, nameof(Round), round, "stop", none, out _));
    Assert.Equal(0, round.StartCalls);
    Assert.Equal(100, round.FixedCalls);
  }

  [Fact]
  public void ObjectScriptsStillRefuseTheLifecycleDeclaredOnTheSharedBase()
  {
    var listener = new ObjectListener();
    Bridge.AddInstance(ObjectUuid, nameof(ObjectListener), listener);
    var none = Array.Empty<object?>();

    Assert.True(Bridge.TryInvokeScript(ObjectUuid, nameof(ObjectListener), listener, "Ping", none, out _));
    Assert.False(Bridge.TryInvokeScript(ObjectUuid, nameof(ObjectListener), listener, "start", none, out _));
    Assert.False(Bridge.TryInvokeScript(ObjectUuid, nameof(ObjectListener), listener, "onCollisionEnter",
                                        new object?[] { "x" }, out _));
  }

  [Fact]
  public void TheObjectPathCreatesAScriptBaseAndRefusesASceneScript()
  {
    Assert.True(Bridge.TryCreateInstance(typeof(ObjectListener), ObjectUuid, nameof(ObjectListener)));
    Assert.True(Bridge.IsHealthy(ObjectUuid, nameof(ObjectListener)));

    Assert.False(Bridge.TryCreateInstance(typeof(Round), ObjectUuid, nameof(Round)));
    Assert.False(Bridge.IsHealthy(ObjectUuid, nameof(Round)));
  }

  [Fact]
  public void TheScenePathCreatesASceneScriptAndRefusesAScriptBase()
  {
    Assert.True(Bridge.TryCreateSceneInstance(typeof(Round), SceneUuid, nameof(Round)));
    Assert.True(Bridge.IsHealthy(SceneUuid, nameof(Round)));

    Assert.False(Bridge.TryCreateSceneInstance(typeof(ObjectListener), SceneUuid, nameof(ObjectListener)));
    Assert.False(Bridge.IsHealthy(SceneUuid, nameof(ObjectListener)));
    Assert.False(Bridge.TryCreateSceneInstance(null, SceneUuid, "Missing"));
  }

  [Fact]
  public void AWrongKindIsRefusedWithoutFaultingSoTheRightKindCanStillAttachUnderTheSameKey()
  {
    Assert.False(Bridge.TryCreateInstance(typeof(Round), SceneUuid, nameof(Round)));

    Assert.True(Bridge.TryCreateSceneInstance(typeof(Round), SceneUuid, nameof(Round)));
    Assert.True(Bridge.IsHealthy(SceneUuid, nameof(Round)));
  }

  [Fact]
  public void AThrowingSceneScriptConstructorReportsFailureWithoutRegisteringAnything()
  {
    var thrown = Record.Exception(() =>
      Assert.False(Bridge.TryCreateSceneInstance(typeof(ThrowingSceneScript), SceneUuid,
                                                 nameof(ThrowingSceneScript))));

    Assert.Null(thrown);
    Assert.False(Bridge.IsHealthy(SceneUuid, nameof(ThrowingSceneScript)));
  }
}
