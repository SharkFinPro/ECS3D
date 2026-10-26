// Demonstrates cursor picking: drag a rigid body with the left mouse button and let go to fling it the opposite way.
using System;
using System.Numerics;
using ScriptBridge;

public class DragLauncher : ScriptBase
{
    // Launch speed per unit of drag length.
    [ExposeToEditor("Strength")]
    private float m_strength = 4.0f;

    [ExposeToEditor("Max Speed")]
    private float m_maxSpeed = 30.0f;

    [ExposeToEditor("Max Distance")]
    private float m_maxDistance = 100.0f;

    // A shorter drag is a click, not a pull.
    [ExposeToEditor("Min Drag")]
    private float m_minDrag = 0.1f;

    [ExposeToEditor("Log Launches")]
    private bool m_logLaunches = true;

    // InputUtils has no mouse edge query, so the previous state is tracked here.
    private bool m_wasDown = false;

    // The grabbed object's uuid, "" while nothing is held.
    private string m_target = "";

    private Vector3 m_grabPoint;

    private Vector3 m_planeNormal;

    private Vector3 m_dragPoint;

    public override void fixedUpdate(float dt)
    {
        bool down = input.mouseButton(MouseButton.Left);
        bool pressed = down && !m_wasDown;
        bool released = !down && m_wasDown;
        m_wasDown = down;

        if (!input.windowIsFocused() || (m_target.Length > 0 && !World.objectExists(m_target)))
        {
            m_target = "";
            return;
        }

        if (pressed)
        {
            tryGrab();
        }

        if (m_target.Length == 0)
        {
            return;
        }

        updateDragPoint();

        if (released)
        {
            launch();
            m_target = "";
        }
    }

    private void tryGrab()
    {
        if (!input.tryGetCursorRay(out Vector3 origin, out Vector3 direction) ||
            direction.LengthSquared() < 1e-8f ||
            !raycast(origin, Vector3.Normalize(direction), m_maxDistance, out RaycastHit hit) ||
            !World.tryGetRigidBody(hit.objectUuid, out _))
        {
            return;
        }

        m_target = hit.objectUuid;
        m_grabPoint = hit.point;
        m_dragPoint = hit.point;
        m_planeNormal = Vector3.Normalize(direction);
    }

    // The point where the cursor ray crosses the plane through the grab point; the last good one is kept when
    // the ray is parallel to the plane, points away from it or is unavailable.
    private void updateDragPoint()
    {
        if (!input.tryGetCursorRay(out Vector3 origin, out Vector3 direction))
        {
            return;
        }

        float facing = Vector3.Dot(m_planeNormal, direction);
        if (MathF.Abs(facing) < 1e-6f)
        {
            return;
        }

        float t = Vector3.Dot(m_planeNormal, m_grabPoint - origin) / facing;
        if (t > 0.0f)
        {
            m_dragPoint = origin + direction * t;
        }
    }

    private void launch()
    {
        Vector3 drag = m_dragPoint - m_grabPoint;
        if (drag.Length() < m_minDrag || !World.tryGetRigidBody(m_target, out RigidBody body))
        {
            return;
        }

        Vector3 velocity = -drag * m_strength;
        float speed = velocity.Length();
        if (speed > m_maxSpeed)
        {
            velocity *= m_maxSpeed / speed;
            speed = m_maxSpeed;
        }

        body.setVelocity(velocity);
        if (m_logLaunches)
        {
            Log.info(FormattableString.Invariant($"DragLauncher launched '{describe(m_target)}' at {speed:F1}"));
        }
    }

    private static string describe(string uuid)
    {
        string name = World.getObjectName(uuid);
        return name.Length == 0 ? uuid : name;
    }
}
