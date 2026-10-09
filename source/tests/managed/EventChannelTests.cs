using System;
using System.Collections.Generic;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

public class EventChannelTests
{
  private readonly HashSet<string> _faulted = new();
  private readonly List<(string Owner, string Event, Exception Ex)> _faults = new();
  private readonly EventChannel _channel;
  private bool _faultCheckThrows;

  public EventChannelTests()
  {
    _channel = new EventChannel(owner => _faultCheckThrows ? throw new InvalidOperationException("gate") : _faulted.Contains(owner), (owner, name, ex) =>
    {
      _faulted.Add(owner);
      _faults.Add((owner, name, ex));
    });
  }

  [Fact]
  public void DeliversInSubscriptionOrderAndCountsHandlers()
  {
    var seen = new List<string>();
    _channel.subscribe("a", "go", _ => seen.Add("first"));
    _channel.subscribe("b", "go", _ => seen.Add("second"));
    _channel.subscribe("c", "go", _ => seen.Add("third"));

    var ran = _channel.publish("go", null);

    Assert.Equal(3, ran);
    Assert.Equal(new[] { "first", "second", "third" }, seen);
  }

  [Fact]
  public void DeliversPayload()
  {
    object? got = null;
    _channel.subscribe("a", "go", p => got = p);

    _channel.publish("go", 42);

    Assert.Equal(42, got);
  }

  [Fact]
  public void MatchesExactNameOnly()
  {
    var calls = 0;
    _channel.subscribe("a", "round", _ => calls++);

    Assert.Equal(0, _channel.publish("Round", null));
    Assert.Equal(0, _channel.publish("round ", null));
    Assert.Equal(0, _channel.publish("roun", null));
    Assert.Equal(0, calls);

    Assert.Equal(1, _channel.publish("round", null));
    Assert.Equal(1, calls);
  }

  [Fact]
  public void TypedSubscriptionDeliversOnlyMatchingPayloadsAndWarnsOnce()
  {
    var got = new List<int>();
    var sub = _channel.subscribe<int>("a", "score", got.Add);

    _channel.publish("score", "not an int");
    _channel.publish("score", 2.5);
    Assert.Empty(got);

    _channel.publish("score", 7);
    Assert.Equal(new[] { 7 }, got);
    Assert.True(sub.isActive);
    Assert.Empty(_faults);
  }

  [Fact]
  public void TypedSubscriptionAcceptsNullForReferenceTypesOnly()
  {
    var strings = new List<string?>();
    var ints = new List<int>();
    _channel.subscribe<string>("a", "e", strings.Add);
    _channel.subscribe<int>("b", "e", ints.Add);

    _channel.publish("e", null);

    Assert.Equal(new string?[] { null }, strings);
    Assert.Empty(ints);

    _channel.publish("e", 5);
    Assert.Equal(new[] { 5 }, ints);
  }

  [Fact]
  public void SubscribeDuringDeliveryIsNotCalledForThatPublish()
  {
    var lateCalls = 0;
    _channel.subscribe("a", "go", _ => _channel.subscribe("a", "go", _ => lateCalls++));

    Assert.Equal(1, _channel.publish("go", null));
    Assert.Equal(0, lateCalls);

    Assert.Equal(2, _channel.publish("go", null));
    Assert.Equal(1, lateCalls);
  }

  [Fact]
  public void UnsubscribingAnotherNotYetCalledHandlerTakesEffectImmediately()
  {
    var secondCalls = 0;
    Subscription? second = null;
    _channel.subscribe("a", "go", _ => second!.unsubscribe());
    second = _channel.subscribe("b", "go", _ => secondCalls++);

    Assert.Equal(1, _channel.publish("go", null));
    Assert.Equal(0, secondCalls);
    Assert.False(second.isActive);
  }

  [Fact]
  public void HandlerMayUnsubscribeItself()
  {
    var calls = 0;
    Subscription? self = null;
    self = _channel.subscribe("a", "go", _ =>
    {
      calls++;
      self!.unsubscribe();
    });

    Assert.Equal(1, _channel.publish("go", null));
    Assert.Equal(0, _channel.publish("go", null));
    Assert.Equal(1, calls);
  }

  [Fact]
  public void UnsubscribeIsIdempotent()
  {
    var sub = _channel.subscribe("a", "go", _ => { });
    Assert.True(sub.isActive);

    sub.unsubscribe();
    sub.unsubscribe();

    Assert.False(sub.isActive);
    Assert.Equal(0, _channel.publish("go", null));
  }

  [Fact]
  public void ThrowingHandlerFaultsOnlyItsOwnerAndOthersStillReceive()
  {
    var received = new List<string>();
    _channel.subscribe("bad", "go", _ => throw new InvalidOperationException("boom"));
    _channel.subscribe("good", "go", _ => received.Add("good"));

    var ran = _channel.publish("go", null);

    Assert.Equal(2, ran);
    Assert.Equal(new[] { "good" }, received);
    var fault = Assert.Single(_faults);
    Assert.Equal("bad", fault.Owner);
    Assert.Equal("go", fault.Event);
    Assert.IsType<InvalidOperationException>(fault.Ex);
    Assert.DoesNotContain("good", _faulted);
  }

