using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Xml;
using System.Xml.Linq;
using Xunit;

namespace ECS3DManagedTests;

// At 125% display scale a font-derived line height can make Avalonia draw only the first wrapped line, so every
// wrapped TextBlock needs a LineHeight of its own. The views are not compiled into this assembly, so the source is read.
public class LauncherWrappedTextTests
{
  private static readonly string[] FilesWithWrappedText =
  {
    "LearnView.axaml", "MainWindow.axaml", "NewProjectDialog.axaml", "ProjectsView.axaml", "SamplesView.axaml",
    "SettingsView.axaml", "TemplatesView.axaml"
  };

  private static string LauncherSource() =>
    Path.Combine(typeof(LauncherWrappedTextTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!, "apps", "launcher", "source");

  private static bool IsText(XElement e) => e.Name.LocalName is "TextBlock" or "SelectableTextBlock";

  private static bool IsStyle(XElement e) => e.Name.LocalName is "Style" or "ControlTheme";

  private static bool IsSetter(XElement e, string property) =>
    e.Name.LocalName == "Setter" && (string?)e.Attribute("Property") == property;

  // A value written as an attribute, or as a property element such as <TextBlock.LineHeight>.
  private static string? PropertyValue(XElement owner, string property) =>
    (string?)owner.Attribute(property) ??
    owner.Elements().FirstOrDefault(c => c.Name.LocalName.EndsWith("." + property))?.Value.Trim();

  private static string? SetterValue(XElement setter) =>
    (string?)setter.Attribute("Value") ??
    setter.Elements().FirstOrDefault(c => c.Name.LocalName == "Setter.Value")?.Value.Trim();

  private static bool Wraps(string? value) => !string.IsNullOrEmpty(value) && value != "NoWrap";

  private static bool IsLineHeight(string? value) =>
    double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var h) && double.IsFinite(h) && h > 0;

  private static string Where(string file, XElement e) =>
    $"{Path.GetFileName(file)}:{((IXmlLineInfo)e).LineNumber}";

  [Fact]
  public void EveryWrappedTextBlockAndWrappingStyleSetsALineHeight()
  {
    var missing = new List<string>();
    var wrapping = new List<string>();

    foreach (var file in Directory.EnumerateFiles(LauncherSource(), "*.axaml", SearchOption.AllDirectories))
    {
      var root = XDocument.Load(file, LoadOptions.SetLineInfo).Root!;

      foreach (var e in root.DescendantsAndSelf().Where(e => IsText(e) && Wraps(PropertyValue(e, "TextWrapping"))))
      {
        wrapping.Add(Path.GetFileName(file));
        if (!IsLineHeight(PropertyValue(e, "LineHeight")))
        {
          missing.Add($"{Where(file, e)} {e.Name.LocalName}");
        }
      }

      foreach (var style in root.Descendants().Where(e => IsStyle(e) &&
                 e.Elements().Any(s => IsSetter(s, "TextWrapping") && Wraps(SetterValue(s)))))
      {
        wrapping.Add(Path.GetFileName(file));
        if (!style.Elements().Any(s => IsSetter(s, "LineHeight") && IsLineHeight(SetterValue(s))))
        {
          missing.Add($"{Where(file, style)} {style.Name.LocalName} {(string?)style.Attribute("Selector")}");
        }
      }
    }

    Assert.Equal(FilesWithWrappedText, wrapping.Distinct().OrderBy(f => f, StringComparer.Ordinal).ToArray());
    Assert.True(wrapping.Count >= 17, $"found only {wrapping.Count} wrapping TextBlocks and styles");
    Assert.True(missing.Count == 0, "wrapped text without a positive LineHeight: " + string.Join(", ", missing));
  }
}
