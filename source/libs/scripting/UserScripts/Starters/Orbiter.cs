// Circles a point or a named object, following it if it moves, and can turn to face its direction of travel.
// The center is read in world space and the position is written locally, so the orbiter should be a root
// object; a parented orbiter orbits in its parent's frame, offset by the parent.
using System;
using System.Numerics;
using ScriptBridge;

public class Orbiter : ScriptBase
{
    [ExposeToEditor("Center Object")]
    private string m_centerObject = "";

    [ExposeToEditor("Center")]
    private Vector3 m_center = Vector3.Zero;

    [ExposeToEditor("Radius")]
    private float m_radius = 5.0f;

    [ExposeToEditor("Period Seconds")]
    private float m_periodSeconds = 10.0f;

    [ExposeToEditor("Axis")]
    private Vector3 m_axis = new Vector3(0.0f, 1.0f, 0.0f);

    [ExposeToEditor("Start Angle")]
    private float m_startAngle = 0.0f;

    [ExposeToEditor("Face Travel")]
    private bool m_faceTravel = false;

    private readonly NamedObject m_centerLookup = new NamedObject("Orbiter Center Object");

    private double m_time = 0.0;

    public override void start()
    {
        m_time = 0.0;

        if (World.tryGetRigidBody(EntityId, out _))
        {
            Log.warn("Orbiter: this object has a RigidBody, so physics and the script will fight over its position.");
        }

        if (hasParent())
        {
            Log.warn("Orbiter: this object has a parent, so it orbits in its parent's frame; make it a root object.");
        }
    }

    public override void fixedUpdate(float dt)
    {
        m_centerLookup.Name = m_centerObject;
        bool centerFound = m_centerLookup.tryResolve(dt, out string centerUuid);
        m_time += dt;

        if (!tryGetCenter(centerFound, centerUuid, out Vector3 center) ||
            m_axis.LengthSquared() <= 0.0f || !float.IsFinite(m_axis.LengthSquared()))
        {
            return;
        }

        Vector3 axis = Vector3.Normalize(m_axis);
        orbitBasis(axis, out Vector3 u, out Vector3 v);

        double turns = m_periodSeconds > 0.0f ? m_time / m_periodSeconds : 0.0;
        double degrees = (m_startAngle + 360.0 * turns) % 360.0;
        float angle = (float)(degrees * (Math.PI / 180.0));
        Vector3 radial = u * MathF.Cos(angle) + v * MathF.Sin(angle);
        transform.setPosition(center + radial * m_radius);

        if (m_faceTravel)
        {
            Vector3 travel = v * MathF.Cos(angle) - u * MathF.Sin(angle);
            faceAlong(m_radius < 0.0f ? -travel : travel, axis);
        }
    }

    // A parent shows up as a world transform that differs from the local one.
    private bool hasParent()
    {
        return Vector3.Distance(transform.getPosition(), transform.getLocalPosition()) > 1e-4f ||
               Vector3.Distance(transform.getRotation(), transform.getLocalRotation()) > 1e-3f;
    }

    // False while a named center is missing, so the object holds still instead of jumping to the fixed Center.
    private bool tryGetCenter(bool centerFound, string centerUuid, out Vector3 center)
    {
        center = m_center;
        if (string.IsNullOrEmpty(m_centerObject))
        {
            return true;
        }

        if (centerFound && World.tryGetTransform(centerUuid, out Transform centerTransform))
        {
            center = centerTransform.getPosition();
            return true;
        }

        return false;
    }

    // The orbit plane's basis: u is world X (or world Z when the axis is near X) with the axis part removed,
    // and v = axis x u, so the angle grows counterclockwise looking down the axis.
    private static void orbitBasis(Vector3 axis, out Vector3 u, out Vector3 v)
    {
        Vector3 reference = MathF.Abs(axis.X) < 0.9f ? Vector3.UnitX : Vector3.UnitZ;
        u = Vector3.Normalize(reference - axis * Vector3.Dot(reference, axis));
        v = Vector3.Cross(axis, u);
    }

    // Forward is -Z, up is the orbit axis, so the object stays upright in its orbit plane.
    private void faceAlong(Vector3 travel, Vector3 axis)
    {
        Vector3 right = Vector3.Cross(travel, axis);
        Matrix4x4 basis = new Matrix4x4(
            right.X, right.Y, right.Z, 0.0f,
            axis.X, axis.Y, axis.Z, 0.0f,
            -travel.X, -travel.Y, -travel.Z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
        Vector3 euler = StarterUtils.quaternionToEuler(Quaternion.CreateFromRotationMatrix(basis));
        transform.setRotation(euler.X, euler.Y, euler.Z);
    }
}
