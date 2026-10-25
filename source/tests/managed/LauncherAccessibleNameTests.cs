using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Xml.Linq;
using Xunit;

namespace ECS3DManagedTests;

// The views' XAML is not compiled into this assembly, so the accessible-name contract is read from the files.
// Avalonia names a button after its Content only when that is a string or a lone TextBlock; any other content
// (a Grid, a Path, a Border) is announced by its type, which is what the bug bash saw through UI Automation.
public class LauncherAccessibleNameTests
{
  private const string NameAttribute = "AutomationProperties.Name";

  private static string LauncherSource() =>
    Path.Combine(
      typeof(LauncherAccessibleNameTests).Assembly
        .GetCustomAttributes<AssemblyMetadataAttribute>()
        .Single(a => a.Key == "ECS3DSourceRoot").Value!,
      "apps", "launcher", "source");

  private static IEnumerable<(string File, XElement Element)> Elements(string localName) =>
    Directory.EnumerateFiles(LauncherSource(), "*.axaml", SearchOption.AllDirectories)
      .SelectMany(file => XDocument.Load(file).Descendants()
        .Where(element => element.Name.LocalName == localName)
        .Select(element => (Path.GetFileName(file), element)));

  private static bool IsNamed(XElement button)
  {
    if (button.Attribute("Content") is not null || button.Attribute(NameAttribute) is not null)
    {
      return true;
    }

    var content = button.Elements().SingleOrDefault();
    return content is { Name.LocalName: "TextBlock" } && content.Attribute("Text") is not null;
  }

  private static string Describe((string File, XElement Element) entry) =>
    $"{entry.File}: {entry.Element.Attribute("Command")?.Value ?? entry.Element.Attribute("Click")?.Value ?? "?"}";

  private static XElement MainWindow() =>
    XDocument.Load(Path.Combine(LauncherSource(), "Shell", "MainWindow.axaml")).Root!;

  [Fact]
  public void EveryButtonHasAnAccessibleName()
  {
    var unnamed = Elements("Button").Where(entry => !IsNamed(entry.Element)).Select(Describe).ToList();

    Assert.True(unnamed.Count == 0, "Buttons announced by their content type: " + string.Join("; ", unnamed));
  }

  [Fact]
  public void EveryMenuItemHasAHeader()
  {
    Assert.All(Elements("InvokableMenuItem"), entry => Assert.NotNull(entry.Element.Attribute("Header")));
  }

  // A plain MenuItem is listed by assistive technology but cannot be invoked through it.
  [Fact]
  public void NoViewUsesAPlainMenuItem()
  {
    Assert.Empty(Elements("MenuItem"));
  }

  [Theory]
  [InlineData("OnMinimize", "Minimize")]
  [InlineData("OnMaximize", "Maximize")]
  [InlineData("OnClose", "Close")]
  public void CaptionButtonsAreNamedAfterTheirAction(string handler, string name)
  {
    var button = MainWindow().Descendants()
      .Single(element => element.Name.LocalName == "Button" && (string?)element.Attribute("Click") == handler);

    Assert.Equal(name, (string?)button.Attribute(NameAttribute));
  }

  [Theory]
  [InlineData("{Binding Projects.SetGridCommand}", "Grid view")]
  [InlineData("{Binding Projects.SetListCommand}", "List view")]
  [InlineData("{Binding OpenJoinCommand}", "Join a game")]
  [InlineData("{Binding NewProjectCommand}", "New Project")]
  public void ShellButtonsAreNamedAfterTheirAction(string command, string name)
  {
    var button = MainWindow().Descendants()
      .Single(element => element.Name.LocalName == "Button" && (string?)element.Attribute("Command") == command);

    Assert.Equal(name, (string?)button.Attribute(NameAttribute));
  }

  // The last child of a DockPanel fills what is left, so the Join button would span the empty sidebar below it.
  [Fact]
  public void SidebarDoesNotStretchItsLastButtonOverTheEmptySpace()
  {
    var sidebar = MainWindow().Descendants()
      .Single(element => element.Name.LocalName == "DockPanel" &&
                         element.Elements().Any(child => (string?)child.Attribute("Command") == "{Binding OpenJoinCommand}"));

    Assert.Equal("False", (string?)sidebar.Attribute("LastChildFill"));
  }
}
