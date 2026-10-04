using System.Collections.Generic;
using System.Linq;

namespace ScriptBridge;

// A by-name view of one live script instance on another object, from World.tryGetScript. It reaches only
// [ExposeToEditor] fields and public methods the script's own type adds, and a call through it runs under
// the target's fault gate, so a throwing target faults itself rather than the caller.
public sealed class ScriptHandle
{
    private readonly ScriptBase _instance;

    public string EntityId { get; }

    public string ClassName { get; }

    internal ScriptHandle(string uuid, string className, ScriptBase instance)
    {
        EntityId = uuid;
        ClassName = className;
        _instance = instance;
    }

    // False once the script is detached, replaced by a new instance, reloaded or faulted; every other
    // member then does nothing.
    public bool isAlive => Bridge.IsLive(EntityId, ClassName, _instance);

    public IReadOnlyList<string> exposedFieldNames =>
        isAlive
            ? Bridge.SupportedExposedFields(_instance).Select(f => f.Name).ToList()
            : new List<string>();

    // Strict: only fields exposedFieldNames lists, and the value must already be a T (no conversion).
    public bool tryGetField<T>(string fieldName, out T value)
    {
        if (isAlive && Bridge.TryReadSupportedExposedField(_instance, fieldName, out var read) && read is T found)
        {
            value = found;
            return true;
        }

        value = default!;
        return false;
    }

    public bool tryInvoke(string methodName, params object?[] args) =>
        Bridge.TryInvokeScript(EntityId, ClassName, _instance, methodName, args ?? new object?[] { null }, out _);

    public bool tryInvoke(string methodName, out object? result, params object?[] args) =>
        Bridge.TryInvokeScript(EntityId, ClassName, _instance, methodName, args ?? new object?[] { null },
                               out result);
}
