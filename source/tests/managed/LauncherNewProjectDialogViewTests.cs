using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
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

  private static bool _resourcesAdded;

  // The dialog's StaticResource keys come from the launcher's Theme.axaml, which the test project does not link.
  private static void EnsureThemeResources()
  {
    if (_resourcesAdded)
    {
      return;
    }

    var resources = Application.Current!.Resources;
    foreach (var key in new[] { "PanelBrush", "LineBrush", "T2Brush", "T3Brush", "DangerBrush", "OkBrush", "AccText" })
    {
      resources[key] = Brushes.Gray;
    }

    resources["MonoFontFamily"] = FontFamily.Default;
    resources["BareButton"] = new ControlTheme(typeof(Button));
    _resourcesAdded = true;
  }

  private static IEnumerable<T> All<T>(Visual root) where T : Visual => root.GetVisualDescendants().OfType<T>();

  // The message under the name box; its text stays null while the name is valid.
  private static TextBlock NameErrorBlock(NewProjectDialog dialog)
  {
    var nameBox = dialog.FindControl<TextBox>("NameBox")!;
    var panel = (Panel)nameBox.GetVisualParent()!;
    return (TextBlock)panel.Children[panel.Children.IndexOf(nameBox) + 1];
  }

  private static Button ButtonOf(Visual root, ICommand command) =>
    All<Button>(root).Single(button => ReferenceEquals(button.Command, command));

  private static (NewProjectDialog Dialog, Window Window) Show(NewProjectViewModel vm)
  {
    EnsureThemeResources();
    var dialog = new NewProjectDialog { DataContext = vm };
    var window = LauncherCardMenuTests.ShowInWindow(dialog, dialog);
    Assert.True(dialog.IsAttachedToVisualTree());
    Assert.NotNull(dialog.FindControl<TextBox>("NameBox"));
    return (dialog, window);
  }

  [Fact]
  public async Task TypingANameUpdatesTheViewModelAndThePreview()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var vm = NewDialog();
      var (dialog, window) = Show(vm);
      var nameBox = dialog.FindControl<TextBox>("NameBox")!;
      Assert.Equal("First-Person", nameBox.Text);

      nameBox.Text = "Game";

      var expected = Path.Combine(Projects, "Game", "project.ecs3d");
      Assert.Equal("Game", vm.ProjectName);
      Assert.Contains(All<TextBlock>(dialog), block => block.Text == expected);
      Assert.Contains(All<TextBlock>(dialog), block => block.Classes.Contains("ok") && block.IsVisible);
      window.Close();
    });
  }

  [Fact]
  public async Task AnInvalidNameShowsItsReasonAndDisablesCreate()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var vm = NewDialog();
      var (dialog, window) = Show(vm);
      var nameBox = dialog.FindControl<TextBox>("NameBox")!;
      var create = ButtonOf(dialog, vm.CreateCommand);
      Assert.True(create.IsEnabled);
      Assert.False(NameErrorBlock(dialog).IsVisible);

      nameBox.Text = "";

      Assert.True(NameErrorBlock(dialog).IsVisible);
      Assert.Equal(NameRequired, NameErrorBlock(dialog).Text);
      Assert.False(create.IsEnabled);
      Assert.DoesNotContain(All<TextBlock>(dialog), block => block.Text?.EndsWith("project.ecs3d") == true);

      nameBox.Text = "Fine";

      Assert.False(NameErrorBlock(dialog).IsVisible);
      Assert.True(create.IsEnabled);
      window.Close();
    });
  }

  [Fact]
  public async Task TheOpenInEditorCheckBoxBindsBothWays()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var vm = NewDialog();
      var (dialog, window) = Show(vm);
      var box = All<CheckBox>(dialog).Single();
      Assert.True(box.IsChecked);

      box.IsChecked = false;
      Assert.False(vm.OpenInEditor);

      vm.OpenInEditor = true;
      Assert.True(box.IsChecked);
      window.Close();
    });
  }

  [Fact]
  public async Task WhileCreatingTheInputsAndButtonsAreDisabled()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      Action? pending = null;
      var vm = NewDialog(background: work => pending = work);
      var (dialog, window) = Show(vm);
      var nameBox = dialog.FindControl<TextBox>("NameBox")!;
      var box = All<CheckBox>(dialog).Single();
      var create = ButtonOf(dialog, vm.CreateCommand);
      var cancel = ButtonOf(dialog, vm.CancelCommand);
      var progress = All<ProgressBar>(dialog).Single();
      Assert.True(nameBox.IsEffectivelyEnabled);
      Assert.True(cancel.IsEnabled);
      Assert.False(progress.IsVisible);

      create.Command!.Execute(null);

      Assert.NotNull(pending);
      Assert.False(nameBox.IsEffectivelyEnabled);
      Assert.False(box.IsEffectivelyEnabled);
      Assert.False(create.IsEnabled);
      Assert.False(cancel.IsEnabled);
      Assert.True(progress.IsVisible);

      pending!();
      window.Close();
    });
  }
}
