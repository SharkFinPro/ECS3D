using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;

namespace ECS3DLauncher.Services;

// The launcher's side of recent-projects.json; the schema is source/libs/settings/RecentProjects.md.
// The editor writes the file too, so callers Load again before each change they want to keep.
public sealed class RecentProjectsStore
{
    internal const int MaxEntries = 50;

    private const int SchemaVersion = 1;
    private const int WriteAttempts = 5;
    private static readonly TimeSpan WriteRetryDelay = TimeSpan.FromMilliseconds(20);

    private static readonly JsonSerializerOptions Pretty = new() { WriteIndented = true };

    private readonly IFileSystem _fileSystem;
    private readonly ILauncherLog _log;
    private readonly string _path;
    private readonly TimeProvider _time;
    private readonly HostOs _os;
    private readonly Action<TimeSpan> _delay;
    private JsonObject _extra = new();
    private List<JsonObject> _projects = [];

    public RecentProjectsStore(
        IFileSystem fileSystem,
        ILauncherLog log,
        UserDataDirectory userData,
        TimeProvider time,
        HostOs os)
        : this(fileSystem, log, userData, time, os, Thread.Sleep)
    {
    }

    // The delay between write attempts is injected so tests do not wait.
    public RecentProjectsStore(
        IFileSystem fileSystem,
        ILauncherLog log,
        UserDataDirectory userData,
        TimeProvider time,
        HostOs os,
        Action<TimeSpan> delay)
    {
        _delay = delay;
        _fileSystem = fileSystem;
        _log = log;
        _path = userData.RecentProjectsFile();
        _time = time;
        _os = os;
    }

    public string FilePath => _path;

    public IReadOnlyList<RecentProject> Entries() => _projects.Select(ToEntry).ToList();

    // False when the file exists but could not be read (a sharing violation, a lock): the list is then empty
    // only for lack of a read, so callers must not save it over the real file. Corrupt JSON still returns true.
    public bool Load()
    {
        _extra = new JsonObject();
        _projects = [];
        if (!_fileSystem.FileExists(_path))
        {
            return true;
        }

        var readable = true;
        try
        {
            readable = LoadDocument();
        }
        catch (ArgumentException ex)
        {
            // A repeated key surfaces here, when a field is first read.
            _extra = new JsonObject();
            _projects = [];
            _log.Warn($"{_path}: could not read recent projects: {ex.Message}; starting with an empty list");
        }

        return readable;
    }

    // False when the file could not be read at all.
    private bool LoadDocument()
    {
        var root = ReadRoot(out var readable);
        if (root is null)
        {
            return readable;
        }

        if (root.TryGetPropertyValue("projects", out var node))
        {
            if (node is not JsonArray array)
            {
                _log.Warn($"{_path}: 'projects' is not an array; starting with an empty list");
                return true;
            }

            _projects = ValidEntries(array);
            root.Remove("projects");
        }

        _extra = root;
        return true;
    }

    public bool Save()
    {
        try
        {
            var directory = Path.GetDirectoryName(_path);
            if (!string.IsNullOrEmpty(directory))
            {
                _fileSystem.CreateDirectory(directory);
            }

            WriteWithRetry(BuildDocument().ToJsonString(Pretty));
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or NotSupportedException
                                       or ArgumentException)
        {
            _log.Warn($"{_path}: could not save recent projects: {ex.Message}");
            return false;
        }
    }

    // The editor may hold the file open for a moment, as the engine's writer allows for.
    private void WriteWithRetry(string contents)
    {
        for (var attempt = 1; attempt < WriteAttempts; attempt++)
        {
            try
            {
                _fileSystem.WriteAllTextAtomic(_path, contents);
                return;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                _delay(WriteRetryDelay);
            }
        }

        // The last attempt's failure reaches Save, which reports it.
        _fileSystem.WriteAllTextAtomic(_path, contents);
    }

    public void RecordOpened(string path) => RecordOpened(path, null);

    // A template id is only ever set here, never cleared by a later open.
    public void RecordOpened(string path, string? template)
    {
        if (string.IsNullOrEmpty(path))
        {
            return;
        }

        var normalized = RecentProjectPaths.Normalize(path, _os);
        var entry = TakeMatching(KeyOf(path)) ?? new JsonObject();
        entry["path"] = normalized;
        entry["name"] = RecentProjectPaths.DisplayName(normalized);
        entry["lastOpenedUtc"] = RecentProjectTime.Write(_time.GetUtcNow());
        if (!string.IsNullOrEmpty(template))
        {
            entry["template"] = template;
        }

        _projects.Insert(0, entry);
        EvictOverflow();
    }

    public bool SetPinned(string path, bool pinned)
    {
        var entry = FindEntry(KeyOf(path));
        if (entry is null)
        {
            return false;
        }

        if (pinned)
        {
            entry["pinned"] = true;
        }
        else
        {
            entry.Remove("pinned");
        }

        return true;
    }

    // Pins an unpinned entry and unpins a pinned one, judged from the entry as loaded.
    public bool TogglePinned(string path)
    {
        var entry = FindEntry(KeyOf(path));
        return entry is not null && SetPinned(path, !IsPinned(entry));
    }

    // Never touches the project's files.
    public bool Remove(string path)
    {
        var key = KeyOf(path);
        return _projects.RemoveAll(entry => StoredKey(entry) == key) > 0;
    }

