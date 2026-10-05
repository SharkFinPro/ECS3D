using System;
using System.Collections.Generic;

namespace ScriptBridge;

// Handle for one subscription. Safe to keep and to unsubscribe from more than once, including from
// inside the handler it belongs to.
public sealed class Subscription
{
    private readonly EventChannel? _channel;

    internal Subscription(EventChannel? channel, string ownerKey, string eventName, Action<object?>? handler)
    {
        _channel = channel;
        OwnerKey = ownerKey;
        EventName = eventName;
        Handler = handler;
        IsActive = channel != null;
    }

    internal string OwnerKey { get; }
    internal string EventName { get; }
    internal Action<object?>? Handler { get; }
    internal bool IsActive { get; set; }

    public bool isActive => IsActive;

    public void unsubscribe() => _channel?.Remove(this);
}

// Synchronous publish/subscribe between script instances, by exact event name. Each handler runs under
// its owner's fault gate (supplied by the bridge), so a throwing subscriber faults only itself and never
// the publisher or the other subscribers. Not thread-safe: scripts run on the tick thread.
internal sealed class EventChannel
{
    internal const int MaxNestingDepth = 16;

    private readonly Func<string, bool> _isFaulted;
    private readonly Action<string, string, Exception> _onFault;
    private readonly List<Subscription> _subscriptions = new();
    private int _depth;

    internal EventChannel(Func<string, bool> isFaulted, Action<string, string, Exception> onFault)
    {
        _isFaulted = isFaulted;
        _onFault = onFault;
    }

    // A throwing log fallback must not fault a subscriber or the caller.
    private static void SafeWarn(string message)
    {
        try
        {
            Log.warn(message);
        }
        catch
        {
        }
    }

    internal Subscription subscribe(string ownerKey, string eventName, Action<object?> handler)
    {
        if (string.IsNullOrEmpty(eventName) || handler == null)
        {
            SafeWarn("Event subscription refused: the event name must not be empty and the handler must not be null.");
            return new Subscription(null, ownerKey, eventName ?? "", null);
        }

        var subscription = new Subscription(this, ownerKey, eventName, handler);
        _subscriptions.Add(subscription);
        return subscription;
    }

    internal Subscription subscribe<T>(string ownerKey, string eventName, Action<T> handler)
    {
        if (handler == null)
        {
            return subscribe(ownerKey, eventName, (Action<object?>)null!);
        }

        var warned = false;
        return subscribe(ownerKey, eventName, payload =>
        {
            if (payload is T typed)
            {
                handler(typed);
            }
            else if (payload == null && default(T) == null)
            {
                handler(default!);
            }
            else if (!warned)
            {
                warned = true;
                SafeWarn($"Event '{eventName}' was published with a payload of type " +
                         $"{payload?.GetType().Name ?? "null"}; a subscriber expecting {typeof(T).Name} skipped it.");
            }
        });
    }

    // Returns how many handlers were called, including any that threw.
    internal int publish(string eventName, object? payload)
    {
        if (string.IsNullOrEmpty(eventName))
        {
            SafeWarn("Event publish refused: the event name must not be empty.");
            return 0;
        }

        if (_depth >= MaxNestingDepth)
        {
            SafeWarn($"Event '{eventName}' was not published: handlers published events {MaxNestingDepth} " +
                     "levels deep, which looks like a loop between scripts.");
            return 0;
        }

        var targets = new List<Subscription>();
        foreach (var subscription in _subscriptions)
        {
            if (subscription.EventName == eventName)
            {
                targets.Add(subscription);
            }
        }

        var ran = 0;
        _depth++;
        try
        {
            foreach (var subscription in targets)
            {
                if (!subscription.IsActive || _isFaulted(subscription.OwnerKey))
                {
                    continue;
                }

                ran++;
                try
                {
                    subscription.Handler!(payload);
                }
                catch (Exception ex)
                {
                    try
                    {
                        _onFault(subscription.OwnerKey, eventName, ex);
                    }
                    catch
                    {
                    }
                }
            }
        }
        finally
        {
            _depth--;
        }

        return ran;
    }

    internal void Remove(Subscription subscription)
    {
        subscription.IsActive = false;
        _subscriptions.Remove(subscription);
    }

    internal void removeOwner(string ownerKey)
    {
        _subscriptions.RemoveAll(subscription =>
        {
            if (subscription.OwnerKey != ownerKey)
            {
                return false;
            }

            subscription.IsActive = false;
            return true;
        });
    }

    internal void clear()
    {
        foreach (var subscription in _subscriptions)
        {
            subscription.IsActive = false;
        }

        _subscriptions.Clear();
    }
}
