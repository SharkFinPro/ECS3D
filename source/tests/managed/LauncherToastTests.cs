using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Xml.Linq;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherToastTests : NewProjectFixture
{
  // Timers stay pending until a test completes them, and ignore cancellation, so a timer that outlives its
  // toast can be simulated.
  private sealed class FakeToastDelay
  {
    public List<(TimeSpan Span, CancellationToken Token, TaskCompletionSource Done)> Calls { get; } = [];

    public Task Delay(TimeSpan span, CancellationToken token)
    {
      var done = new TaskCompletionSource();
      Calls.Add((span, token, done));
      return done.Task;
    }

    public void Complete(int index) => Calls[index].Done.SetResult();
  }

  private (MainWindowViewModel Model, FakeToastDelay Delay) NewToastMain()
  {
    var delay = new FakeToastDelay();
    var model = NewMain();
    model.ToastDelay = delay.Delay;
    return (model, delay);
  }

  // The toast comes from the New Project button when no template exists.
  private static void ShowToast(MainWindowViewModel model) => model.NewProjectCommand.Execute(null);

  [Fact]
  public void TheTemplatesToastClearsWhenTheEngineFolderChangesToOneWithTemplates()
  {
    var model = NewMain();
    _rig.AddEngine(LauncherRig.OtherDir);
    AddTemplate("empty-3d", "Empty 3D", templatesFolder: Path.Combine(LauncherRig.OtherDir, "templates"));
    ShowToast(model);
    Assert.Contains("No project templates", model.ToastMessage);

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Single(model.Templates.Templates);
    Assert.False(model.HasToast);
  }

  [Fact]
  public void AnotherToastSurvivesATemplateReloadThatFindsTemplates()
  {
    var model = NewMain();
    _rig.AddEngine(LauncherRig.OtherDir);
    AddTemplate("empty-3d", "Empty 3D", templatesFolder: Path.Combine(LauncherRig.OtherDir, "templates"));
    _rig.Files.FailWrites = true;
    model.Settings.EditorConsole = true;
    _rig.Files.FailWrites = false;
    var message = model.ToastMessage;
    Assert.NotNull(message);

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Single(model.Templates.Templates);
    Assert.Equal(message, model.ToastMessage);
  }

  [Fact]
  public void TheTemplatesToastStaysWhileTheNewFolderHasNoTemplatesEither()
  {
    var model = NewMain();
    _rig.AddEngine(LauncherRig.OtherDir);
    ShowToast(model);

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Empty(model.Templates.Templates);
    Assert.Contains("No project templates", model.ToastMessage);
  }

  [Fact]
  public void AToastDismissesItselfAfterTheTimeout()
  {
    WithoutContext(() =>
    {
      var (model, delay) = NewToastMain();
      ShowToast(model);

      var call = Assert.Single(delay.Calls);
      Assert.Equal(TimeSpan.FromSeconds(15), call.Span);
      Assert.True(model.HasToast);

      delay.Complete(0);

      Assert.False(model.HasToast);
      Assert.False(model.HasToastLog);
    });
  }

  [Fact]
  public void AnOlderTimerDoesNotDismissANewerToast()
  {
    WithoutContext(() =>
    {
      var (model, delay) = NewToastMain();
      ShowToast(model);
      ShowToast(model);
      Assert.Equal(2, delay.Calls.Count);
      Assert.True(delay.Calls[0].Token.IsCancellationRequested);
      Assert.False(delay.Calls[1].Token.IsCancellationRequested);

      delay.Complete(0);

      Assert.True(model.HasToast);

      delay.Complete(1);

      Assert.False(model.HasToast);
    });
  }

  [Fact]
  public void ADismissedToastsTimerCannotDismissTheNextToast()
  {
    WithoutContext(() =>
    {
      var (model, delay) = NewToastMain();
      ShowToast(model);
      model.DismissToastCommand.Execute(null);
      Assert.True(delay.Calls[0].Token.IsCancellationRequested);
      ShowToast(model);

      delay.Complete(0);

      Assert.True(model.HasToast);
    });
  }

  [Fact]
  public void ADismissedToastStaysDismissedAndItsTimerIsCancelled()
  {
    WithoutContext(() =>
    {
      var (model, delay) = NewToastMain();
      ShowToast(model);

      model.DismissToastCommand.Execute(null);

      Assert.False(model.HasToast);
      Assert.True(delay.Calls[0].Token.IsCancellationRequested);
    });
  }

  [Fact]
  public void TheBannerSettingsButtonIsHiddenOnlyOnTheSettingsTab()
  {
    var model = NewMain();
    var log = new PropertyLog(model);
    Assert.True(model.ShowBannerSettingsButton);

    model.ActiveNav = "settings";

    Assert.False(model.ShowBannerSettingsButton);
    Assert.Contains(nameof(MainWindowViewModel.ShowBannerSettingsButton), log.Names);

    model.ActiveNav = "samples";

    Assert.True(model.ShowBannerSettingsButton);
  }

  [Fact]
  public void TheNewerEngineToastSurvivesAReloadThatFindsOnlyGatedTemplates()
  {
    AddTemplate("future", "Future", minEngine: "9.0");
    var model = NewMain();
    _rig.AddEngine(LauncherRig.OtherDir);
    AddTemplate("later", "Later", minEngine: "9.0", templatesFolder: Path.Combine(LauncherRig.OtherDir, "templates"));
    ShowToast(model);
    Assert.Contains("newer engine", model.ToastMessage);

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Single(model.Templates.Templates);
    Assert.Contains("newer engine", model.ToastMessage);
  }

  [Fact]
  public void TheBannerSettingsButtonAndTheToastAreWiredInTheWindow()
  {
    var window = XDocument.Load(Path.Combine(
      typeof(LauncherToastTests).Assembly.GetCustomAttributes<AssemblyMetadataAttribute>()
        .Single(a => a.Key == "ECS3DSourceRoot").Value!,
      "apps", "launcher", "source", "Shell", "MainWindow.axaml")).Root!;
    var toast = window.Descendants()
      .Single(e => e.Name.LocalName == "Border" && (string?)e.Attribute("IsVisible") == "{Binding HasToast}");
    var button = window.Descendants()
      .Single(e => e.Name.LocalName == "Button" && (string?)e.Attribute("Command") == "{Binding OpenSettingsCommand}"
        && e.Attribute("IsVisible") is not null);

    Assert.Equal("Assertive", (string?)toast.Attribute("AutomationProperties.LiveSetting"));
    Assert.Equal("{Binding ShowBannerSettingsButton}", (string?)button.Attribute("IsVisible"));
  }
}
