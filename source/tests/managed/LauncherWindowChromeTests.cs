using System.IO;
using System.Linq;
using System.Reflection;
using System.Xml.Linq;
using Xunit;

namespace ECS3DManagedTests;

// The window's XAML is not compiled into this assembly, so its chrome contract is read from the file. Snapping
// itself needs a real Win32 window; these pin the settings that give Windows one it can snap.
public class LauncherWindowChromeTests
{
  private const string RoleAttribute = "WindowDecorationProperties.ElementRole";

  private static XElement MainWindow()
  {
    var root = typeof(LauncherWindowChromeTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!;
    return XDocument.Load(Path.Combine(root, "apps", "launcher", "source", "Shell", "MainWindow.axaml")).Root!;
  }

  private static XElement SettingsView()
  {
    var root = typeof(LauncherWindowChromeTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!;
    return XDocument.Load(Path.Combine(root, "apps", "launcher", "source", "Tabs", "Settings", "SettingsView.axaml")).Root!;
  }

  private static bool ShowsVersion(XElement element) =>
    element.Name.LocalName == "TextBlock" && (string?)element.Attribute("Text") == "{Binding Version}";

  private static string? RoleOfButtonClicking(XElement window, string handler) =>
    (string?)window.Descendants()
      .Single(element => element.Name.LocalName == "Button" && (string?)element.Attribute("Click") == handler)
      .Attribute(RoleAttribute);

  [Fact]
  public void WindowKeepsAResizableFrameExtendedUnderTheCustomTitleBar()
  {
    var window = MainWindow();

    Assert.Equal("BorderOnly", (string?)window.Attribute("WindowDecorations"));
    Assert.Equal("True", (string?)window.Attribute("ExtendClientAreaToDecorationsHint"));
  }

  // Win32 sizes the caption band from the hint, so it has to match the title bar row.
  [Fact]
  public void TitleBarHeightHintMatchesTheTitleBarRow()
  {
    var window = MainWindow();
    var root = window.Elements().Single(element => (string?)element.Attribute("Name") == "Root");
    var titleRowHeight = ((string?)root.Attribute("RowDefinitions"))!.Split(',')[0];

    Assert.Equal(titleRowHeight, (string?)window.Attribute("ExtendClientAreaTitleBarHeightHint"));
  }

  [Fact]
  public void TitleBarIsTheOnlyCaptionDragArea()
  {
    var titleBars = MainWindow().Descendants()
      .Where(element => (string?)element.Attribute(RoleAttribute) == "TitleBar")
      .ToList();

    var titleBar = Assert.Single(titleBars);
    Assert.Equal("0", (string?)titleBar.Attribute("Grid.Row"));
    Assert.Null(titleBar.Attribute("PointerPressed"));
  }

  [Fact]
  public void TitleBarDoesNotShowTheVersion()
  {
    Assert.DoesNotContain(MainWindow().Descendants(), ShowsVersion);
  }

  [Fact]
  public void SettingsViewShowsTheVersionUnderAbout()
  {
    var heading = SettingsView().Descendants()
      .Single(element => (string?)element.Attribute("Text") == "ABOUT");
    var section = heading.Parent!.Elements().SkipWhile(element => element != heading).Skip(1).ToList();

    Assert.Equal("Version", (string?)section[0].Attribute("Text"));
    Assert.True(ShowsVersion(section[1]));
  }

  [Theory]
  [InlineData("OnMinimize", "MinimizeButton")]
  [InlineData("OnMaximize", "MaximizeButton")]
  [InlineData("OnClose", "CloseButton")]
  public void CaptionButtonsCarryTheirWindowRoles(string handler, string role)
  {
    Assert.Equal(role, RoleOfButtonClicking(MainWindow(), handler));
  }
}