    // Points an entry at a moved project, keeping its place, time, pin, template and unknown fields.
    public bool Relocate(string oldPath, string newPath)
    {
        var entry = FindEntry(KeyOf(oldPath));
        if (entry is null)
        {
            return false;
        }

        var normalized = RecentProjectPaths.Normalize(newPath, _os);
        var newKey = KeyOf(newPath);
        var duplicates = _projects.Where(other => !ReferenceEquals(other, entry) && StoredKey(other) == newKey).ToList();
        foreach (var duplicate in duplicates)
        {
            Absorb(entry, duplicate);
        }

        _projects.RemoveAll(duplicates.Contains);
        entry["path"] = normalized;
        entry["name"] = RecentProjectPaths.DisplayName(normalized);
        return true;
    }

    // The survivor keeps the newer open time, and any pin or template the other one had.
    private static void Absorb(JsonObject entry, JsonObject duplicate)
    {
        if (IsPinned(duplicate))
        {
            entry["pinned"] = true;
        }

        if (StringField(entry, "template") is null && StringField(duplicate, "template") is { } template)
        {
            entry["template"] = template;
        }

        var mine = RecentProjectTime.Parse(StringField(entry, "lastOpenedUtc"));
        var theirs = RecentProjectTime.Parse(StringField(duplicate, "lastOpenedUtc"));
        if (theirs is not null && (mine is null || theirs > mine))
        {
            entry["lastOpenedUtc"] = StringField(duplicate, "lastOpenedUtc");
        }
    }

    // Two spellings of one project path give the same key.
    public string ProjectKey(string path) => KeyOf(path);

    private string KeyOf(string path) => RecentProjectPaths.Key(path, _os);

    private string StoredKey(JsonObject entry) => KeyOf(StringField(entry, "path") ?? string.Empty);

    private JsonObject? FindEntry(string key) => _projects.FirstOrDefault(entry => StoredKey(entry) == key);

    // Removes every entry for the key and returns the first of them.
    private JsonObject? TakeMatching(string key)
    {
        var matches = _projects.Where(entry => StoredKey(entry) == key).ToList();
        _projects.RemoveAll(matches.Contains);
        return matches.FirstOrDefault();
    }

    // The front entry is the one just recorded, so never a candidate. The oldest goes; on a tie, the later one.
    private void EvictOverflow()
    {
        while (_projects.Count > MaxEntries)
        {
            var victim = -1;
            var victimTime = DateTimeOffset.MaxValue;
            for (var i = 1; i < _projects.Count; i++)
            {
                var time = RecentProjectTime.Parse(StringField(_projects[i], "lastOpenedUtc")) ?? DateTimeOffset.MinValue;
                if (!IsPinned(_projects[i]) && time <= victimTime)
                {
                    victim = i;
                    victimTime = time;
                }
            }

            if (victim < 0)
            {
                return;
            }

            _projects.RemoveAt(victim);
        }
    }

    private JsonObject? ReadRoot(out bool readable)
    {
        readable = true;
        try
        {
            var node = JsonNode.Parse(_fileSystem.ReadAllText(_path));
            if (node is JsonObject root)
            {
                return root;
            }

            _log.Warn($"{_path}: the top level is not an object; starting with an empty list");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            readable = false;
            _log.Warn($"{_path}: could not read recent projects: {ex.Message}; leaving the file alone");
        }
        catch (Exception ex) when (ex is JsonException or ArgumentException)
        {
            _log.Warn($"{_path}: could not read recent projects: {ex.Message}; starting with an empty list");
        }

        return null;
    }

    private List<JsonObject> ValidEntries(JsonArray array)
    {
        var result = new List<JsonObject>();
        foreach (var node in array)
        {
            if (node is JsonObject entry && StringField(entry, "path") is not null)
            {
                result.Add((JsonObject)entry.DeepClone());
            }
        }

        var dropped = array.Count - result.Count;
        if (dropped > 0)
        {
            _log.Warn($"{_path}: dropped {dropped} malformed entries");
        }

        return result;
    }

    // Written afresh from clones, since a node can have only one parent.
    private JsonObject BuildDocument()
    {
        var existing = _extra["version"] is JsonValue value && value.TryGetValue<int>(out var number) ? number : 0;
        var document = new JsonObject { ["version"] = Math.Max(SchemaVersion, existing) };
        foreach (var (key, node) in _extra)
        {
            if (key is not ("version" or "projects"))
            {
                document[key] = node?.DeepClone();
            }
        }

        var projects = new JsonArray();
        foreach (var entry in _projects)
        {
            projects.Add(entry.DeepClone());
        }

        document["projects"] = projects;
        return document;
    }

    private static RecentProject ToEntry(JsonObject entry) => new(
        StringField(entry, "path") ?? string.Empty,
        StringField(entry, "name") ?? string.Empty,
        StringField(entry, "lastOpenedUtc") ?? string.Empty,
        IsPinned(entry),
        StringField(entry, "template"));

    private static string? StringField(JsonObject entry, string key) =>
        entry[key] is JsonValue value && value.TryGetValue<string>(out var text) ? text : null;

    private static bool IsPinned(JsonObject entry) =>
        entry["pinned"] is JsonValue value && value.TryGetValue<bool>(out var pinned) && pinned;
}
