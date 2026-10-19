// Collision enter and a velocity change: sets the speed of whatever lands on it along Direction to Strength.
// The colliding object itself must carry the RigidBody; a child collider of a compound body is not bounced.
using System.Collections.Generic;
using System.Numerics;
using ScriptBridge;

public class BouncePad : ScriptBase
{
    // Units per second along Direction; replaces the body's own speed along it, so every bounce is as high.
    [ExposeToEditor("Strength")]
    private float m_strength = 12.0f;

    [ExposeToEditor("Direction")]
    private Vector3 m_direction = new Vector3(0.0f, 1.0f, 0.0f);

    // Seconds of fixed time before the same body can be bounced again.
    [ExposeToEditor("Cooldown")]
    private float m_cooldown = 0.2f;

    // The server's fixed step, until the first fixedUpdate reports the real one.
    private const float DefaultDt = 1.0f / 50.0f;

    private float m_dt = DefaultDt;
    private float m_time = 0.0f;
    private readonly Dictionary<string, float> m_readyAt = new();

    public override void fixedUpdate(float dt)
    {
        if (dt > 0.0f)
        {
            m_dt = dt;
        }

        m_time += dt;
    }

    public override void onCollisionEnter(string otherUuid)
    {
        if (m_direction.LengthSquared() < 1e-12f || !World.tryGetRigidBody(otherUuid, out RigidBody body))
        {
            return;
        }

        if (m_readyAt.TryGetValue(otherUuid, out float readyAt) && m_time < readyAt)
        {
            return;
        }

        pruneExpired();
        m_readyAt[otherUuid] = m_time + m_cooldown;

        Vector3 direction = Vector3.Normalize(m_direction);
        Vector3 velocity = body.getVelocity();
        float along = Vector3.Dot(velocity, direction);
        body.setVelocity(velocity - along * direction + StarterUtils.perTick(m_strength, m_dt) * direction);
    }

    private void pruneExpired()
    {
        List<string>? expired = null;
        foreach (KeyValuePair<string, float> entry in m_readyAt)
        {
            if (entry.Value <= m_time)
            {
                (expired ??= new List<string>()).Add(entry.Key);
            }
        }

        if (expired == null)
        {
            return;
        }

        foreach (string uuid in expired)
        {
            m_readyAt.Remove(uuid);
        }
    }
}
