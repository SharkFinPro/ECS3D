// Scene script: gives every joining player a body, adopting a placed one or spawning the player prefab.
using System;
using System.Collections.Generic;
using System.Numerics;
using ScriptBridge;

public class PlayerSpawnManager : SceneScript
{
    [ExposeToEditor("Player Prefab")]
    private string m_playerPrefab = "9c7f0e83-4d16-4b52-a09e-27f5b3d81c6a";

    [ExposeToEditor("Spawn Point Prefix")]
    private string m_spawnPointPrefix = "SpawnPoint";

    [ExposeToEditor("Spawn Height")]
    private float m_spawnHeight = 1.0f;

    [ExposeToEditor("Respawn Below Y")]
    private float m_respawnBelowY = -20.0f;

    [ExposeToEditor("Tint Players")]
    private bool m_tintPlayers = false;

    // Slot colors, cycling for slot 4 and up: palette_red, palette_blue, palette_green, palette_orange.
    private static readonly string[] SlotTextures =
    {
        "7c8c01db-771f-4048-94c2-004b8d19e7aa",
        "66a8a6fc-d151-4567-9ce5-2e17cc675fe0",
        "c5eee5e6-9bd0-4f9b-977f-b9220a21a630",
        "ea184ad8-ca64-48cd-b69e-92bcb4649158"
    };

    private const float RespawnDelaySeconds = 2.0f;
    private const float LapHeight = 3.0f;

    private sealed class Tracked
    {
        public string Uuid = "";
        public bool Spawned;
        public Vector3 Home;
        public bool RespawnPending;
    }

    private readonly Dictionary<int, Tracked> m_players = new();
    private int m_joins;
    private bool m_warnedNoSpawnPoints;

    // Every join takes the next spawn point, an adopted player's included, so a placed player is not stood on.
    public override void onPlayerJoined(int slot)
    {
        if (m_players.TryGetValue(slot, out var known) && World.objectExists(known.Uuid))
        {
            return;
        }

        int join = m_joins++;
        string placed = StarterUtils.findPlayerBody(slot);
        if (placed.Length > 0)
        {
            Vector3 home = World.tryGetTransform(placed, out Transform transform)
                ? transform.getPosition()
                : Vector3.Zero;
            m_players[slot] = new Tracked { Uuid = placed, Spawned = false, Home = home };
            publish(StarterEvents.PlayerSpawned, new PlayerSpawnedEvent { Slot = slot, Uuid = placed });
            return;
        }

        var entry = new Tracked { Home = homeFor(join) };
        m_players[slot] = entry;
        spawnBody(slot, entry);
    }

    public override void onPlayerLeft(int slot)
    {
        if (!m_players.Remove(slot, out var entry))
        {
            return;
        }

        if (entry.Spawned && entry.Uuid.Length > 0)
        {
            World.destroy(entry.Uuid);
        }
    }

    public override void fixedUpdate(float dt)
    {
        foreach (var pair in m_players)
        {
            Tracked entry = pair.Value;
            if (entry.RespawnPending)
            {
                continue;
            }

            if (!World.objectExists(entry.Uuid))
            {
                scheduleRespawn(pair.Key, entry);
            }
            else if (World.tryGetTransform(entry.Uuid, out Transform transform) &&
                     transform.getPosition().Y < m_respawnBelowY)
            {
                returnHome(entry, transform);
            }
        }
    }

    private void scheduleRespawn(int slot, Tracked entry)
    {
        entry.RespawnPending = true;
        after(RespawnDelaySeconds, () =>
        {
            entry.RespawnPending = false;
            if (m_players.TryGetValue(slot, out var current) && ReferenceEquals(current, entry))
            {
                spawnBody(slot, entry);
            }
        });
    }

    private void returnHome(Tracked entry, Transform transform)
    {
        transform.setPosition(entry.Home);
        if (World.tryGetRigidBody(entry.Uuid, out RigidBody body))
        {
            body.setVelocity(Vector3.Zero);
            body.setAngularVelocity(Vector3.Zero);
        }
    }

    // A failed spawn drops the player so it is not retried every two seconds.
    private void spawnBody(int slot, Tracked entry)
    {
        string uuid = World.spawnPrefab(m_playerPrefab, entry.Home);
        if (uuid.Length == 0)
        {
            Log.warn($"PlayerSpawnManager: could not spawn prefab '{m_playerPrefab}' for player {slot}.");
            m_players.Remove(slot);
            return;
        }

        entry.Uuid = uuid;
        entry.Spawned = true;
        if (World.tryGetPlayerController(uuid, out PlayerController controller))
        {
            controller.setPlayerSlot(slot);
        }

        if (m_tintPlayers && World.tryGetModelRenderer(uuid, out ModelRenderer renderer))
        {
            renderer.setTexture(SlotTextures[slot % SlotTextures.Length]);
        }

        publish(StarterEvents.PlayerSpawned, new PlayerSpawnedEvent { Slot = slot, Uuid = uuid });
    }

    // Join n takes point n mod count; each full lap over the points sits another LapHeight higher.
    private Vector3 homeFor(int join)
    {
        var points = findSpawnPoints();
        if (points.Count == 0)
        {
            if (!m_warnedNoSpawnPoints)
            {
                m_warnedNoSpawnPoints = true;
                Log.warn($"PlayerSpawnManager: no objects named '{m_spawnPointPrefix}...' were found; " +
                         "spawning players at the origin.");
            }

            return new Vector3(0.0f, m_spawnHeight, 0.0f);
        }

        Vector3 point = points[join % points.Count];
        return point + new Vector3(0.0f, m_spawnHeight + LapHeight * (join / points.Count), 0.0f);
    }

    private List<Vector3> findSpawnPoints()
    {
        var named = new List<(string Name, Vector3 Position)>();
        if (m_spawnPointPrefix.Length > 0)
        {
            foreach (string uuid in World.getAllObjects())
            {
                string name = World.getObjectName(uuid);
                if (name.StartsWith(m_spawnPointPrefix, StringComparison.Ordinal) &&
                    World.tryGetTransform(uuid, out Transform transform))
                {
                    named.Add((name, transform.getPosition()));
                }
            }
        }

        named.Sort((a, b) => string.CompareOrdinal(a.Name, b.Name));
        var positions = new List<Vector3>(named.Count);
        foreach (var item in named)
        {
            positions.Add(item.Position);
        }

        return positions;
    }
}
