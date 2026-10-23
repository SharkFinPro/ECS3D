using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Headless;
using ECS3DLauncher.Shared.Controls;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherCardMenuTests
{
  private static Task OnUiThread(System.Action action) =>
    HeadlessUnitTestSession.GetOrStartForAssembly(typeof(LauncherCardMenuTests).Assembly)
      .Dispatch(action, CancellationToken.None);

  private static (Button Button, ContextMenu Menu, Window Window) ShowCard()
  {
    var button = new Button();
    var menu = new ContextMenu { Items = { new MenuItem { Header = "Open" } } };
    var card = new Border { ContextMenu = menu, Child = new StackPanel { Children = { button } } };
    var window = new Window { Content = card };
    window.Show();
    return (button, menu, window);
  }

  [Fact]
  public async Task MoreButtonOpensTheCardMenu()
  {
    await OnUiThread(() =>
    {
      var (button, menu, window) = ShowCard();

      Assert.True(CardMenu.OpenFrom(button));
      Assert.True(menu.IsOpen);

      menu.Close();
      window.Close();
    });
  }

  [Fact]
  public async Task OpeningFromTheButtonLeavesRightClickPlacementAlone()
  {
    await OnUiThread(() =>
    {
      var (button, menu, window) = ShowCard();

      Assert.True(CardMenu.OpenFrom(button));
      menu.Close();

      Assert.Null(menu.PlacementTarget);
      Assert.Equal(PlacementMode.Pointer, menu.Placement);
      window.Close();
    });
  }

  [Fact]
  public async Task AControlWithNoMenuAboveItOpensNothing()
  {
    await OnUiThread(() =>
    {
      var (withMenu, menu, menuWindow) = ShowCard();
      var bare = new Button();
      var bareWindow = new Window { Content = new StackPanel { Children = { bare } } };
      bareWindow.Show();

      Assert.True(CardMenu.OpenFrom(withMenu));
      Assert.True(menu.IsOpen);
      Assert.False(CardMenu.OpenFrom(bare));

      menu.Close();
      menuWindow.Close();
      bareWindow.Close();
    });
  }
}
