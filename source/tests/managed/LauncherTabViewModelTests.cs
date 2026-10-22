using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Xml.Linq;
using Avalonia.Media;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Converters;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Shell;
using ECS3DLauncher.Tabs.Templates;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherTabViewModelTests
{
  private static readonly XNamespace XamlNamespace = "http://schemas.microsoft.com/winfx/2006/xaml";

  private static string SourceRoot() =>
    typeof(LauncherTabViewModelTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!;

  private static string[] IconKeys() =>
    XDocument.Load(Path.Combine(SourceRoot(), "apps", "launcher", "source", "Shared", "Resources", "Icons.axaml"))
      .Descendants()
      .Select(element => (string?)element.Attribute(XamlNamespace + "Key"))
      .OfType<string>()
      .ToArray();

  [Fact]
  public void NavItemRaisesCountAndActiveChangesOnlyForANewValue()
  {
    var item = new NavItemViewModel { Key = "projects", Label = "Projects", Icon = "IconFolder" };
    var log = new PropertyLog(item);

    item.Count = "3";
    item.IsActive = true;

    Assert.Equal(new[] { "Count", "IsActive" }, log.Names.ToArray());

    item.Count = "3";
    item.IsActive = true;

    Assert.Equal(2, log.Names.Count);
  }

  [Fact]
  public void TemplatesAreSixNamedAndDescribed()
  {
    var templates = new TemplatesViewModel().Templates;

    Assert.Equal(6, templates.Count);
    Assert.Equal(templates.Count, templates.Select(template => template.Name).Distinct().Count());
    Assert.All(templates, template => Assert.False(string.IsNullOrWhiteSpace(template.Description)));
  }

  [Fact]
  public void EveryIconKeyTheShellAndTemplatesUseIsDefined()
  {
    var keys = IconKeys();
    var shell = new MainWindowViewModel(
      new LauncherRig().NewEditor(),
      new FakeProcessRunner(),
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      "1.2.3",
      action => action());
    var used = shell.NavItems.Select(item => item.Icon)
      .Concat(shell.Templates.Templates.Select(template => template.Icon))
      .Append(new Project { Name = "", Path = "", Edited = "" }.PlaceholderIcon)
      .Distinct()
      .ToArray();

    Assert.NotEmpty(used);
    Assert.All(used, key => Assert.Contains(key, keys));
  }

  [Fact]
  public void TheIconKeyScanFindsKnownKeysAndRejectsMadeUpOnes()
  {
    var keys = IconKeys();

    Assert.Contains("IconFolder", keys);
    Assert.DoesNotContain("IconDoesNotExist", keys);
  }

  [Fact]
  public void ARealCardIsTheInverseOfTheNewCard()
  {
    var real = new Project { Name = "Alpha", Path = "/work/alpha", Edited = "today" };
    var sentinel = new Project { Name = "", Path = "", Edited = "", IsNewCard = true };

    Assert.True(real.IsRealCard);
    Assert.False(sentinel.IsRealCard);
  }

  [Fact]
  public void AProjectWithoutAThumbnailOrGlowShowsNeither()
  {
    var plain = new Project { Name = "Alpha", Path = "/work/alpha", Edited = "today" };
    var clearGlow = new Project
    {
      Name = "Beta",
      Path = "/work/beta",
      Edited = "today",
      GlowColor = Color.FromArgb(0, 255, 0, 0),
    };

    Assert.False(plain.HasThumbnail);
    Assert.Null(plain.GlowBrush);
    Assert.Null(clearGlow.GlowBrush);
  }

  [Fact]
  public void TheIconConverterIgnoresNonStringsAndCannotConvertBack()
  {
    var converter = IconKeyConverter.Instance;

    Assert.Null(converter.Convert(42, typeof(object), null, CultureInfo.InvariantCulture));
    Assert.Null(converter.Convert(null, typeof(object), null, CultureInfo.InvariantCulture));
    Assert.Throws<NotSupportedException>(
      () => converter.ConvertBack("IconFolder", typeof(string), null, CultureInfo.InvariantCulture));
  }
}
