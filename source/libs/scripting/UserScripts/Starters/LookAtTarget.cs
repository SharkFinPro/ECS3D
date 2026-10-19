// Turns to face a named object or a player body at a capped rate. Use it on a root object: a child's aim is in
// its parent's frame, not the world's.
using System;
using System.Numerics;
using ScriptBridge;

public class LookAtTarget : ScriptBase
{
    [ExposeToEditor("Target")]
    private string m_target = "";

    // 0 or more targets that player's body and wins over Target.
    [ExposeToEditor("Target Player Slot")]
    private int m_targetPlayerSlot = -1;

    // Degrees per second; 0 snaps straight to the target.
    [ExposeToEditor("Turn Speed")]
    private float m_turnSpeed = 180.0f;

    [ExposeToEditor("Yaw Only")]
    private bool m_yawOnly = true;

    private const float ToDegrees = 180.0f / MathF.PI;

    private readonly TargetRef m_targetRef = new TargetRef("LookAtTarget");

    public override void start()
    {
        if (World.tryGetRigidBody(EntityId, out _))
        {
            Log.warn("LookAtTarget: this object has a RigidBody, so physics and the script will fight over " +
                     "its rotation. Use it on an object without one.");
        }
    }

    public override void fixedUpdate(float dt)
    {
        m_targetRef.Name = m_target;
        m_targetRef.Slot = m_targetPlayerSlot;
        if (!m_targetRef.tryResolve(dt, out string targetUuid) ||
            !World.tryGetTransform(targetUuid, out Transform target))
        {
            return;
        }

        Vector3 delta = target.getPosition() - transform.getPosition();
        if (m_yawOnly)
        {
            delta.Y = 0.0f;
        }

        float length = delta.Length();
        if (length < 1e-3f)
        {
            return;
        }

        Vector3 localRotation = transform.getLocalRotation();

        // Forward is -Z; with the engine's Euler order (yaw about Y after pitch about X) it points along
        // (-cos(pitch) sin(yaw), sin(pitch), -cos(pitch) cos(yaw)). Straight above or below, yaw is undefined,
        // so it keeps what it had.
        float horizontal = MathF.Sqrt(delta.X * delta.X + delta.Z * delta.Z);
        float yaw = horizontal < 1e-3f ? localRotation.Y : MathF.Atan2(-delta.X, -delta.Z) * ToDegrees;
        float pitch = m_yawOnly ? 0.0f : MathF.Asin(Math.Clamp(delta.Y / length, -1.0f, 1.0f)) * ToDegrees;
        Vector3 goal = new Vector3(pitch, yaw, 0.0f);

        if (m_turnSpeed <= 0.0f)
        {
            transform.setRotation(goal.X, goal.Y, goal.Z);
            return;
        }

        Quaternion current = StarterUtils.eulerToQuaternion(localRotation);
        Quaternion desired = StarterUtils.eulerToQuaternion(goal);
        float dot = Quaternion.Dot(current, desired);
        if (dot < 0.0f)
        {
            desired = Quaternion.Negate(desired);
            dot = -dot;
        }

        float angle = 2.0f * MathF.Acos(Math.Min(dot, 1.0f)) * ToDegrees;
        float maxStep = m_turnSpeed * dt;
        if (angle <= maxStep)
        {
            transform.setRotation(goal.X, goal.Y, goal.Z);
            return;
        }

        Quaternion turned = Quaternion.Normalize(Quaternion.Slerp(current, desired, maxStep / angle));
        Vector3 euler = StarterUtils.quaternionToEuler(turned);
        transform.setRotation(euler.X, euler.Y, euler.Z);
    }
}
