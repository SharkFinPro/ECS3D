// Demonstrates cursor picking: drag a rigid body with the left mouse button and let go to fling it the opposite way.
using System;
using System.Numerics;
using ScriptBridge;

public class DragLauncher : ScriptBase
{
    // Launch speed in units per second per unit of drag length.
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

    private const float DefaultDt = 1.0f / 50.0f;

    // InputUtils has no mouse edge query, so the previous state is tracked here. The first tick only records it,
    // so a button already held at start is not a press.
    private bool m_wasDown = false;

    private bool m_seenInput = false;

    private float m_dt = DefaultDt;

    // The grabbed object's uuid, "" while nothing is held.
    private string m_target = "";

    // Distance along the cursor ray to the grab point, and the grab point relative to the object's position.
    private float m_depth;

    private Vector3 m_grabOffset;

    // Where the cursor is at the grab depth, relative to the grab point.
    private Vector3 m_pull;

    public override void fixedUpdate(float dt)
    {
        if (dt > 0.0f)
        {
            m_dt = dt;
        }

        bool down = input.mouseButton(MouseButton.Left);
        bool pressed = m_seenInput && down && !m_wasDown;
        bool released = m_seenInput && !down && m_wasDown;
        m_wasDown = down;
        m_seenInput = true;

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

        updatePull();

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
            !World.tryGetRigidBody(hit.objectUuid, out _) ||
            !World.tryGetTransform(hit.objectUuid, out Transform targetTransform))
        {
            return;
        }

        m_target = hit.objectUuid;
        m_depth = hit.distance;
        m_grabOffset = hit.point - targetTransform.getPosition();
        m_pull = Vector3.Zero;
    }

    // Measured at the grab depth from the camera, so walking or turning while holding does not drag the point
    // along; the last good pull is kept while the cursor ray is unavailable.
    private void updatePull()
    {
        if (!input.tryGetCursorRay(out Vector3 origin, out Vector3 direction) ||
            direction.LengthSquared() < 1e-8f ||
            !World.tryGetTransform(m_target, out Transform targetTransform))
        {
            return;
        }

        Vector3 cursorPoint = origin + Vector3.Normalize(direction) * m_depth;
        m_pull = cursorPoint - (targetTransform.getPosition() + m_grabOffset);
    }

    private void launch()
    {
        if (m_pull.Length() < m_minDrag || !World.tryGetRigidBody(m_target, out RigidBody body))
        {
            return;
        }

        // Strength and Max Speed are per second; the engine's velocity is per tick.
        Vector3 velocity = -m_pull * m_strength;
        float speed = velocity.Length();
        if (speed > m_maxSpeed)
        {
            velocity *= m_maxSpeed / speed;
            speed = m_maxSpeed;
        }

        body.setVelocity(velocity * StarterUtils.perTick(1.0f, m_dt));
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
