using System.Collections.Generic;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace ECS3DLauncher.Services;

// Applies the rules of the engine's parseTemplateManifest, plus a parseable minEngineVersion.
public static class TemplateManifestParser
{
    private const int MaxPlayers = 64;

    private const string TagsProblem = "field 'tags' must be an array of strings";

    private static readonly string[] StringFields =
        ["id", "name", "description", "category", "thumbnail", "entryScene", "minEngineVersion"];

    private static readonly string[] NonEmptyFields = ["name", "description", "entryScene", "minEngineVersion"];

    private static readonly Regex IdPattern = new("^[a-z0-9]+(-[a-z0-9]+)*$", RegexOptions.CultureInvariant);

    public static bool TryParse(string json, out TemplateManifest? manifest, out string reason)
    {
        manifest = null;
        try
        {
            using var document = JsonDocument.Parse(json);
            return TryRead(document.RootElement, out manifest, out reason);
        }
        catch (JsonException ex)
        {
            reason = $"not valid JSON ({ex.Message})";
            return false;
        }
    }

    private static bool TryRead(JsonElement root, out TemplateManifest? manifest, out string reason)
    {
        manifest = null;
        if (root.ValueKind != JsonValueKind.Object)
        {
            reason = "manifest must be a JSON object";
            return false;
        }

        var values = new Dictionary<string, string>();
        foreach (var field in StringFields)
        {
            if (!root.TryGetProperty(field, out var element))
            {
                reason = $"missing required field '{field}'";
                return false;
            }

            if (element.ValueKind != JsonValueKind.String)
            {
                reason = $"field '{field}' must be a string";
                return false;
            }

            values[field] = element.GetString()!;
        }

        var problem = FindValueProblem(values);
        if (problem is not null)
        {
            reason = problem;
            return false;
        }

        if (!TryReadTags(root, out var tags, out reason) || !TryReadPlayers(root, out var players, out reason))
        {
            return false;
        }

        manifest = new TemplateManifest(values["id"], values["name"], values["description"], values["category"], tags,
            values["thumbnail"], values["entryScene"], values["minEngineVersion"], players);
        return true;
    }

    private static string? FindValueProblem(Dictionary<string, string> values)
    {
        foreach (var field in NonEmptyFields)
        {
            if (values[field].Length == 0)
            {
                return $"field '{field}' must not be empty";
            }
        }

        if (!IdPattern.IsMatch(values["id"]))
        {
            return "field 'id' must be lowercase letters and digits separated by single hyphens";
        }

        if (values["category"] != TemplateManifest.TemplateCategory && values["category"] != TemplateManifest.SampleCategory)
        {
            return "field 'category' must be \"template\" or \"sample\"";
        }

        return EngineVersion.TryParse(values["minEngineVersion"], out _)
            ? null
            : "field 'minEngineVersion' is not a version";
    }

    private static bool TryReadTags(JsonElement root, out IReadOnlyList<string> tags, out string reason)
    {
        tags = [];
        reason = "";
        if (!root.TryGetProperty("tags", out var element))
        {
            reason = "missing required field 'tags'";
            return false;
        }

        if (element.ValueKind != JsonValueKind.Array)
        {
            reason = TagsProblem;
            return false;
        }

        var read = new List<string>();
        foreach (var tag in element.EnumerateArray())
        {
            if (tag.ValueKind != JsonValueKind.String)
            {
                reason = TagsProblem;
                return false;
            }

            read.Add(tag.GetString()!);
        }

        tags = read;
        return true;
    }

    private static bool TryReadPlayers(JsonElement root, out TemplatePlayers? players, out string reason)
    {
        players = null;
        reason = "";
        if (!root.TryGetProperty("players", out var element))
        {
            return true;
        }

        if (element.ValueKind != JsonValueKind.Object)
        {
            reason = "field 'players' must be an object with 'min' and 'max'";
            return false;
        }

        if (!TryGetInt(element, "min", out var min) || !TryGetInt(element, "max", out var max))
        {
            reason = "field 'players' needs integer 'min' and 'max'";
            return false;
        }

        if (min < 1 || max < min || max > MaxPlayers)
        {
            reason = $"field 'players' needs 1 <= min <= max <= {MaxPlayers}";
            return false;
        }

        players = new TemplatePlayers(min, max);
        return true;
    }

    private static bool TryGetInt(JsonElement parent, string name, out int value)
    {
        value = 0;
        return parent.TryGetProperty(name, out var element)
            && element.ValueKind == JsonValueKind.Number
            && element.TryGetInt32(out value);
    }
}
