using System.Collections.Generic;
using System.Linq;

namespace ECS3DLauncher.Services;

// Tooltip is set only when the action is unavailable, and names the missing file.
public sealed record ActionState(bool Available, string? Tooltip);

// Which engine actions the located engine can run. Open and Explore need the editor, Play the client,
// Host the server and the client.
public sealed class EngineActions
{
    public EngineActions(EngineLocation engine)
    {
        Open = StateFor(engine, EngineExecutable.Editor);
        Explore = StateFor(engine, EngineExecutable.Editor);
        Play = StateFor(engine, EngineExecutable.Client);
        Host = StateFor(engine, EngineExecutable.Server, EngineExecutable.Client);
    }

    public ActionState Open { get; }

    public ActionState Explore { get; }

    public ActionState Play { get; }

    public ActionState Host { get; }

    private static ActionState StateFor(EngineLocation engine, params EngineExecutable[] needed)
    {
        var missing = new List<MissingExecutable>(
            engine.Missing.Where(item => needed.Contains(item.Executable)));
        return missing.Count == 0
            ? new ActionState(true, null)
            : new ActionState(false, EngineLocation.Sentence(engine.Folder, missing));
    }
}
