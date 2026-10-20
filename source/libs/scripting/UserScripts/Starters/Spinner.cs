// Turns its object about an axis: composes quaternions on a kinematic object, and sets the angular velocity
// on a physics body so the engine stays in charge. The axis is in the parent's frame for a child object
// (the local rotation is what it reads and writes), and in world space for a root object.
using System;
using System.Numerics;
using ScriptBridge;

public class Spinner : ScriptBase
{
    [ExposeToEditor("Axis")]
    private Vector3 m_axis = new Vector3(0.0f, 1.0f, 0.0f);

    [ExposeToEditor("Degrees Per Second")]
    private float m_degreesPerSecond = 45.0f;

    // Kept across ticks because Euler angles lose precision near +/-90 degrees of yaw; reseeded whenever the
    // rotation read back is not the one written last tick (an editor or another script turned the object).
    private Quaternion m_orientation = Quaternion.Identity;

    private Vector3? m_lastWritten;

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
        Vector3 read = transform.getLocalRotation();
        if (m_lastWritten is not Vector3 last || !closeTo(last, read))
        {
            m_orientation = StarterUtils.eulerToQuaternion(read);
        }

        m_orientation = Quaternion.Normalize(step * m_orientation);
        Vector3 euler = StarterUtils.quaternionToEuler(m_orientation);
        m_lastWritten = euler;
        transform.setRotation(euler.X, euler.Y, euler.Z);
    }

    private static bool closeTo(Vector3 a, Vector3 b)
    {
        const float tolerance = 1e-3f;
        return MathF.Abs(a.X - b.X) <= tolerance && MathF.Abs(a.Y - b.Y) <= tolerance &&
               MathF.Abs(a.Z - b.Z) <= tolerance;
    }
}
