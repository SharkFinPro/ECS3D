using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.VisualTree;
using ECS3DLauncher.Tabs.Templates;
using Xunit;

namespace ECS3DManagedTests;

// Drives the real dialog XAML in a headless window, so the bindings are exercised through the controls.
[Collection("AvaloniaHeadless")]
public class LauncherNewProjectDialogViewTests : NewProjectFixture
{
  private const string NameRequired = "Enter a project name.";

  // The StaticResource keys the dialog uses come from the launcher theme, which the test project does not link.
  private static void EnsureThemeResources()
  {
    var resources = Application.Current!.Resources;
    foreach (var key in new[] { "PanelBrush", "LineBrush", "T2Brush", "T3Brush", "DangerBrush", "OkBrush", "AccText" })
    {
      if (!resources.ContainsKey(key))
      {
        resources[key] = Brushes.Gray;
      }
    }

    if (!resources.ContainsKey("MonoFontFamily"))
    {
      resources["MonoFontFamily"] = FontFamily.Default;
    }

    if (!resources.ContainsKey("BareButton"))
    {
      resources["BareButton"] = new ControlTheme(typeof(Button));
    }
  }

  private static IEnumerable<T> All<T>(Visual root) where T : Visual => root.GetVisualDescendants().OfType<T>();

  private static Button ButtonOf(Visual root, ICommand command) =>
    All<Button>(root).Single(button => ReferenceEquals(button.Command, command));

  private static bool IsShown(TextBlock block, string cssClass) =>
    block.Classes.Contains(cssClass) && block.IsEffectivelyVisible;

  private sealed class ShownDialog : IDisposable
  {
    private readonly Window _window;

    public NewProjectDialog Dialog { get; }

    public void PressEnter() => _window.KeyPress(Key.Enter, RawInputModifiers.None, PhysicalKey.Enter, null);

    public ShownDialog(NewProjectViewModel vm)
    {
      EnsureThemeResources();
      Dialog = new NewProjectDialog { DataContext = vm };
      _window = LauncherCardMenuTests.ShowInWindow(Dialog, Dialog);
    }

    public TextBox NameBox => Dialog.FindControl<TextBox>("NameBox")!;

    public TextBlock NameError => Dialog.FindControl<TextBlock>("NameErrorText")!;

    public TextBox LocationBox => All<TextBox>(Dialog).Single(box => box.Name != "NameBox");

    // The message under the location row, which has no name of its own.
    public TextBlock LocationError
    {
      get
      {
        var row = (Panel)LocationBox.GetVisualParent()!;
        var panel = (Panel)row.GetVisualParent()!;
        return (TextBlock)panel.Children[panel.Children.IndexOf(row) + 1];
      }
    }

    public void SetRenderScaling(double scaling) => _window.SetRenderScaling(scaling);

    public void Dispose() => _window.Close();
  }

  private static ShownDialog Show(NewProjectViewModel vm)
  {
    var shown = new ShownDialog(vm);
    Assert.True(shown.Dialog.IsAttachedToVisualTree());
    Assert.NotNull(shown.NameBox);
    return shown;
  }

  [Fact]
  public async Task TypingANameUpdatesTheViewModelAndThePreview()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var vm = NewDialog();
      using var shown = Show(vm);
      var dialog = shown.Dialog;
      Assert.Equal("First-Person", shown.NameBox.Text);

      shown.NameBox.Text = "Game";

