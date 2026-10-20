// Moves between its start and start + Offset (both in its parent's frame), pausing at each end with `after`, and
// can carry bodies resting on top. Carried riders must be root objects, whose world and local positions match.
using System;
using System.Collections.Generic;
using System.Numerics;
using ScriptBridge;

public class PingPongMover : ScriptBase
{
    // Local: in the parent's frame, like the position it starts from.
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
    private bool m_pausePending;
    private readonly List<string> m_riders = new List<string>();

    public override void start()
    {
        m_start = transform.getLocalPosition();
        m_current = m_start;
        m_progress = 0.0f;
        m_direction = 1.0f;
        m_paused = false;
        m_pausePending = false;
        m_riders.Clear();

        if (World.tryGetRigidBody(EntityId, out _))
        {
            Log.warn("PingPongMover: this object has a RigidBody, so physics and the script will fight over " +
                     "its position. Use it on an object without one.");
        }
    }

    public override void fixedUpdate(float dt)
    {
        // Timers tick before fixedUpdate, so scheduling on the tick after arrival makes the hold last the full
        // Pause Seconds instead of a tick less.
        if (m_pausePending)
        {
            m_pausePending = false;
            after(m_pauseSeconds, () => m_paused = false);
        }

        Vector3 displacement = Vector3.Zero;
        float length = m_offset.Length();
        if (!m_paused && length > 1e-4f && m_speed > 0.0f)
        {
            m_progress += m_direction * m_speed * dt / length;
            if (m_progress >= 1.0f || m_progress <= 0.0f)
            {
                m_progress = Math.Clamp(m_progress, 0.0f, 1.0f);
                m_direction = -m_direction;
                m_paused = m_pauseSeconds > 0.0f;
                m_pausePending = m_paused;
            }

            float shaped = m_ease ? m_progress * m_progress * (3.0f - 2.0f * m_progress) : m_progress;
            Vector3 position = m_start + m_offset * shaped;
            displacement = position - m_current;
            m_current = position;
            transform.setPosition(position);
        }

        if (m_carryRiders && displacement != Vector3.Zero && m_riders.Count > 0)
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

    // A Box collider gives the top exactly: its offset plus its size times the Y scale (the box is scaled by the
    // object's scale). Any other shape falls back to the Y scale alone, which is a half-extent.
    private float topHeight()
    {
        float height = transform.getPosition().Y;
        if (World.tryGetCollider(EntityId, out Collider collider) && collider.getShape() == ColliderShape.Box &&
            collider.tryGetBoxOffset(out Vector3 offset) && collider.tryGetBoxSize(out Vector3 size))
        {
            return height + offset.Y + size.Y * transform.getScale().Y;
        }

        return height + transform.getScale().Y;
    }

    private void carryRiders(Vector3 displacement)
    {
        float top = topHeight();
        for (int i = m_riders.Count - 1; i >= 0; i--)
        {
            if (!World.tryGetTransform(m_riders[i], out Transform rider))
            {
                m_riders.RemoveAt(i);
                continue;
            }

            Vector3 position = rider.getPosition();
            bool isRoot = Vector3.DistanceSquared(position, rider.getLocalPosition()) < 1e-6f;
            if (isRoot && position.Y >= top)
            {
                rider.setPosition(position + displacement);
            }
        }
    }
}
