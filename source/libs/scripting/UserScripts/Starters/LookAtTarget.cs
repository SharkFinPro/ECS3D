// Turns to face a named object or a player body at a capped rate: aiming, players by slot, quaternion slerp.
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

    private const float SlotRetrySeconds = 1.0f;
    private const float ToDegrees = 180.0f / MathF.PI;

    private readonly NamedObject m_targetObject = new NamedObject("LookAtTarget");
    private string m_slotUuid = "";
    private int m_resolvedSlot = -1;
    private float m_sinceSlotLookup;
    private bool m_slotLooked;
    private bool m_slotWarned;

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
        if (!tryResolveTarget(dt, out string targetUuid) || !World.tryGetTransform(targetUuid, out Transform target))
        {
            return;
        }

        Vector3 delta = target.getPosition() - transform.getPosition();
        if (m_yawOnly)
        {
            delta.Y = 0.0f;
        }

        float length = delta.Length();
        if (length < 1e-4f)
        {
            return;
        }

        // Forward is -Z; with the engine's Euler order (yaw about Y after pitch about X) it points along
        // (-cos(pitch) sin(yaw), sin(pitch), -cos(pitch) cos(yaw)).
        float yaw = MathF.Atan2(-delta.X, -delta.Z) * ToDegrees;
        float pitch = m_yawOnly ? 0.0f : MathF.Asin(Math.Clamp(delta.Y / length, -1.0f, 1.0f)) * ToDegrees;
        Vector3 goal = new Vector3(pitch, yaw, 0.0f);

        if (m_turnSpeed <= 0.0f)
        {
            transform.setRotation(goal.X, goal.Y, goal.Z);
            return;
        }

        Quaternion current = StarterUtils.eulerToQuaternion(transform.getRotation());
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
            Log.warn($"LookAtTarget: no player body holds slot {m_targetPlayerSlot}; it will keep looking.");
        }

        return false;
    }
}
