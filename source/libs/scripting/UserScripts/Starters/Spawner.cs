// Timers, prefab spawning and destroying: drops a prefab at intervals and keeps at most Max Alive of them.
using System;
using System.Collections.Generic;
using System.Numerics;
using ScriptBridge;

public class Spawner : ScriptBase
{
    // The Sphere prefab of the default project.
    [ExposeToEditor("Prefab")]
    private string m_prefab = "6d2b4f19-08ac-4d73-9e52-b83a1c6f0e47";

    [ExposeToEditor("Interval")]
    private float m_interval = 1.0f;

    [ExposeToEditor("Max Alive")]
    private int m_maxAlive = 10;

    // Radius of the horizontal disc the spawn point is picked from.
    [ExposeToEditor("Offset Radius")]
    private float m_offsetRadius = 2.0f;

    [ExposeToEditor("Height Offset")]
    private float m_heightOffset = 0.0f;

    // Units per second, given to a spawned object that has a RigidBody.
    [ExposeToEditor("Initial Velocity")]
    private Vector3 m_initialVelocity = Vector3.Zero;

    [ExposeToEditor("Spawn On Start")]
    private bool m_spawnOnStart = true;

    // 0 picks a different sequence each run; any other value repeats the same offsets.
    [ExposeToEditor("Seed")]
    private int m_seed = 0;

    // The server's fixed step, until the first fixedUpdate reports the real one.
    private const float DefaultDt = 1.0f / 50.0f;

    private float m_dt = DefaultDt;
    private Random m_random = new Random();
    private bool m_warnedBadPrefab = false;
    private readonly List<string> m_spawned = new();

    public override void start()
    {
        m_random = m_seed == 0 ? new Random() : new Random(m_seed);
        if (m_spawnOnStart)
        {
            spawnOne();
        }

        every(m_interval, spawnOne);
    }

    public override void fixedUpdate(float dt)
    {
        if (dt > 0.0f)
        {
            m_dt = dt;
        }
    }

    private void spawnOne()
    {
        // Two draws every time, so a seed gives the same sequence whatever the radius.
        float angle = 2.0f * MathF.PI * (float)m_random.NextDouble();
        float distance = m_offsetRadius * MathF.Sqrt((float)m_random.NextDouble());

        m_spawned.RemoveAll(id => !World.objectExists(id));
        int cap = Math.Max(m_maxAlive, 1);
        while (m_spawned.Count >= cap)
        {
            World.destroy(m_spawned[0]);
            m_spawned.RemoveAt(0);
        }

        Vector3 position = transform.getPosition() +
            new Vector3(distance * MathF.Cos(angle), m_heightOffset, distance * MathF.Sin(angle));
        string uuid = World.spawnPrefab(m_prefab, position);
        if (uuid.Length == 0)
        {
            if (!m_warnedBadPrefab)
            {
                m_warnedBadPrefab = true;
                Log.warn($"Spawner: prefab '{m_prefab}' could not be spawned; check the Prefab uuid.");
            }

            return;
        }

        m_warnedBadPrefab = false;
        m_spawned.Add(uuid);
        if (m_initialVelocity != Vector3.Zero && World.tryGetRigidBody(uuid, out RigidBody body))
        {
            body.setVelocity(StarterUtils.perTick(m_initialVelocity.X, m_dt),
                StarterUtils.perTick(m_initialVelocity.Y, m_dt), StarterUtils.perTick(m_initialVelocity.Z, m_dt));
        }
    }
}
