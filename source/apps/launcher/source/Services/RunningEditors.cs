using System;
using System.Collections.Generic;

namespace ECS3DLauncher.Services;

// Counts the editors this launcher started per project, so a card can show "In editor" until the last one exits.
public sealed class RunningEditors
{
    private readonly Func<string, string> _key;
    private readonly Action _changed;
    private readonly Dictionary<string, int> _counts = [];
    private readonly object _lock = new();

    // The key function makes two spellings of one project path count together.
    public RunningEditors(Func<string, string> key, Action changed)
    {
        _key = key;
        _changed = changed;
    }

    public bool IsRunning(string projectPath)
    {
        lock (_lock)
        {
            return _counts.ContainsKey(_key(projectPath));
        }
    }

    public void Started(string projectPath)
    {
        lock (_lock)
        {
            var key = _key(projectPath);
            _counts[key] = _counts.GetValueOrDefault(key) + 1;
        }

        _changed();
    }

    public void Stopped(string projectPath)
    {
        lock (_lock)
        {
            var key = _key(projectPath);
            var left = _counts.GetValueOrDefault(key) - 1;
            if (left > 0)
            {
                _counts[key] = left;
            }
            else
            {
                _counts.Remove(key);
            }
        }

        _changed();
    }
}
