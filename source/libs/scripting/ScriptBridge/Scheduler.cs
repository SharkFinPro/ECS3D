using System;
using System.Collections.Generic;

namespace ScriptBridge;

public sealed class TimerHandle
{
    private readonly Scheduler? _owner;

    internal TimerHandle(Scheduler? owner, double due, double interval, bool repeating, Action? action)
    {
        _owner = owner;
        Due = due;
        Interval = interval;
        Repeating = repeating;
        Action = action;
        isActive = owner != null;
    }

    public bool isActive { get; private set; }

    public void cancel()
    {
        if (!isActive)
        {
            return;
        }

        isActive = false;
        _owner?.Remove(this);
    }

    internal double Due { get; set; }

    internal double Interval { get; }

    internal bool Repeating { get; }

    internal Action? Action { get; }

    internal void Deactivate() => isActive = false;
}

// Timers advance only through tick(dt), so they follow the simulation's fixed ticks: a paused or stopped
// scene freezes them, and a test can drive them deterministically.
internal sealed class Scheduler
{
    private readonly List<TimerHandle> _timers = new();
    private double _now;

    public TimerHandle after(float seconds, Action action)
    {
        if (!float.IsFinite(seconds) || seconds < 0f)
        {
            return Refuse(nameof(after), seconds);
        }

        return Add(new TimerHandle(this, _now + seconds, 0.0, false, action));
    }

    public TimerHandle every(float seconds, Action action)
    {
        if (!float.IsFinite(seconds) || seconds <= 0f)
        {
            return Refuse(nameof(every), seconds);
        }

        return Add(new TimerHandle(this, _now + seconds, seconds, true, action));
    }

    public void tick(float dt)
    {
        _now += dt;

        // A snapshot, so a callback may schedule or cancel timers; one scheduled mid-tick is not in it.
        foreach (var timer in _timers.ToArray())
        {
            if (!timer.isActive || timer.Due > _now)
            {
                continue;
            }

            // State is settled before the callback runs, so a throwing callback is not re-fired.
            if (timer.Repeating)
            {
                var missed = Math.Floor((_now - timer.Due) / timer.Interval) + 1.0;
                timer.Due += missed * timer.Interval;
            }
            else
            {
                timer.cancel();
            }

            timer.Action!();
        }
    }

    internal void Remove(TimerHandle timer) => _timers.Remove(timer);

    private TimerHandle Add(TimerHandle timer)
    {
        _timers.Add(timer);
        return timer;
    }

    private static TimerHandle Refuse(string method, float seconds)
    {
        Log.warn($"{method}: invalid duration {seconds}; no timer scheduled");
        return new TimerHandle(null, 0.0, 0.0, false, null);
    }
}
