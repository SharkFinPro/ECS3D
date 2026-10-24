using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Xml.Linq;
using Xunit;

namespace ECS3DManagedTests;

// The window and project files are not compiled into this assembly, so the icon wiring is read from the source tree.
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

  // Entries are checked by header rather than loaded: the headless icon loader is a stub that never decodes.
  [Fact]
  public void IconFileIsAnIcoWithSmallAndLargeSizesAndPngEntries()
  {
    var bytes = File.ReadAllBytes(IconFile());
    var count = BitConverter.ToUInt16(bytes, 4);
    var widths = Enumerable.Range(0, count).Select(i => bytes[6 + i * 16] == 0 ? 256 : (int)bytes[6 + i * 16]).ToList();
    byte[] pngSignature = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

    Assert.Equal(new byte[] { 0, 0, 1, 0 }, bytes.Take(4).ToArray());
    Assert.True(count > 0);
    Assert.Contains(16, widths);
    Assert.Contains(256, widths);

    for (var i = 0; i < count; i++)
    {
      var entry = 6 + i * 16;
      var size = BitConverter.ToUInt32(bytes, entry + 8);
      var offset = BitConverter.ToUInt32(bytes, entry + 12);

      Assert.True((long)offset + size <= bytes.Length);
      Assert.Equal(pngSignature, bytes.Skip((int)offset).Take(8).ToArray());
    }
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
    var grid = MainWindow().Descendants()
      .Single(g => g.Name.LocalName == "Grid" && (string?)g.Attribute("ColumnDefinitions") == "Auto,Auto,Auto,*,Auto,Auto");
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
}
