using System.Collections.Generic;
using System.Linq;

namespace ECS3DLauncher.Services;

public sealed record MissingExecutable(EngineExecutable Executable, string FileName, string ExpectedPath);

public sealed record EngineLocation(
    string Folder,
    IReadOnlyDictionary<EngineExecutable, string> Found,
    IReadOnlyList<MissingExecutable> Missing)
{
    public bool IsComplete => Missing.Count == 0;

    // The sentence for the engine-not-found banner; null when nothing is missing.
    public string? Describe() => Missing.Count == 0 ? null : Sentence(Folder, Missing);

    public static string Sentence(string folder, IReadOnlyList<MissingExecutable> missing)
    {
        var names = missing.Select(item => item.FileName).ToList();
        var list = names.Count == 1
            ? names[0]
            : string.Join(", ", names.Take(names.Count - 1)) + " and " + names[^1];
        var verb = names.Count == 1 ? "was" : "were";
        return $"{list} {verb} not found in {folder}";
    }
}
