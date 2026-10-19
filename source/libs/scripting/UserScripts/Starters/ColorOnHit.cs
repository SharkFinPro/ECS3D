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

    // Read just before the first swap, so a texture changed after start() is still the one restored.
    private string m_originalTexture = "";

    private bool m_originalCaptured = false;

    private bool m_warnedFailure = false;

    private bool m_warnedNoOriginal = false;

    // Kept per object, so a repeated enter for one object does not need two exits.
    private readonly HashSet<string> m_contacts = new HashSet<string>();

    public override void start()
    {
        m_contacts.Clear();
        m_originalCaptured = false;

        if (World.tryGetModelRenderer(EntityId, out ModelRenderer renderer))
        {
            m_renderer = renderer;
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
            if (!m_originalCaptured)
            {
                m_originalTexture = m_renderer.getTextureUUID();
                m_originalCaptured = true;
            }

            apply(m_hitTexture);
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
            if (m_originalTexture.Length == 0)
            {
                if (!m_warnedNoOriginal)
                {
                    m_warnedNoOriginal = true;
                    Log.warn("ColorOnHit: the object had no texture to restore, so it keeps the hit texture.");
                }

                return;
            }

            apply(m_originalTexture);
        }
    }

    private void apply(string texture)
    {
        if (!m_renderer!.setTexture(texture) && !m_warnedFailure)
        {
            m_warnedFailure = true;
            Log.warn($"ColorOnHit: texture '{texture}' could not be applied; check that it names a texture asset.");
        }
    }

    private bool isEligible(string otherUuid)
    {
        return !m_playersOnly || World.tryGetPlayerController(otherUuid, out _);
    }
}