  [Fact]
  public void ThrowingHandlerDoesNotThrowIntoPublisher()
  {
    _channel.subscribe("bad", "go", _ => throw new InvalidOperationException("boom"));

    var ex = Record.Exception(() => _channel.publish("go", null));

    Assert.Null(ex);
  }

  [Fact]
  public void FaultedOwnerIsSkipped()
  {
    var calls = 0;
    _channel.subscribe("owner", "go", _ => calls++);
    Assert.Equal(1, _channel.publish("go", null));

    _faulted.Add("owner");

    Assert.Equal(0, _channel.publish("go", null));
    Assert.Equal(1, calls);
  }

  [Fact]
  public void NestedPublishWorksBelowTheCapAndIsRefusedPastIt()
  {
    var depthReached = 0;
    var refused = false;
    _channel.subscribe("a", "loop", _ =>
    {
      depthReached++;
      if (_channel.publish("loop", null) == 0)
      {
        refused = true;
      }
    });

    _channel.publish("loop", null);

    Assert.Equal(EventChannel.MaxNestingDepth, depthReached);
    Assert.True(refused);

    var pingPong = 0;
    _channel.subscribe("b", "once", _ =>
    {
      if (pingPong++ < 3)
      {
        _channel.publish("once", null);
      }
    });
    _channel.publish("once", null);
    Assert.Equal(4, pingPong);
  }

  [Fact]
  public void DepthResetsWhenAnExceptionEscapesPublish()
  {
    _channel.subscribe("a", "inner", _ => { });

    _faultCheckThrows = true;
    for (var i = 0; i < EventChannel.MaxNestingDepth + 2; i++)
    {
      Assert.Throws<InvalidOperationException>(() => _channel.publish("inner", null));
    }

    _faultCheckThrows = false;
    var depthReached = 0;
    _channel.subscribe("b", "loop", _ =>
    {
      depthReached++;
      _channel.publish("loop", null);
    });
    _channel.publish("loop", null);

    Assert.Equal(EventChannel.MaxNestingDepth, depthReached);
  }

  [Fact]
  public void SecondHandlerOfAFaultedOwnerIsSkippedForTheRestOfThePublish()
  {
    var secondCalls = 0;
    var otherCalls = 0;
    _channel.subscribe("owner", "go", _ => throw new InvalidOperationException("boom"));
    _channel.subscribe("owner", "go", _ => secondCalls++);
    _channel.subscribe("other", "go", _ => otherCalls++);

    var ran = _channel.publish("go", null);

    Assert.Equal(2, ran);
    Assert.Equal(0, secondCalls);
    Assert.Equal(1, otherCalls);
    Assert.Single(_faults);
  }

  [Fact]
  public void RemoveOwnerDropsOnlyThatOwnersSubscriptions()
  {
    var kept = 0;
    var dropped = 0;
    var droppedSub = _channel.subscribe("gone", "go", _ => dropped++);
    _channel.subscribe("gone", "other", _ => dropped++);
    _channel.subscribe("kept", "go", _ => kept++);

    _channel.removeOwner("gone");

    Assert.False(droppedSub.isActive);
    Assert.Equal(1, _channel.publish("go", null));
    Assert.Equal(0, _channel.publish("other", null));
    Assert.Equal(1, kept);
    Assert.Equal(0, dropped);
  }

  [Fact]
  public void ClearDropsEverySubscription()
  {
    var calls = 0;
    var sub = _channel.subscribe("a", "go", _ => calls++);
    _channel.subscribe("b", "go", _ => calls++);
    Assert.Equal(2, _channel.publish("go", null));

    _channel.clear();

    Assert.False(sub.isActive);
    Assert.Equal(0, _channel.publish("go", null));
    Assert.Equal(2, calls);
  }

  [Fact]
  public void InvalidArgumentsAreRefusedWithoutThrowing()
  {
    var calls = 0;
    var valid = _channel.subscribe("a", "go", _ => calls++);

    var nullName = _channel.subscribe("a", null!, _ => calls++);
    var emptyName = _channel.subscribe("a", "", _ => calls++);
    var nullHandler = _channel.subscribe("a", "go", (Action<object?>)null!);
    var nullTypedHandler = _channel.subscribe<int>("a", "go", null!);

    Assert.True(valid.isActive);
    Assert.False(nullName.isActive);
    Assert.False(emptyName.isActive);
    Assert.False(nullHandler.isActive);
    Assert.False(nullTypedHandler.isActive);
    Assert.Null(Record.Exception(() => nullHandler.unsubscribe()));
    Assert.Equal(0, _channel.publish("", null));
    Assert.Equal(0, _channel.publish(null!, null));
    Assert.Equal(1, _channel.publish("go", null));
    Assert.Equal(1, calls);
  }
}

