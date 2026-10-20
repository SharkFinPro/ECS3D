// Demonstrates triggers and events: touching a player publishes a collected event, then the pickup removes itself.
// Give its collider the trigger flag so players pass through it instead of bumping off it.
using ScriptBridge;

public class Collectible : ScriptBase
{
    [ExposeToEditor("Value")]
    private int m_value = 1;

    [ExposeToEditor("Event Name")]
    private string m_eventName = StarterEvents.Collected;

    private bool m_collected = false;

    public override void onCollisionEnter(string otherUuid)
    {
        if (m_collected || !World.tryGetPlayerController(otherUuid, out PlayerController player))
        {
            return;
        }

        m_collected = true;

        string eventName = string.IsNullOrWhiteSpace(m_eventName) ? StarterEvents.Collected : m_eventName;
        publish(eventName, new CollectedEvent
        {
            Slot = player.getPlayerSlot(),
            Value = m_value,
            CollectorUuid = otherUuid
        });

        World.destroy(EntityId);
    }
}
