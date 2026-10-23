# Prefabs

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

A prefab is an `AssetRegistry` record whose **body travels inline** — one `Object::serialize()`
blob, stored dumped in `AssetRecord::body` and threaded through the same `serialize`/`loadFromJSON`/`pack`/
`unpack` contract as everything else (the shape `Script::m_fields` already uses). It is deliberately **not a
file on disk**: a model path names a *client-side GPU resource the server does not use*, but a prefab body
is *server-side gameplay data the server needs to instantiate*, and the editor and server may share no
filesystem. Prefabs (like scenes) key off a display name in `path`; `registerAsset` is first-wins for every
other type, but **re-registering an existing prefab name updates its body in place, keeping the uuid**, so
"Save as Prefab" over an existing name means *update it*. Instantiation is
`ObjectManager::instantiate(body)` — fresh uuids via `reassignUUIDs`, the shared core of `duplicateObject` —
reachable two ways: the editor's `instantiatePrefab` **`sceneEdit` op** (the one op keyed by an asset, hence
the `const AssetRegistry*` parameter on `applySceneEdit`; it takes an optional parent uuid, so dropping a
prefab onto an object in the hierarchy instantiates it as that object's child via `instantiateUnder` instead
of at the scene root), and a script's `World.spawnPrefab(uuid, position)`,
which rides the existing `objectSpawned` replication (one message carries the whole subtree). The binding
reaches the registry through **`BindingContext::setAssetRegistry`**, injected once at startup exactly like the
sim's raycast/overlap statics. **Instances are detached copies** — nothing records which prefab an object came
from, so overrides and prefab→instance propagation don't exist (deliberately deferred).
`DefaultProject` defines its `Block`/`Rigid Block`/`Sphere`/`Player`/`Capsule`/`Cylinder`/`Ramp` bodies once,
registers them as prefabs with stable uuids, and builds its scenes by instancing them.
The first scene, Welcome (`DefaultWelcome.cpp`, with its shelf rest poses in `RestPose.cpp`), is the current one; it shares the uuids and builders through `DefaultProjectParts.h`. **Editing a prefab's contents** happens in the
Inspector: `AssetInspector` deserializes the body into a `TransientObject` (editor lib) — a detached `Object`
living in a private scratch `ObjectManager` that is not wired to a scene/`SceneManager`/replication, uuids
preserved so the body is stable across edits — and draws it with a reused `ObjectInspector`. Its edits apply
locally (value edits in place; structural edits deferred past `display()` then `applySceneEdit`'d, so the
component map isn't mutated mid-iteration), and each change re-serializes the body and re-registers it under
the prefab's existing name via the `addAsset` op (updating the body in place, keeping the uuid — the same
"Save as Prefab" path), which re-snapshots. `TransientObject::markSynced` records the just-sent body so the
local-apply/snapshot echo doesn't rebuild the object mid-edit. Because that body update makes the server
re-snapshot the **whole project** (unlike a live object's cheap `editComponent` rebroadcast), the send is
**coalesced**: edits mutate the detached object live for instant local feedback but only flush to the network
once no ImGui widget is active (drag released) or the selection changes, so a whole slider drag is one
snapshot rather than one per frame.