// Bridge wiring: the channel's fault gate is the bridge's, and subscriptions die with their instance.
[Collection("BridgeInstances")]
public class EventChannelBridgeTests : IDisposable
{
  public class Listener : ScriptBase
  {
    public List<object?> Received { get; } = new();

    public void Listen(string eventName) => subscribe(eventName, p => Received.Add(p));

    public Subscription ListenTo(string eventName) => subscribe(eventName, p => Received.Add(p));

    public Subscription ListenTyped(string eventName) => subscribe<string>(eventName, p => Received.Add(p));

    public void ListenAndThrow(string eventName) => subscribe(eventName, _ => throw new InvalidOperationException("boom"));
  }

  public class Announcer : ScriptBase
  {
    public int Announce(string eventName, object? payload) => publish(eventName, payload);
  }

  private readonly List<(string Uuid, string ClassName)> _added = new();

  private T Add<T>(string uuid) where T : ScriptBase, new()
  {
    var instance = new T();
    Bridge.AddInstance(uuid, typeof(T).Name, instance);
    _added.Add((uuid, typeof(T).Name));
    return instance;
  }

  public void Dispose()
  {
    foreach (var (uuid, className) in _added)
    {
      Bridge.RemoveInstance(uuid, className);
    }

    Bridge.Events.clear();
  }

  [Fact]
  public void SubscriberReceivesPublishFromAnotherInstance()
  {
    var listener = Add<Listener>("evt-listener");
    var announcer = Add<Announcer>("evt-announcer");
    listener.Listen("roundEnded");

    var ran = announcer.Announce("roundEnded", "payload");

    Assert.Equal(1, ran);
    Assert.Equal(new object?[] { "payload" }, listener.Received);
  }

  [Fact]
  public void RemovedInstanceNoLongerReceives()
  {
    var listener = Add<Listener>("evt-listener");
    var announcer = Add<Announcer>("evt-announcer");
    listener.Listen("roundEnded");
    Assert.Equal(1, announcer.Announce("roundEnded", 1));

    Bridge.RemoveInstance("evt-listener", nameof(Listener));

    Assert.Equal(0, announcer.Announce("roundEnded", 2));
    Assert.Single(listener.Received);
  }

  [Fact]
  public void ThrowingSubscriberIsFaultedAndPublisherAndOthersAreNot()
  {
    var bad = Add<Listener>("evt-bad");
    var good = Add<Listener>("evt-good");
    var announcer = Add<Announcer>("evt-announcer");
    bad.ListenAndThrow("go");
    good.Listen("go");
    Assert.True(Bridge.TryFindScript("evt-bad", nameof(Listener), out _));

    var ran = announcer.Announce("go", null);

    Assert.Equal(2, ran);
    Assert.Single(good.Received);
    Assert.False(Bridge.TryFindScript("evt-bad", nameof(Listener), out _));
    Assert.True(Bridge.TryFindScript("evt-good", nameof(Listener), out _));
    Assert.True(Bridge.TryFindScript("evt-announcer", nameof(Announcer), out _));

    Assert.Equal(1, announcer.Announce("go", null));
    Assert.Equal(2, good.Received.Count);
  }

  [Fact]
  public void SubscribeFromAnInstanceThatWasNeverAddedIsRefused()
  {
    var announcer = Add<Announcer>("evt-announcer");
    var live = Add<Listener>("evt-live");
    var stray = new Listener();

    var strayHandle = stray.ListenTo("go");
    var liveHandle = live.ListenTo("go");

    Assert.False(strayHandle.isActive);
    Assert.True(liveHandle.isActive);
    Assert.Equal(1, announcer.Announce("go", 1));
    Assert.Empty(stray.Received);
    Assert.Single(live.Received);
  }

  [Fact]
  public void SubscribeAfterRemoveInstanceIsRefused()
  {
    var announcer = Add<Announcer>("evt-announcer");
    var listener = Add<Listener>("evt-listener");
    Assert.True(listener.ListenTyped("go").isActive);

    Bridge.RemoveInstance("evt-listener", nameof(Listener));
    var late = listener.ListenTo("go");

    Assert.False(late.isActive);
    Assert.Equal(0, announcer.Announce("go", "x"));
    Assert.Empty(listener.Received);
  }

  [Fact]
  public void ReplacingAnInstanceDropsTheOldOnesHandlers()
  {
    var announcer = Add<Announcer>("evt-announcer");
    var old = Add<Listener>("evt-listener");
    var oldHandle = old.ListenTo("go");
    Assert.Equal(1, announcer.Announce("go", 1));

    var replacement = new Listener();
    Bridge.AddInstance("evt-listener", nameof(Listener), replacement);
    var newHandle = replacement.ListenTo("go");

    Assert.False(oldHandle.isActive);
    Assert.True(newHandle.isActive);
    Assert.Equal(1, announcer.Announce("go", 2));
    Assert.Single(old.Received);
    Assert.Equal(new object?[] { 2 }, replacement.Received);
  }
}
