// Moves toward a named object or a player body and stops short of it: steering with VelocityChange. Use it on a
// root object: positions are read in the world and moved in the parent's frame.
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

    private readonly TargetRef m_targetRef = new TargetRef("Follower");

    public override void fixedUpdate(float dt)
    {
        m_targetRef.Name = m_target;
        m_targetRef.Slot = m_targetPlayerSlot;
        if (!m_targetRef.tryResolve(dt, out string targetUuid) ||
            !World.tryGetTransform(targetUuid, out Transform target))
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
            steer(heading, distance - m_stopDistance, position, dt);
            return;
        }

        if (!arrived)
        {
            Vector3 step = heading * Math.Min(m_speed * dt, distance - m_stopDistance);
            transform.move(step.X, step.Y, step.Z);
        }
    }

    private void steer(Vector3 heading, float remaining, Vector3 position, float dt)
    {
        Vector3 velocity = rigidBody.getVelocity();
        if (m_ignoreHeight)
        {
            velocity.Y = 0.0f;
        }

        // Closing the gap by Grip each tick, a body carries on for about speed / Grip before it stops, so it
        // starts braking that far out instead of coasting past the stop distance.
        if (remaining < velocity.Length() / Grip)
        {
            heading = Vector3.Zero;
        }

        Vector3 change = (heading * (m_speed * dt) - velocity) * Grip;
        if (change.LengthSquared() > 0.0f)
        {
            rigidBody.applyForce(change, position, ForceMode.VelocityChange);
        }
    }
}
