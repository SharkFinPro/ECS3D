// A timer that ends the object: destroys it after Seconds of running time (paused time does not count).
using ScriptBridge;

public class Lifetime : ScriptBase
{
    [ExposeToEditor("Seconds")]
    private float m_seconds = 5.0f;

    public override void start()
    {
        after(m_seconds, () => World.destroy(EntityId));
    }
}
