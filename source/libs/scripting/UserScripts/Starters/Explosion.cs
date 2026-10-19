// Demonstrates sphere overlap queries and one-shot velocity changes: a key press or a timer blows bodies outward.
using System;
using System.Numerics;
using ScriptBridge;

public class Explosion : ScriptBase
{
    [ExposeToEditor("Key")]
    private string m_key = "E";

    [ExposeToEditor("Radius")]
    private float m_radius = 7.0f;

    // Units per second at the center; it falls off linearly to zero at the edge.
    [ExposeToEditor("Strength")]
    private float m_strength = 18.0f;

    [ExposeToEditor("Upward Bias")]
    private float m_upwardBias = 0.3f;

    [ExposeToEditor("Cooldown")]
    private float m_cooldown = 1.5f;

    // Seconds between automatic blasts; 0 turns the timer off.
    [ExposeToEditor("Fire Every")]
    private float m_fireEvery = 0.0f;

    private readonly KeyBinding m_keyBinding = new KeyBinding("Explosion Key");

    // InputUtils has no edge detection, so the previous state is tracked here.
    private bool m_wasDown = false;

    private float m_cooldownLeft = 0.0f;

    private float m_sinceFire = 0.0f;

    public override void fixedUpdate(float dt)
    {
        m_cooldownLeft = MathF.Max(0.0f, m_cooldownLeft - dt);

        Key? key = m_keyBinding.refresh(m_key);
        bool down = key != null && InputUtils.keyIsPressed(key.Value);
        bool pressed = down && !m_wasDown;
        m_wasDown = down;

        bool timerDue = false;
        if (m_fireEvery > 0.0f)
        {
            m_sinceFire += dt;
            timerDue = m_sinceFire >= m_fireEvery;
        }

        if ((pressed || timerDue) && m_cooldownLeft <= 0.0f)
        {
            detonate(dt);
            m_cooldownLeft = m_cooldown;
            m_sinceFire = 0.0f;
        }
    }

    private void detonate(float dt)
    {
        if (m_radius <= 0.0f)
        {
            return;
        }

        Vector3 center = transform.getPosition();
        int hit = 0;

        foreach (string other in overlapSphere(center, m_radius))
        {
            if (other == EntityId ||
                !World.tryGetRigidBody(other, out RigidBody body) ||
                !World.tryGetTransform(other, out Transform otherTransform))
            {
                continue;
            }

            Vector3 position = otherTransform.getPosition();
            ++hit;

            Vector3 offset = position - center;
            float distance = offset.Length();
            float magnitude = m_strength * MathF.Max(0.0f, 1.0f - distance / m_radius);
            if (magnitude <= 0.0f)
            {
                continue;
            }

            body.applyForce(blastDirection(offset, distance) * StarterUtils.perTick(magnitude, dt), position,
                ForceMode.VelocityChange);
        }

        Log.info($"Explosion hit {hit} bodies");
    }

    private Vector3 blastDirection(Vector3 offset, float distance)
    {
        Vector3 away = distance < 1e-5f ? Vector3.UnitY : offset / distance;
        Vector3 tilted = away + Vector3.UnitY * m_upwardBias;

        return tilted.LengthSquared() < 1e-8f ? away : Vector3.Normalize(tilted);
    }
}
