using System;
using System.Collections.Generic;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

public class SchedulerTests
{
  private readonly Scheduler _scheduler = new();

  [Fact]
  public void AfterFiresOnceOnTheFirstTickAtOrPastTheDueTime()
  {
    var calls = 0;
    var handle = _scheduler.after(1f, () => calls++);

    _scheduler.tick(0.5f);
    Assert.Equal(0, calls);
    Assert.True(handle.isActive);

    _scheduler.tick(0.5f);
    Assert.Equal(1, calls);
    Assert.False(handle.isActive);

    _scheduler.tick(5f);
    Assert.Equal(1, calls);
  }

  [Fact]
  public void AfterZeroFiresOnTheNextTick()
  {
    var calls = 0;
    _scheduler.after(0f, () => calls++);
    Assert.Equal(0, calls);

    _scheduler.tick(0.25f);
    Assert.Equal(1, calls);
  }

  [Fact]
  public void EveryFiresOnEachIntervalWithoutDriftAcrossUnevenDt()
  {
    var fireTimes = new List<double>();
    var elapsed = 0.0;
    _scheduler.every(1f, () => fireTimes.Add(elapsed));

    foreach (var dt in new[] { 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f })
    {
      elapsed += dt;
      _scheduler.tick(dt);
    }

    // Due at 1, 2, 3, 4, 5, 6 seen on the ticks ending at 1.5, 2.25, 3.0, 4.5, 5.25, 6.0.
    Assert.Equal(new[] { 1.5, 2.25, 3.0, 4.5, 5.25, 6.0 }, fireTimes);
  }

  [Fact]
  public void EveryShorterThanATickFiresOncePerTickWithoutCatchUp()
  {
    var calls = 0;
    _scheduler.every(0.1f, () => calls++);

    _scheduler.tick(1f);
    Assert.Equal(1, calls);

    _scheduler.tick(1f);
    Assert.Equal(2, calls);
  }
  [Fact]
  public void CancelBeforeDueStopsTheTimer()
  {
    var calls = 0;
    var kept = _scheduler.after(1f, () => calls += 10);
    var cancelled = _scheduler.after(1f, () => calls++);

    cancelled.cancel();
    Assert.False(cancelled.isActive);

    _scheduler.tick(1f);
    Assert.Equal(10, calls);
    Assert.False(kept.isActive);
  }

  [Fact]
  public void CancellingARepeatingTimerFromItsOwnCallbackStopsIt()
  {
    var calls = 0;
    TimerHandle? handle = null;
    handle = _scheduler.every(1f, () =>
    {
      calls++;
      handle!.cancel();
    });

    _scheduler.tick(1f);
    _scheduler.tick(1f);

    Assert.Equal(1, calls);
    Assert.False(handle.isActive);
  }

  [Fact]
  public void CancellingAnotherDueTimerFromACallbackTakesEffectImmediately()
  {
    var firstCalls = 0;
    var secondCalls = 0;
    TimerHandle? second = null;
    _scheduler.after(1f, () =>
    {
      firstCalls++;
      second!.cancel();
    });
    second = _scheduler.after(1f, () => secondCalls++);

    _scheduler.tick(1f);

    Assert.Equal(1, firstCalls);
    Assert.Equal(0, secondCalls);
  }

  [Fact]
  public void ATimerScheduledInACallbackFiresNoEarlierThanTheNextTick()
  {
    var inner = 0;
    _scheduler.after(1f, () => _scheduler.after(0f, () => inner++));

    _scheduler.tick(1f);
    Assert.Equal(0, inner);

    _scheduler.tick(0.25f);
    Assert.Equal(1, inner);
  }

