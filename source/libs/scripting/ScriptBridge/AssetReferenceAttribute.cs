using System;

namespace ScriptBridge;

// Base of the attributes that mark an [ExposeToEditor] string field as holding an asset's uuid, so the
// editor draws a slot for that asset kind instead of a text box. The value stays the uuid string.
[AttributeUsage(AttributeTargets.Field)]
public abstract class AssetReferenceAttribute : Attribute
{
    public string Asset { get; }

    protected AssetReferenceAttribute(string asset)
    {
        Asset = asset;
    }
}
