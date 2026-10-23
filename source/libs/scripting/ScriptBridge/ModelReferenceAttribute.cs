using System;

namespace ScriptBridge;

// Marks an [ExposeToEditor] string field as holding a model's uuid, so the editor draws a model slot
// instead of a text box. The value stays the uuid string.
[AttributeUsage(AttributeTargets.Field)]
public class ModelReferenceAttribute : AssetReferenceAttribute
{
    public ModelReferenceAttribute() : base("model")
    {
    }
}
