using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Xml.Linq;
using Avalonia.Controls;
using Xunit;

namespace ECS3DManagedTests;

// The window and project files are not compiled into this assembly, so the icon wiring is read from the source tree.
[Collection("AvaloniaHeadless")]
public class LauncherIconTests
{
  private const string IconPath = "source/Assets/launcher.ico";
  private const string MarkPath = "source/Assets/launcher-mark.png";

  private static string Launcher() =>
    Path.Combine(typeof(LauncherIconTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!, "apps", "launcher");

  private static string IconFile() => Path.Combine(Launcher(), IconPath.Replace('/', Path.DirectorySeparatorChar));

  private static XElement Project() => XDocument.Load(Path.Combine(Launcher(), "ECS3DLauncher.csproj")).Root!;

  private static XElement MainWindow() =>
    XDocument.Load(Path.Combine(Launcher(), "source", "Shell", "MainWindow.axaml")).Root!;

  private static string ProjectValue(string name) =>
    Project().Descendants(name).Single().Value;

  [Fact]
  public void ExecutableUsesTheIconFile()
  {
    Assert.Equal(IconPath, ProjectValue("ApplicationIcon"));
    Assert.True(File.Exists(IconFile()));
  }

  [Fact]
  public void IconFileIsAnIcoWithSmallAndLargeSizes()
  {
    var bytes = File.ReadAllBytes(IconFile());
    var count = BitConverter.ToUInt16(bytes, 4);
    var widths = Enumerable.Range(0, count).Select(i => bytes[6 + i * 16] == 0 ? 256 : (int)bytes[6 + i * 16]).ToList();

    Assert.Equal(new byte[] { 0, 0, 1, 0 }, bytes.Take(4).ToArray());
    Assert.Contains(16, widths);
    Assert.Contains(256, widths);
  }

  // The positive control: the Assets glob is what makes the avares URI below resolvable at run time.
  [Fact]
  public void WindowIconIsAnEmbeddedResourceAtItsAvaresUri()
  {
    var glob = Project().Descendants("AvaloniaResource").Select(e => (string?)e.Attribute("Include"));
    Assert.Contains("source/Assets/**", glob);

    var icon = (string?)MainWindow().Attribute("Icon");
    Assert.Equal($"avares://{ProjectValue("AssemblyName")}/{IconPath}", icon);
  }

  // The old mark was a Border holding a Path with IconModel; an Image whose Source is the mark file replaces it.
  [Fact]
  public void TitleBarMarkIsTheLauncherMarkImage()
  {
    var grid = MainWindow().Descendants("Grid")
      .Single(g => (string?)g.Attribute("ColumnDefinitions") == "Auto,Auto,Auto,*,Auto,Auto");
    var first = grid.Elements().Where(e => (string?)e.Attribute("Grid.Column") == "0").ToList();

    var image = Assert.Single(first, e => e.Name.LocalName == "Image");
    Assert.Equal($"avares://{ProjectValue("AssemblyName")}/{MarkPath}", (string?)image.Attribute("Source"));
    Assert.DoesNotContain(first.SelectMany(e => e.DescendantsAndSelf()), e =>
      e.Name.LocalName == "Path" && ((string?)e.Attribute("Data"))?.Contains("IconModel") == true);
  }

  [Fact]
  public void MarkFileIsAPngInTheAssetsFolder()
  {
    var file = Path.Combine(Launcher(), MarkPath.Replace('/', Path.DirectorySeparatorChar));

    Assert.True(File.Exists(file));
    Assert.Equal(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
      File.ReadAllBytes(file).Take(8).ToArray());
  }

  [Fact]
  public async Task IconFileLoadsAsAWindowIcon()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      Assert.Null(new Window().Icon);

      using var stream = File.OpenRead(IconFile());
      var window = new Window { Icon = new WindowIcon(stream) };

      Assert.NotNull(window.Icon);
    });
  }
}
