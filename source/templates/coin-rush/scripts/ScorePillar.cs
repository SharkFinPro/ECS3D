// Coin Rush's score display: grows its object upward as one player's score rises, since the engine has no text.
// It sizes a Block (2 units tall at scale 1) and keeps the base where it was placed. Use it on a root object,
// because the transform setters write local values.
using System;
using System.Numerics;
using ScriptBridge;

internal static class ScorePillarMath
{
    // The pillar's total height for a score: the minimum, plus the units each point adds, up to the maximum.
    internal static float height(int score, float unitsPerPoint, float minHeight, float maxHeight)
    {
        float min = float.IsFinite(minHeight) ? MathF.Max(minHeight, 0.0f) : 0.0f;
        float per = float.IsFinite(unitsPerPoint) ? MathF.Max(unitsPerPoint, 0.0f) : 0.0f;
        float max = float.IsFinite(maxHeight) ? MathF.Max(maxHeight, min) : float.MaxValue;
        return MathF.Min(min + MathF.Max(score, 0) * per, max);
    }

    // A Block's Y scale is a half-extent, so it is half the height, and the center sits that far above the base.
    internal static float halfHeight(float height) => height * 0.5f;

    internal static float centerY(float baseY, float height) => baseY + halfHeight(height);
}

public class ScorePillar : ScriptBase
{
    [ExposeToEditor("Slot")]
    private int m_slot;

    [ExposeToEditor("Units Per Point")]
    private float m_unitsPerPoint = 0.5f;

    // The height at a score of zero.
    [ExposeToEditor("Min Height")]
    private float m_minHeight = 0.2f;

    [ExposeToEditor("Max Height")]
    private float m_maxHeight = 12.0f;

    private float m_baseY;

    public override void start()
    {
        Vector3 position = transform.getLocalPosition();
        Vector3 scale = transform.getLocalScale();
        m_baseY = position.Y - scale.Y;

        if (World.tryGetRigidBody(EntityId, out _))
        {
            Log.warn("ScorePillar: this object has a RigidBody, so physics and the script will fight over its size.");
        }

        subscribe<ScoreChangedEvent>(StarterEvents.ScoreChanged, onScoreChanged);
        resize(0);
    }

    // ScoreKeeper publishes a score of zero for every slot when a round ends, which shrinks the pillar again.
    private void onScoreChanged(ScoreChangedEvent changed)
    {
        if (changed != null && changed.Slot == m_slot)
        {
            resize(changed.Score);
        }
    }

    private void resize(int score)
    {
        float height = ScorePillarMath.height(score, m_unitsPerPoint, m_minHeight, m_maxHeight);
        Vector3 position = transform.getLocalPosition();
        Vector3 scale = transform.getLocalScale();

        transform.setScale(scale.X, ScorePillarMath.halfHeight(height), scale.Z);
        transform.setPosition(position.X, ScorePillarMath.centerY(m_baseY, height), position.Z);
    }
}
