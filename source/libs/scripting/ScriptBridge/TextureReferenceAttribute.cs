using System;

namespace ScriptBridge;

// Marks an [ExposeToEditor] string field as holding a texture's uuid, so the editor draws a texture slot
// instead of a text box. The value stays the uuid string.
[AttributeUsage(AttributeTargets.Field)]
public class TextureReferenceAttribute : AssetReferenceAttribute
{
    public TextureReferenceAttribute() : base("texture")
    {
    }
}
