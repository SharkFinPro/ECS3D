# Scene Scripts and Events

An object script (`ScriptBase`) belongs to one object. A scene script (`SceneScript`) belongs to the whole
scene, for logic that has no single owner: a score, a round timer, a spawner. Scripts can also send each
other named events. This guide covers both and ends with a small score keeper.

It assumes you have read [Your first script](your-first-script.md).

## Scene script or object script

Both derive from a shared base that gives them `start()`, `fixedUpdate(float dt)`, `variableUpdate()`,
`stop()`, the `after` and `every` timers, and events. They differ in what else they have.

| Feature | `ScriptBase` | `SceneScript` |
|---------|--------------|---------------|
| Owner | One object | The current scene |
| `transform`, `rigidBody`, `input`, `camera` | Yes | No |
| Collision callbacks | Yes | No |
| Scene-wide callbacks | No | Yes |
| Where you add it | Drag onto an object's Inspector | The Scene Scripts panel |

A scene script has no object, so to change an object it looks one up through `World`, for example
`World.findObjectByName("Door")` and then `World.tryGetTransform`.

A class is one kind or the other. A `SceneScript` class cannot be put on an object, and a `ScriptBase` class
cannot be put on a scene. If you do, the server logs one warning and leaves it unattached.

## Adding a scene script

1. Create the script with Assets, then New Script. The template derives from `ScriptBase`, so edit the file
   and change the base class to `SceneScript`. Then press Play, wait for "Reload successful." in the
   Console, and press Stop. A new script is only compiled while the scene runs, and a reload restarts
   scripts, so `onSceneLoaded` does not run on that first Play.
2. Open Window, then Scene Scripts.
3. Press Add Scene Script and pick the class. You can also drag a script asset onto the panel.
4. Each script shows its exposed fields (`[ExposeToEditor]`, as for object scripts). Up and down arrows
   reorder the list and Remove deletes an entry.

A scene holds at most one scene script of each class. The list is saved with the scene, and the panel's
changes can be undone.

## Order of execution

Scene scripts start before object scripts, run `variableUpdate` and `fixedUpdate` before them every tick in
list order, and stop after them. Reorder the list when one scene script depends on another.

## Scene-wide callbacks

Override any of these on a `SceneScript`. They are `public override void`.

| Method | When |
|--------|------|
| `onSceneLoaded()` | Once, after every script has started, when the scene runs |
| `onSceneUnloading()` | When the scene stops, before any script's `stop()` |
| `onObjectSpawned(string uuid)` | An object appeared, once per object in a spawned subtree |
| `onObjectDestroyed(string uuid)` | An object was removed |
| `onPlayerJoined(int slot)` | A player took a slot. Players already connected get it after loaded |
| `onPlayerLeft(int slot)` | A player left |

They are delivered only while the scene is not stopped. A scene script added while the scene is already
running, or created again by a script reload, misses `onSceneLoaded` and the joins that happened at start.

## Events

Scripts of either kind can publish and subscribe to named events. These are protected members, so call them
from inside your class.

- `publish(string eventName, object? payload = null)` runs every subscriber and returns how many ran.
- `subscribe<T>(string eventName, Action<T> handler)` runs the handler when a payload of type `T` arrives.
  A payload of another type is skipped with one warning. There is also an untyped
  `subscribe(string, Action<object?>)`.
- `subscribe` returns a `Subscription`. Keep it and call `unsubscribe()` to stop early.

The rules:

- Names match exactly, and an empty name is refused with a warning.
- Delivery is synchronous. The handlers have all run by the time `publish` returns.
- Subscribe from `start()`. A subscription made from the constructor, or by a script that is not attached,
  is refused with a warning.
- A subscription ends when its script stops or is removed, and all of them end on a script reload. The
  restarted scripts subscribe again in `start()`.
- Each handler runs under its own script's safety net. If one throws, only that script is marked as failed.
  The publisher and the other subscribers are not affected.
- Handlers may publish further events, but only 16 levels deep. Past that, the publish is refused with a
  warning, which stops loops between scripts.

To call into a scene script directly instead, use `getSceneScript<T>()`, which returns `null` when the scene
has none. `requireSceneScript<T>()` does the same and logs an error once while the script is missing. From
`World`, `World.tryGetSceneScript<T>(out script)` also works. A direct call is not protected by the safety
net, so a throw inside it faults the caller. Events are the safer default.

## Worked example: a score keeper

First, the scene script. Save it as `CoinScoreKeeper.cs`.

```csharp
using System.Collections.Generic;
using ScriptBridge;

public class CoinScoreKeeper : SceneScript
{
    public const string CoinCollected = "coinCollected";

    [ExposeToEditor("Target")]
    private int Target = 5;

    private readonly Dictionary<int, int> m_scores = new Dictionary<int, int>();

    public override void start()
    {
        subscribe<int>(CoinCollected, onCoinCollected);
    }

    public override void onSceneLoaded()
    {
        m_scores.Clear();
        Log.info($"Collect {Target} coins to win.");
    }

    public override void onPlayerLeft(int slot)
    {
        m_scores.Remove(slot);
    }

    private void onCoinCollected(int slot)
    {
        m_scores.TryGetValue(slot, out int score);
        score++;
        m_scores[slot] = score;

        int total = 0;
        foreach (int value in m_scores.Values)
        {
            total += value;
        }

        Log.info($"Player {slot} has {score} coin(s). Total collected: {total}.");

        if (score >= Target)
        {
            Log.info($"Player {slot} wins!");
        }
    }
}
```

Next, change the coin from the previous guide so it publishes instead of only logging. Replace
`onCollisionEnter` in `BobbingCoin.cs` with:

```csharp
    public override void onCollisionEnter(string otherUuid)
    {
        if (m_collected || !World.tryGetPlayerController(otherUuid, out PlayerController player))
        {
            return;
        }

        m_collected = true;

        if (publish(CoinScoreKeeper.CoinCollected, player.getPlayerSlot()) == 0)
        {
            Log.warn("Nobody is listening for coin pickups. Add CoinScoreKeeper in the Scene Scripts panel.");
        }

        World.destroy(EntityId);
    }
```

Add `CoinScoreKeeper` in the Scene Scripts panel. If it is new, run the compile cycle described above
first. Then press Play, and walk through coins. The Console prints a
running total each time. Because the keeper starts before any object script, it is already subscribed when
the first coin is touched. The payload is a plain `int`, so `subscribe<int>` matches it. For richer data,
define a small class and publish an instance of it, then subscribe with that class as `T`.

## Tips

- Keep event names in one place, such as a `const string` on the receiving class, so a typo is a compile
  error instead of a silent miss.
- A publish that returns 0 means no live subscriber ran. Logging it, as above, finds missing wiring fast.
