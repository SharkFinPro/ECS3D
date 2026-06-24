using System;

namespace ScriptBridge;

[AttributeUsage(AttributeTargets.Field)]
public class ExposeToEditorAttribute : Attribute
{
    public string DisplayName { get; }

    public ExposeToEditorAttribute(string displayName = "Script Variable")
    {
        DisplayName = displayName;
    }
}