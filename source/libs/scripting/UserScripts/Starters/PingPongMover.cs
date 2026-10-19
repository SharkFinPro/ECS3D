// Moves between its start and start + Offset, pausing at each end with `after`; can carry bodies resting on top.
using System;
using System.Collections.Generic;
using System.Numerics;
using ScriptBridge;

public class PingPongMover : ScriptBase
{
    [ExposeToEditor("Offset")]
    private Vector3 m_offset = new Vector3(0.0f, 4.0f, 0.0f);

    // Units per second along the offset.
    [ExposeToEditor("Speed")]
    private float m_speed = 2.0f;

    [ExposeToEditor("Pause Seconds")]
    private float m_pauseSeconds = 1.0f;

    // Smoothstep on each leg's progress; the leg takes the same time either way.
    [ExposeToEditor("Ease")]
    private bool m_ease = true;

    // Moves the position of bodies resting on top by the platform's own displacement. Turn off for a platform
    // that should shove players instead.
    [ExposeToEditor("Carry Riders")]
    private bool m_carryRiders = true;

    private Vector3 m_start = Vector3.Zero;
    private Vector3 m_current = Vector3.Zero;
    private float m_progress;
    private float m_direction = 1.0f;
    private bool m_paused;
    private readonly List<string> m_riders = new List<string>();

    public override void start()
    {
        m_start = transform.getPosition();
        m_current = m_start;
        m_progress = 0.0f;
        m_direction = 1.0f;
        m_paused = false;
        m_riders.Clear();

        if (World.tryGetRigidBody(EntityId, out _))
        {
            Log.warn("PingPongMover: this object has a RigidBody, so physics and the script will fight over " +
                     "its position. Use it on an object without one.");
        }
    }

    public override void fixedUpdate(float dt)
    {
        Vector3 displacement = Vector3.Zero;
        float length = m_offset.Length();
        if (!m_paused && length > 1e-4f && m_speed > 0.0f)
        {
            m_progress += m_direction * m_speed * dt / length;
            if (m_progress >= 1.0f || m_progress <= 0.0f)
            {
                m_progress = Math.Clamp(m_progress, 0.0f, 1.0f);
                m_direction = -m_direction;
                pauseAtEnd();
            }

            float shaped = m_ease ? m_progress * m_progress * (3.0f - 2.0f * m_progress) : m_progress;
            Vector3 position = m_start + m_offset * shaped;
            displacement = position - m_current;
            m_current = position;
            transform.setPosition(position);
        }

        if (m_carryRiders && displacement != Vector3.Zero)
        {
            carryRiders(displacement);
        }
    }

    public override void onCollisionEnter(string otherUuid)
    {
        if (!m_riders.Contains(otherUuid) && World.tryGetRigidBody(otherUuid, out _))
        {
            m_riders.Add(otherUuid);
        }
    }

    public override void onCollisionExit(string otherUuid)
    {
        m_riders.Remove(otherUuid);
    }

    private void pauseAtEnd()
    {
        if (m_pauseSeconds <= 0.0f)
        {
            return;
        }

        m_paused = true;
        after(m_pauseSeconds, () => m_paused = false);
    }

    // The top is the platform's height plus its half-extent, which is its Y scale (a scale is a half-extent).
    private void carryRiders(Vector3 displacement)
    {
        float top = m_current.Y + transform.getScale().Y;
        for (int i = m_riders.Count - 1; i >= 0; i--)
        {
            if (!World.tryGetTransform(m_riders[i], out Transform rider))
            {
                m_riders.RemoveAt(i);
                continue;
            }

            Vector3 position = rider.getPosition();
            if (position.Y >= top)
            {
                rider.setPosition(position + displacement);
            }
        }
    }
}