      var expected = Path.Combine(Projects, "Game", "project.ecs3d");
      Assert.Equal("Game", vm.ProjectName);
      Assert.Contains(All<TextBlock>(dialog), block => block.Text == expected);
      Assert.Contains(All<TextBlock>(dialog), block => IsShown(block, "ok"));
    });
  }

  [Fact]
  public async Task ANameThatIsTakenShowsTheTargetProblemInsteadOfTheOkMessage()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      _files.Seed(Path.Combine(Folder("Taken"), "project.ecs3d"), "{}");
      using var shown = Show(NewDialog());
      var dialog = shown.Dialog;

      shown.NameBox.Text = "Taken";

      Assert.DoesNotContain(All<TextBlock>(dialog), block => IsShown(block, "ok"));
      var problem = All<TextBlock>(dialog).Single(block =>
        IsShown(block, "error") && block.Text?.Contains("already exists") == true);
      Assert.False(string.IsNullOrEmpty(problem.Text));

      shown.NameBox.Text = "Free";

      Assert.Contains(All<TextBlock>(dialog), block => IsShown(block, "ok"));
      Assert.DoesNotContain(All<TextBlock>(dialog), block =>
        IsShown(block, "error") && block.Text?.Contains("already exists") == true);
    });
  }

  [Fact]
  public async Task AnInvalidNameShowsItsReasonAndDisablesCreate()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var vm = NewDialog();
      using var shown = Show(vm);
      var dialog = shown.Dialog;
      var create = ButtonOf(dialog, vm.CreateCommand);
      var preview = Path.Combine(Projects, "First-Person", "project.ecs3d");
      Assert.True(create.IsEffectivelyEnabled);
      Assert.Contains(All<TextBlock>(dialog), block => block.Text == preview);
      Assert.True(shown.NameError.Classes.Contains("error"));
      Assert.False(shown.NameError.IsEffectivelyVisible);

      shown.NameBox.Text = "";

      Assert.True(shown.NameError.IsEffectivelyVisible);
      Assert.Equal(NameRequired, shown.NameError.Text);
      Assert.False(create.IsEffectivelyEnabled);
      Assert.DoesNotContain(All<TextBlock>(dialog), block => block.Text == preview);

      shown.NameBox.Text = "Fine";

      Assert.False(shown.NameError.IsEffectivelyVisible);
      Assert.True(create.IsEffectivelyEnabled);
    });
  }

  [Fact]
  public async Task AnInvalidLocationShowsItsReason()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var vm = NewDialog();
      using var shown = Show(vm);
      var create = ButtonOf(shown.Dialog, vm.CreateCommand);
      Assert.False(shown.LocationError.IsEffectivelyVisible);
      Assert.True(create.IsEffectivelyEnabled);

      shown.LocationBox.Text = "";

      Assert.True(shown.LocationError.Classes.Contains("error"));
      Assert.True(shown.LocationError.IsEffectivelyVisible);
      Assert.Equal("Enter a location.", shown.LocationError.Text);
      Assert.False(create.IsEffectivelyEnabled);

      shown.LocationBox.Text = Projects;

      Assert.False(shown.LocationError.IsEffectivelyVisible);
      Assert.True(create.IsEffectivelyEnabled);
    });
  }

  // Avalonia drops a wrapped line that does not fit the arranged height. A font-derived line height such as 15.6 does
  // not survive rounding at a display scale of 125%, so the last line of a long error vanished while its space stayed.
  // The stub font of headless mode cannot show that, so the wrapped text carries a line height of its own.
  [Fact]
  public async Task WrappedTextHasALineHeightOfItsOwnAndAFailedCreateShowsAllOfItsMessage()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      const string message = "Could not create the project: The network path was not found. : '\\\\nosuchhost\\share\\x'";
      var vm = NewDialog();
      using var shown = Show(vm);
      shown.SetRenderScaling(1.25);
      typeof(NewProjectViewModel).GetProperty(nameof(NewProjectViewModel.CreateError))!.SetValue(vm, message);

      shown.Dialog.UpdateLayout();

      var wrapped = All<TextBlock>(shown.Dialog).Where(block => block.TextWrapping == TextWrapping.Wrap).ToList();
      Assert.True(wrapped.Count >= 4);
      Assert.All(wrapped, block => Assert.True(double.IsFinite(block.LineHeight) && block.LineHeight > 0, block.Text));
      var error = wrapped.Single(block => block.Text == message);
      Assert.True(error.IsEffectivelyVisible);
      Assert.True(error.TextLayout.TextLines.Count > 1);
      Assert.True(error.TextLayout.TextLines.Sum(line => line.Length) >= message.Length);
    });
  }

  [Fact]
  public async Task TheOpenInEditorCheckBoxBindsBothWays()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var vm = NewDialog();
      using var shown = Show(vm);
      var box = All<CheckBox>(shown.Dialog).Single();
      Assert.True(box.IsChecked);

      box.IsChecked = false;
      Assert.False(vm.OpenInEditor);

      vm.OpenInEditor = true;
      Assert.True(box.IsChecked);
    });
  }

  [Fact]
  public async Task WhileCreatingTheInputsAndButtonsAreDisabled()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      Action? pending = null;
      var vm = NewDialog(background: work => pending = work);
      using var shown = Show(vm);
      var dialog = shown.Dialog;
      var box = All<CheckBox>(dialog).Single();
      var create = ButtonOf(dialog, vm.CreateCommand);
      var cancel = ButtonOf(dialog, vm.CancelCommand);
      var progress = All<ProgressBar>(dialog).Single();
      Assert.True(shown.NameBox.IsEffectivelyEnabled);
      Assert.True(cancel.IsEffectivelyEnabled);
      Assert.False(progress.IsEffectivelyVisible);

      create.Command!.Execute(null);

      Assert.NotNull(pending);
      Assert.False(shown.NameBox.IsEffectivelyEnabled);
      Assert.False(box.IsEffectivelyEnabled);
      Assert.False(create.IsEffectivelyEnabled);
      Assert.False(cancel.IsEffectivelyEnabled);
      Assert.True(progress.IsEffectivelyVisible);

      pending!();
    });
  }

  [Fact]
  public async Task EnterInTheNameBoxCreatesTheProject()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var created = 0;
      var vm = NewDialog(background: work => created++);
      using var shown = Show(vm);
      shown.NameBox.Focus();

      shown.PressEnter();

      Assert.Equal(1, created);
      Assert.True(vm.IsBusy);
    });
  }

  [Fact]
  public async Task EnterDoesNothingWhileCreateIsDisabled()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var created = 0;
      var vm = NewDialog(background: work => created++);
      using var shown = Show(vm);
      shown.NameBox.Focus();
      shown.NameBox.Text = "";
      Assert.False(ButtonOf(shown.Dialog, vm.CreateCommand).IsEffectivelyEnabled);

      shown.PressEnter();

      Assert.Equal(0, created);
      Assert.False(vm.IsBusy);
    });
  }
}
