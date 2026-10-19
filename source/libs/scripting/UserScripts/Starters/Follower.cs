// Moves toward a named object or a player body and stops short of it: steering with VelocityChange.
using System;
using System.Numerics;
using ScriptBridge;

public class Follower : ScriptBase
{
    [ExposeToEditor("Target")]
    private string m_target = "";

    // 0 or more targets that player's body and wins over Target.
    [ExposeToEditor("Target Player Slot")]
    private int m_targetPlayerSlot = -1;

    // Units per second.
    [ExposeToEditor("Speed")]
    private float m_speed = 4.0f;

    [ExposeToEditor("Stop Distance")]
    private float m_stopDistance = 2.0f;

    // Measures and moves on the ground plane only. On a body, the vertical velocity is then left to physics.
    [ExposeToEditor("Ignore Height")]
    private bool m_ignoreHeight = true;

    // The share of the gap between the body's velocity and the one it steers for that it closes each tick, as
    // PlayerScript does, so a push from another body fades instead of being undone at once.
    private const float Grip = 0.1f;
    private const float SlotRetrySeconds = 1.0f;

    private readonly NamedObject m_targetObject = new NamedObject("Follower");
    private string m_slotUuid = "";
    private int m_resolvedSlot = -1;
    private float m_sinceSlotLookup;
    private bool m_slotLooked;
    private bool m_slotWarned;

    public override void fixedUpdate(float dt)
    {
        if (!tryResolveTarget(dt, out string targetUuid) || !World.tryGetTransform(targetUuid, out Transform target))
        {
            return;
        }

        Vector3 position = transform.getPosition();
        Vector3 delta = target.getPosition() - position;
        if (m_ignoreHeight)
        {
            delta.Y = 0.0f;
        }

        float distance = delta.Length();
        bool arrived = distance <= m_stopDistance || distance < 1e-4f;
        Vector3 heading = arrived ? Vector3.Zero : delta / distance;

        if (World.tryGetRigidBody(EntityId, out _))
        {
            steer(heading, position, dt);
            return;
        }

        if (!arrived)
        {
            Vector3 step = heading * Math.Min(m_speed * dt, distance - m_stopDistance);
            transform.move(step.X, step.Y, step.Z);
        }
    }

    private void steer(Vector3 heading, Vector3 position, float dt)
    {
        Vector3 velocity = rigidBody.getVelocity();
        if (m_ignoreHeight)
        {
            velocity.Y = 0.0f;
        }

        Vector3 change = (heading * (m_speed * dt) - velocity) * Grip;
        if (change.LengthSquared() > 0.0f)
        {
            rigidBody.applyForce(change, position, ForceMode.VelocityChange);
        }
    }

    private bool tryResolveTarget(float dt, out string uuid)
    {
        if (m_targetPlayerSlot < 0)
        {
            m_targetObject.Name = m_target;
            return m_targetObject.tryResolve(dt, out uuid);
        }

        return tryResolveSlot(dt, out uuid);
    }

    private bool tryResolveSlot(float dt, out string uuid)
    {
        uuid = "";
        if (m_resolvedSlot != m_targetPlayerSlot)
        {
            m_resolvedSlot = m_targetPlayerSlot;
            m_slotUuid = "";
            m_slotLooked = false;
            m_slotWarned = false;
        }

        if (m_slotUuid.Length > 0)
        {
            if (World.tryGetPlayerController(m_slotUuid, out PlayerController controller) &&
                controller.getPlayerSlot() == m_targetPlayerSlot)
            {
                uuid = m_slotUuid;
                return true;
            }

            m_slotUuid = "";
            m_slotLooked = false;
        }

        m_sinceSlotLookup += dt;
        if (m_slotLooked && m_sinceSlotLookup < SlotRetrySeconds)
        {
            return false;
        }

        m_slotLooked = true;
        m_sinceSlotLookup = 0.0f;
        m_slotUuid = StarterUtils.findPlayerBody(m_targetPlayerSlot);
        if (m_slotUuid.Length > 0)
        {
            m_slotWarned = false;
            uuid = m_slotUuid;
            return true;
        }

        if (!m_slotWarned)
        {
            m_slotWarned = true;
            Log.warn($"Follower: no player body holds slot {m_targetPlayerSlot}; it will keep looking.");
        }

        return false;
    }
}
