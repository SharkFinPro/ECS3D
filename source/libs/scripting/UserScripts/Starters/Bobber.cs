// Moves its object back and forth along an axis on a sine wave around the position it started at. Positions
// are local, so the axis is in the parent's frame for a child object.
using System;
using System.Numerics;
using ScriptBridge;

public class Bobber : ScriptBase
{
    [ExposeToEditor("Axis")]
    private Vector3 m_axis = new Vector3(0.0f, 1.0f, 0.0f);

    [ExposeToEditor("Amplitude")]
    private float m_amplitude = 0.5f;

    [ExposeToEditor("Frequency Hz")]
    private float m_frequencyHz = 0.25f;

    [ExposeToEditor("Phase Degrees")]
    private float m_phaseDegrees = 0.0f;

    private Vector3 m_startPosition = Vector3.Zero;

    private double m_time = 0.0;

    public override void start()
    {
        m_startPosition = transform.getLocalPosition();
        m_time = 0.0;

        if (World.tryGetRigidBody(EntityId, out _))
        {
            Log.warn("Bobber: this object has a RigidBody, so physics and the script will fight over its position.");
        }
    }

    public override void fixedUpdate(float dt)
    {
        m_time += dt;

        if (m_axis.LengthSquared() <= 0.0f || !float.IsFinite(m_axis.LengthSquared()))
        {
            return;
        }

        double phase = m_phaseDegrees * (Math.PI / 180.0);
        float wave = (float)Math.Sin(2.0 * Math.PI * m_frequencyHz * m_time + phase);
        transform.setPosition(m_startPosition + Vector3.Normalize(m_axis) * (m_amplitude * wave));
    }
}
