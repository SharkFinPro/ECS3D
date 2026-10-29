using System.Collections.Generic;
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
  private static string LauncherSource() =>
    Path.Combine(typeof(LauncherWrappedTextTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!, "apps", "launcher", "source");

  private static bool IsWrap(string? value) => value == "Wrap";

  private static string Where(string file, XElement e) =>
    $"{Path.GetFileName(file)}:{((IXmlLineInfo)e).LineNumber}";

  private static bool IsWrapSetter(XElement e) =>
    e.Name.LocalName == "Setter" && (string?)e.Attribute("Property") == "TextWrapping" &&
    IsWrap((string?)e.Attribute("Value"));

  [Fact]
  public void EveryWrappedTextBlockAndWrappingStyleSetsALineHeight()
  {
    var missing = new List<string>();
    var wrappedTextBlocks = new List<string>();
    var wrappingStyles = new List<string>();

    foreach (var file in Directory.EnumerateFiles(LauncherSource(), "*.axaml", SearchOption.AllDirectories))
    {
      var root = XDocument.Load(file, LoadOptions.SetLineInfo).Root!;

      foreach (var e in root.DescendantsAndSelf().Where(e => e.Name.LocalName == "TextBlock" &&
                 IsWrap((string?)e.Attribute("TextWrapping"))))
      {
        wrappedTextBlocks.Add(Path.GetFileName(file));
        if (e.Attribute("LineHeight") == null)
        {
          missing.Add($"{Where(file, e)} TextBlock");
        }
      }

      foreach (var style in root.Descendants().Where(e => e.Name.LocalName == "Style" &&
                 e.Elements().Any(IsWrapSetter)))
      {
        wrappingStyles.Add(Path.GetFileName(file));
        if (!style.Elements().Any(s => s.Name.LocalName == "Setter" &&
                                       (string?)s.Attribute("Property") == "LineHeight"))
        {
          missing.Add($"{Where(file, style)} Style {(string?)style.Attribute("Selector")}");
        }
      }
    }

    Assert.True(wrappedTextBlocks.Count >= 14, $"found only {wrappedTextBlocks.Count} wrapped TextBlocks");
    Assert.Contains("NewProjectDialog.axaml", wrappedTextBlocks);
    Assert.Contains("MainWindow.axaml", wrappedTextBlocks);
    Assert.Contains("NewProjectDialog.axaml", wrappingStyles);
    Assert.Contains("SettingsView.axaml", wrappingStyles);
    Assert.True(missing.Count == 0, "wrapped text without a LineHeight: " + string.Join(", ", missing));
  }
}
