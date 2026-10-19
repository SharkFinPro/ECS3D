// Demonstrates collision events and runtime texture swaps: touching recolors the object until the last contact ends.
using System.Collections.Generic;
using ScriptBridge;

public class ColorOnHit : ScriptBase
{
    // The palette_red texture.
    [ExposeToEditor("Hit Texture")]
    private string m_hitTexture = "7c8c01db-771f-4048-94c2-004b8d19e7aa";

    [ExposeToEditor("Revert On Exit")]
    private bool m_revertOnExit = true;

    [ExposeToEditor("Players Only")]
    private bool m_playersOnly = false;

    private ModelRenderer? m_renderer = null;

    private string m_originalTexture = "";

    // Kept per object, so a repeated enter for one object does not need two exits.
    private readonly HashSet<string> m_contacts = new HashSet<string>();

    public override void start()
    {
        m_contacts.Clear();

        if (World.tryGetModelRenderer(EntityId, out ModelRenderer renderer))
        {
            m_renderer = renderer;
            m_originalTexture = renderer.getTextureUUID();
        }
        else
        {
            m_renderer = null;
            Log.warn("ColorOnHit: this object has no ModelRenderer, so there is nothing to recolor.");
        }
    }

    public override void onCollisionEnter(string otherUuid)
    {
        if (m_renderer == null || !isEligible(otherUuid) || !m_contacts.Add(otherUuid))
        {
            return;
        }

        if (m_contacts.Count == 1)
        {
            m_renderer.setTexture(m_hitTexture);
        }
    }

    public override void onCollisionExit(string otherUuid)
    {
        if (m_renderer == null || !m_contacts.Remove(otherUuid))
        {
            return;
        }

        if (m_contacts.Count == 0 && m_revertOnExit)
        {
            m_renderer.setTexture(m_originalTexture);
        }
    }

    private bool isEligible(string otherUuid)
    {
        return !m_playersOnly || World.tryGetPlayerController(otherUuid, out _);
    }
}