  [Theory]
  [InlineData(float.NaN)]
  [InlineData(float.PositiveInfinity)]
  [InlineData(float.NegativeInfinity)]
  [InlineData(-1f)]
  public void InvalidDurationsReturnAnInactiveHandleThatNeverFires(float seconds)
  {
    var calls = 0;
    var afterHandle = _scheduler.after(seconds, () => calls++);
    var everyHandle = _scheduler.every(seconds, () => calls++);

    var control = _scheduler.after(0f, () => calls += 100);

    _scheduler.tick(10f);

    Assert.False(afterHandle.isActive);
    Assert.False(everyHandle.isActive);
    Assert.Equal(100, calls);
    Assert.False(control.isActive);
  }

  [Fact]
  public void EveryRefusesZeroButAfterAcceptsIt()
  {
    var calls = 0;
    var everyHandle = _scheduler.every(0f, () => calls++);
    var afterHandle = _scheduler.after(0f, () => calls += 10);

    _scheduler.tick(1f);

    Assert.False(everyHandle.isActive);
    Assert.Equal(10, calls);
    Assert.False(afterHandle.isActive);
  }

  [Fact]
  public void ACallbackThatThrowsLeavesTheSchedulerConsistent()
  {
    var oneShotCalls = 0;
    var repeatCalls = 0;
    var laterCalls = 0;
    _scheduler.after(1f, () =>
    {
      oneShotCalls++;
      throw new InvalidOperationException("boom");
    });
    _scheduler.every(1f, () => repeatCalls++);
    _scheduler.after(1f, () => laterCalls++);

    Assert.Throws<InvalidOperationException>(() => _scheduler.tick(1f));
    Assert.Equal(1, oneShotCalls);

    _scheduler.tick(1f);

    Assert.Equal(1, oneShotCalls);
    Assert.Equal(1, repeatCalls);
    Assert.Equal(1, laterCalls);
  }
}

[Collection("BridgeInstances")]
public class SchedulerBridgeTests : IDisposable
{
  private sealed class Timed : ScriptBase
  {
    public int TimerCalls { get; private set; }
    public int FixedCalls { get; private set; }
    public bool Throw { get; set; }

    public void ScheduleAfter(float seconds) => after(seconds, () =>
    {
      TimerCalls++;
      if (Throw)
      {
        throw new InvalidOperationException("boom");
      }
    });

    public void ScheduleEvery(float seconds) => every(seconds, () => TimerCalls++);

    public override void fixedUpdate(float dt) => FixedCalls++;
  }

  private readonly string _uuid = Guid.NewGuid().ToString();

  public void Dispose() => Bridge.RemoveInstance(_uuid, nameof(Timed));

  // fixedUpdate itself is an [UnmanagedCallersOnly] entry point and cannot be called from C#; it
  // delegates to RunFixedUpdate, which is what runs here.
  [Fact]
  public void FixedUpdateTicksTheInstancesTimersBeforeItsOwnFixedUpdate()
  {
    var script = new Timed();
    Bridge.AddInstance(_uuid, nameof(Timed), script);
    script.ScheduleEvery(1f);

    Bridge.RunFixedUpdate(_uuid, nameof(Timed), 0.5f);
    Assert.Equal(0, script.TimerCalls);
    Assert.Equal(1, script.FixedCalls);

    Bridge.RunFixedUpdate(_uuid, nameof(Timed), 0.5f);
    Assert.Equal(1, script.TimerCalls);
    Assert.Equal(2, script.FixedCalls);
  }

  [Fact]
  public void AThrowingTimerFaultsTheScriptAndStopsItsLaterTicks()
  {
    var script = new Timed { Throw = true };
    Bridge.AddInstance(_uuid, nameof(Timed), script);
    script.ScheduleAfter(0f);
    script.ScheduleEvery(1f);

    Bridge.RunFixedUpdate(_uuid, nameof(Timed), 1f);
    Assert.Equal(1, script.TimerCalls);
    Assert.Equal(0, script.FixedCalls);

    Bridge.RunFixedUpdate(_uuid, nameof(Timed), 1f);
    Assert.Equal(1, script.TimerCalls);
    Assert.Equal(0, script.FixedCalls);
  }
}
