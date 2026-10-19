// Scene script: moves a point light over a vertical arc and blends its color and ambient through a day.
using System;
using System.Numerics;
using ScriptBridge;

public class DayNightCycle : SceneScript
{
    [ExposeToEditor("Light")]
    private string m_light = "Sun";

    [ExposeToEditor("Period Seconds")]
    private float m_periodSeconds = 120.0f;

    [ExposeToEditor("Arc Radius")]
    private float m_arcRadius = 40.0f;

    [ExposeToEditor("Arc Center")]
    private Vector3 m_arcCenter = Vector3.Zero;

    // 0-1 through the day: 0 is sunrise in the east (+X), 0.25 noon, 0.5 sunset in the west, 0.75 midnight.
    [ExposeToEditor("Start Time")]
    private float m_startTime = 0.3f;

    [ExposeToEditor("Day Color")]
    private Vector3 m_dayColor = new Vector3(1.0f, 0.96f, 0.88f);

    [ExposeToEditor("Dusk Color")]
    private Vector3 m_duskColor = new Vector3(1.0f, 0.5f, 0.2f);

    [ExposeToEditor("Night Color")]
    private Vector3 m_nightColor = new Vector3(0.1f, 0.15f, 0.4f);

    [ExposeToEditor("Day Ambient")]
    private float m_dayAmbient = 0.3f;

    [ExposeToEditor("Night Ambient")]
    private float m_nightAmbient = 0.06f;

    // Height thresholds of the color blend: night at or below -0.1, dusk at 0, day at or above 0.3.
    private const float NightHeight = -0.1f;
    private const float DuskHeight = 0.0f;
    private const float DayHeight = 0.3f;

    private readonly NamedObject m_sun = new NamedObject("DayNightCycle");
    private float m_elapsed;
    private string m_litUuid = "";
    private float m_baseDiffuse;
    private bool m_warnedNoLight;
    private bool m_warnedRigidBody;

    public override void fixedUpdate(float dt)
    {
        m_sun.Name = m_light;
        m_elapsed += dt;
        if (!m_sun.tryResolve(dt, out string uuid) || !World.tryGetTransform(uuid, out Transform transform))
        {
            return;
        }

        warnIfRigidBody(uuid);

        float angle = 2.0f * MathF.PI * currentPhase();
        float height = MathF.Sin(angle);
        Vector3 position = m_arcCenter + m_arcRadius * new Vector3(MathF.Cos(angle), height, 0.0f);
        transform.setPosition(position);

        if (!World.tryGetLightRenderer(uuid, out LightRenderer light))
        {
            if (!m_warnedNoLight)
            {
                m_warnedNoLight = true;
                Log.warn($"DayNightCycle: '{m_light}' has no LightRenderer; only its position will change.");
            }

            return;
        }

        applyLight(uuid, light, height, position);
    }

    private float currentPhase()
    {
        float phase = m_periodSeconds > 0.0f ? m_startTime + m_elapsed / m_periodSeconds : m_startTime;
        return phase - MathF.Floor(phase);
    }

    private void applyLight(string uuid, LightRenderer light, float height, Vector3 position)
    {
        if (uuid != m_litUuid)
        {
            m_litUuid = uuid;
            m_baseDiffuse = light.getDiffuse();
        }

        float ambient = MathF.Max(m_nightAmbient, lerp(m_nightAmbient, m_dayAmbient, saturate(height)));
        float diffuseScale = m_dayAmbient > 0.0f ? saturate(ambient / m_dayAmbient) : 1.0f;

        light.setColor(colorAt(height));
        light.setAmbient(ambient);
        light.setDiffuse(m_baseDiffuse * diffuseScale);

        if (light.getIsSpotLight())
        {
            Vector3 toCenter = m_arcCenter - position;
            if (toCenter.LengthSquared() > 1e-8f)
            {
                light.setDirection(Vector3.Normalize(toCenter));
            }
        }
    }

    private Vector3 colorAt(float height)
    {
        if (height <= NightHeight)
        {
            return m_nightColor;
        }

        if (height < DuskHeight)
        {
            return Vector3.Lerp(m_nightColor, m_duskColor, (height - NightHeight) / (DuskHeight - NightHeight));
        }

        if (height < DayHeight)
        {
            return Vector3.Lerp(m_duskColor, m_dayColor, (height - DuskHeight) / (DayHeight - DuskHeight));
        }

        return m_dayColor;
    }

    private void warnIfRigidBody(string uuid)
    {
        if (!m_warnedRigidBody && World.tryGetRigidBody(uuid, out _))
        {
            m_warnedRigidBody = true;
            Log.warn($"DayNightCycle: '{m_light}' has a RigidBody, so physics and this script will fight over it.");
        }
    }

    private static float lerp(float a, float b, float t) => a + (b - a) * t;

    private static float saturate(float value) => Math.Clamp(value, 0.0f, 1.0f);
}
