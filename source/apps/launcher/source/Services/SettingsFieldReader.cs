using System;
using System.Text.Json.Nodes;

namespace ECS3DLauncher.Services;

// Reads one field at a time so a bad value costs only that field, with a log line naming it.
internal sealed class SettingsFieldReader
{
    private readonly JsonObject _root;
    private readonly string _file;
    private readonly ILauncherLog _log;

    public SettingsFieldReader(JsonObject root, string file, ILauncherLog log)
    {
        _root = root;
        _file = file;
        _log = log;
    }

    public string? ReadNullableString(string name, string? fallback)
    {
        if (_root.TryGetPropertyValue(name, out var node) && node is null)
        {
            return null;
        }

        return Read(name, fallback, _ => true, "a string");
    }

    public string ReadString(string name, string fallback) =>
        Read(name, fallback, _ => true, "a string");

    public bool ReadBool(string name, bool fallback) => Read(name, fallback, _ => true, "true or false");

    public int ReadPort(string name, int fallback) =>
        Read(name, fallback, SettingsInput.IsPort, "a whole number from 1 to 65535");

    public string ReadChoice(string name, string fallback, params string[] allowed) =>
        Read(name, fallback, value => Array.IndexOf(allowed, value) >= 0, "one of " + string.Join(", ", allowed));

    public LastJoin? ReadLastJoin(string name)
    {
        if (!_root.TryGetPropertyValue(name, out var node) || node is null)
        {
            return null;
        }

        if (node is JsonObject join)
        {
            var reader = new SettingsFieldReader(join, _file, _log);
            var address = reader.ReadString("address", "");
            var port = reader.ReadPort("port", 0);
            if (address.Length > 0 && port != 0)
            {
                return new LastJoin(address, port);
            }
        }

        Warn(name, "an object with an address and a port");
        return null;
    }

    private T Read<T>(string name, T fallback, Func<T, bool> isValid, string expected)
    {
        if (!_root.TryGetPropertyValue(name, out var node))
        {
            return fallback;
        }

        if (node is JsonValue json && json.TryGetValue<T>(out var value) && isValid(value))
        {
            return value;
        }

        Warn(name, expected);
        return fallback;
    }

    private void Warn(string name, string expected) =>
        _log.Warn($"{_file}: field '{name}' should be {expected}; using the default");
}
