# Your First Script

Gameplay in ECS3D is written in C#. A script is a class that derives from `ScriptBase` and is attached to an
object. The server runs it, so it affects every connected view. This guide builds a pickup that bobs up and
down and reports when a player touches it.

## Where scripts live

The server compiles every `.cs` file under `scripts/UserScripts` next to its executable, including
subfolders, into one assembly. In a source build that folder is `bin/scripts/UserScripts`. The repository's
own copy is `source/libs/scripting/UserScripts`, and each build copies it over the `bin` folder.

- To start a script, use Assets, then New Script in the editor (or the plus button in the Assets panel). It
  writes `<Name>.cs` into `scripts/UserScripts` with empty lifecycle methods and registers it as an asset.
- A script is found by its simple class name, so two classes with the same name anywhere in the folder are
  a compile error. Put each class in a file and give it a unique name.
- The class must be `public`, not `abstract`, and derive from `ScriptBase`.
- New Script writes into the editor's own `scripts/UserScripts`. That is the server's folder when the editor
  started its own server (the default). With `--host` to a separate server, place the file in that server's
  scripts folder instead.
- A new script is only compiled while the scene is running. After saving one, press Play, wait for
  "Reload successful." in the Console, then press Stop. Its fields now appear in the Inspector, and the next
  Play runs it from the start. On that first run the class logs "could not be instantiated" until the reload.
- A new script you want to keep belongs in `source/libs/scripting/UserScripts` as well. A build copies that
  folder over `bin`, which overwrites a file of the same name there.

## The lifecycle

Override any of these on your class. All are `public override`.

| Method | When it runs |
|--------|--------------|
| `start()` | Once, when the scene starts running |
| `fixedUpdate(float dt)` | Every fixed simulation tick. `dt` is the tick length in seconds |
| `variableUpdate()` | Every tick, before `fixedUpdate`. Input is read here |
| `stop()` | When the scene stops |
| `onCollisionEnter(string otherUuid)` | The tick a contact begins |
| `onCollisionStay(string otherUuid)` | Every tick a contact continues |
| `onCollisionExit(string otherUuid)` | The tick a contact ends |

`otherUuid` is the unique identifier of the other object. Scripts run only while the scene is Running. A
collider marked as a trigger still raises these events but does not push anything.

## What a script can use

Every `ScriptBase` has these members:

- `transform`: `getPosition()`, `getRotation()`, `getScale()` (world values), `getLocalPosition()`,
  `setPosition(Vector3)`, `setRotation(x, y, z)`, `move(x, y, z)`. The getters above return world
  values, while `setPosition` and `move` act on the local position.
- `rigidBody`: `getVelocity()`, `setVelocity(...)`, `applyForce(force, position, ForceMode)`, `isFalling()`
  and more.
- `input`: this script's own player's keys and mouse, resolved through a Player Controller on the same
  object.
- `camera`: this object's Camera component.
- `EntityId`: the object's uuid, a string.

Vectors are `System.Numerics.Vector3`. To reach other objects, use `World`:

- `World.findObjectByName(name)` returns a uuid, or an empty string.
- `World.tryGetTransform(uuid, out transform)` returns false when the object is gone. `tryGetRigidBody`,
  `tryGetCollider` and `tryGetPlayerController` work the same way.
- `World.spawnPrefab(prefabUuid, position)` and `World.destroy(uuid)` change the scene. A destroy takes
  effect at the end of the tick. A spawn returns an empty string while the scene is stopped, for example
  from a constructor before you press Play.

Log with `Log.trace`, `Log.debug`, `Log.info`, `Log.warn` and `Log.error`. The output appears in the
editor's Console with a "[server] " prefix.

Timers are built in. `after(seconds, action)` runs once and `every(seconds, action)` repeats. Both return a
handle with `cancel()`. They count fixed ticks, so they freeze while the scene is paused.

## Exposing fields to the editor

Mark a field with `[ExposeToEditor("Label")]` and it becomes editable in the Inspector. The field can be
private. Supported types are `float`, `int`, `bool`, `string` and `Vector3`. Any other type is ignored.

Put `[PrefabReference]`, `[ModelReference]` or `[TextureReference]` on a `string` field as well and the
Inspector draws a slot for that kind of asset instead of a text box. Drag a prefab, model or texture from the
Assets panel onto it. Right-click the slot and choose Clear to empty it. The field still holds the asset's
uuid as text.

The Inspector widget shows the field's label. In these guides the field name matches its label. Values you
set in the Inspector are saved with the scene and are kept when scripts reload. Other fields reset.

## A worked example

Save this as `BobbingCoin.cs` in `scripts/UserScripts`. It moves its object up and down, and when a player
touches it, logs which player and removes the coin.

```csharp
using System;
using System.Numerics;
using ScriptBridge;

public class BobbingCoin : ScriptBase
{
    [ExposeToEditor("Height")]
    private float Height = 0.5f;

    [ExposeToEditor("Speed")]
    private float Speed = 2.0f;

    private Vector3 m_home;
    private float m_time;
    private bool m_collected;

    public override void start()
    {
        m_home = transform.getLocalPosition();
        m_time = 0.0f;
        m_collected = false;
        Log.info("Coin ready.");
    }

    public override void fixedUpdate(float dt)
    {
        m_time += dt;
        float offset = MathF.Sin(m_time * Speed) * Height;
        transform.setPosition(new Vector3(m_home.X, m_home.Y + offset, m_home.Z));
    }

    public override void onCollisionEnter(string otherUuid)
    {
        if (m_collected || !World.tryGetPlayerController(otherUuid, out PlayerController player))
        {
            return;
        }

        m_collected = true;
        Log.info($"Player {player.getPlayerSlot()} touched the coin.");
        World.destroy(EntityId);
    }
}
```

To try it:

1. Add a Box Collider to the coin object and tick Is Trigger, so players pass through it. Give the
   coin no Rigid Body, because physics would fight the script for its position.
2. Make sure the player object has a collider (any kind), a Rigid Body and a Player Controller. Contact
   events are produced when both objects have colliders and at least one has a Rigid Body.
3. Drag `BobbingCoin` from the Assets panel onto the coin's Inspector. Once the script has been compiled
   (see Where scripts live), Height and Speed appear under its Scripts section.
4. Press Play and walk the player into the coin. Watch the Console for "Player 0 touched the coin."

## Hot reload

While the scene is Running, the server checks the script folder about twice a second. When any file changes
it recompiles everything. Live field values are saved first. A successful reload calls `stop()` on every
live script and then `start()` on fresh instances. Only `[ExposeToEditor]` fields keep their values.
Other fields reset. `BobbingCoin` re-centers on wherever it is at the moment of a reload.
In the Console you see "Change detected - reloading scripts..." then "Reload successful."

Nothing is checked while the scene is Stopped or Paused, so edit a script, press Play, and it compiles.

## When the code does not compile

The Console shows "Compilation failed:" followed by each error with its file and line, then "The previous
scripts keep running." They run unchanged. The server does not retry the same broken files, and tries
again the next time a file changes. A script whose class cannot be created logs one warning and is left
unattached, with its saved field values kept.

If a running script throws an exception, the server logs it and stops calling that script. Other scripts
keep running. Fix the code and save it, and the next reload gives it a fresh start.

## Next

Scripts can also belong to a whole scene and talk to each other with events. See
[Scene scripts and events](scene-scripts-and-events.md).
