using System.Collections.Generic;

namespace ECS3DLauncher.Services;

public sealed record LearnGuide(int Number, string Title, string Summary, string Url);

// The Learn tab's content: the numbered guides and the shipped-content counts behind the banner.
public static class LearnCatalog
{
    public static string GuidesUrl => "https://github.com/SharkFinPro/ECS3D/blob/main/docs/guides/";

    public static int SampleCount => 2;
    public static int TemplateCount => 2;
    public static int StarterScriptCount => 18;
    public static int ConvexShapeCount => 9;

    public static string BoxSummary =>
        $"Every install includes {SampleCount} samples, {TemplateCount} templates, "
        + $"{StarterScriptCount} starter scripts and {ConvexShapeCount} convex shapes.";

    public static IReadOnlyList<LearnGuide> Guides { get; } =
    [
        Guide(1, "Getting Started",
            "Open the First-Person template in the editor, press Play and walk around with WASD.",
            "getting-started"),
        Guide(2, "Editor Basics",
            "Find your way around the hierarchy, inspector, gizmos, undo, keybinds and the Console panel.",
            "editor-basics"),
        Guide(3, "Your First Script",
            "Write a ScriptBase class, expose fields to the editor, attach it and hot reload your changes.",
            "your-first-script"),
        Guide(4, "Scene Scripts and Events",
            "Share logic across the scene with SceneScript and events, building a ScoreKeeper step by step.",
            "scene-scripts-and-events"),
        Guide(5, "Multiplayer",
            "Host Coin Rush, share your address, join a friend's game and get past the firewall.",
            "multiplayer"),
        Guide(6, "Starter Scripts",
            $"All {StarterScriptCount} ready-made scripts: what each does, its fields and events, and where the samples use them.",
            "starter-scripts"),
    ];

    private static LearnGuide Guide(int number, string title, string summary, string file) =>
        new(number, title, summary, $"{GuidesUrl}{file}.md");
}
