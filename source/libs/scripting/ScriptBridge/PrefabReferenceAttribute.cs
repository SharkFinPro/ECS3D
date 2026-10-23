using System;

namespace ScriptBridge;

// Marks an [ExposeToEditor] string field as holding a prefab's uuid, so the editor draws a prefab slot
// instead of a text box. The value stays the uuid string.
[AttributeUsage(AttributeTargets.Field)]
public class PrefabReferenceAttribute : AssetReferenceAttribute
{
    public PrefabReferenceAttribute() : base("prefab")
    {
    }
}
