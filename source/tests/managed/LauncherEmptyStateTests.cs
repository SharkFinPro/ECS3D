using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.VisualTree;
using ECS3DLauncher.Shared.Controls;
using Xunit;

namespace ECS3DManagedTests;

// Drives the real EmptyState XAML: the line under the heading shows only when Detail is set.
[Collection("AvaloniaHeadless")]
public class LauncherEmptyStateTests
{
  private static void EnsureThemeResources()
  {
    var resources = Application.Current!.Resources;
    foreach (var key in new[] { "PanelBrush", "LineBrush", "T3Brush" })
    {
      if (!resources.ContainsKey(key))
      {
        resources[key] = Brushes.Gray;
      }
    }
  }

  [Fact]
  public async Task TheDetailLineShowsOnlyWhileDetailIsSet()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      EnsureThemeResources();
      var state = new EmptyState { Heading = "No templates match your search" };
      var window = LauncherCardMenuTests.ShowInWindow(state, state);

      var blocks = state.GetVisualDescendants().OfType<TextBlock>().ToArray();
      var detail = blocks.Single(block => block.Text != state.Heading);
      Assert.False(detail.IsEffectivelyVisible);

      state.Detail = "Nothing to show here yet";

      Assert.True(detail.IsEffectivelyVisible);
      Assert.Equal("Nothing to show here yet", detail.Text);

      state.Detail = null;

      Assert.False(detail.IsEffectivelyVisible);
      window.Close();
    });
  }
}
