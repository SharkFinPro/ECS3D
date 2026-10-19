// Demonstrates raycast queries: a ray from this object moves a marker to what it hits and logs changes of target.
using System.Numerics;
using ScriptBridge;

public class LaserPointer : ScriptBase
{
    // Local to the object: turned by its rotation before casting.
    [ExposeToEditor("Direction")]
    private Vector3 m_direction = new Vector3(0.0f, 0.0f, -1.0f);

    [ExposeToEditor("Max Distance")]
    private float m_maxDistance = 50.0f;

    [ExposeToEditor("Marker")]
    private string m_marker = "";

    [ExposeToEditor("Log Changes")]
    private bool m_logChanges = true;

    private readonly NamedObject m_markerObject = new NamedObject("LaserPointer Marker");

    // The uuid last hit, "" for nothing.
    private string m_lastHit = "";

    public override void fixedUpdate(float dt)
    {
        m_markerObject.Name = m_marker;
        m_markerObject.tryResolve(dt, out string markerUuid);

        Vector3 local = m_direction.LengthSquared() < 1e-8f
            ? new Vector3(0.0f, 0.0f, -1.0f)
            : Vector3.Normalize(m_direction);
        Vector3 direction = StarterUtils.rotate(transform.getRotation(), local);

        bool hitSomething = raycast(transform.getPosition(), direction, m_maxDistance, out RaycastHit hit);

        updateMarker(markerUuid, hitSomething, hit.point);
        logChange(hitSomething ? hit.objectUuid : "");
    }

    private static void updateMarker(string markerUuid, bool hitSomething, Vector3 point)
    {
        if (markerUuid.Length == 0)
        {
            return;
        }

        if (hitSomething && World.tryGetTransform(markerUuid, out Transform markerTransform))
        {
            markerTransform.setPosition(point);
        }

        if (World.tryGetModelRenderer(markerUuid, out ModelRenderer renderer))
        {
            renderer.setShouldRender(hitSomething);
        }
    }

    private void logChange(string hitUuid)
    {
        if (hitUuid == m_lastHit)
        {
            return;
        }

        m_lastHit = hitUuid;
        if (m_logChanges)
        {
            Log.info(hitUuid.Length == 0 ? "LaserPointer hit nothing" : $"LaserPointer hit '{describe(hitUuid)}'");
        }
    }

    private static string describe(string uuid)
    {
        string name = World.getObjectName(uuid);
        return name.Length == 0 ? uuid : name;
    }
}
