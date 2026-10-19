// Turns its object about a world-space axis: composes quaternions on a kinematic object, and sets the
// angular velocity on a physics body so the engine stays in charge.
using System;
using System.Numerics;
using ScriptBridge;

public class Spinner : ScriptBase
{
    [ExposeToEditor("Axis")]
    private Vector3 m_axis = new Vector3(0.0f, 1.0f, 0.0f);

    [ExposeToEditor("Degrees Per Second")]
    private float m_degreesPerSecond = 45.0f;

    public override void fixedUpdate(float dt)
    {
        if (m_axis.LengthSquared() <= 0.0f || !float.IsFinite(m_axis.LengthSquared()))
        {
            return;
        }

        Vector3 axis = Vector3.Normalize(m_axis);

        if (World.tryGetRigidBody(EntityId, out RigidBody body))
        {
            body.setAngularVelocity(axis * m_degreesPerSecond);
            return;
        }

        float radians = StarterUtils.perTick(m_degreesPerSecond, dt) * (MathF.PI / 180.0f);
        Quaternion step = Quaternion.CreateFromAxisAngle(axis, radians);
        Quaternion current = StarterUtils.eulerToQuaternion(transform.getRotation());
        Vector3 euler = StarterUtils.quaternionToEuler(Quaternion.Normalize(step * current));
        transform.setRotation(euler.X, euler.Y, euler.Z);
    }
}
