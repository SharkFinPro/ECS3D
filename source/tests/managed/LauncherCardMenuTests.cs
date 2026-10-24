using System;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Headless;
using ECS3DLauncher.Shared.Controls;
using Xunit;

namespace ECS3DManagedTests;

// The headless session swaps process-wide Avalonia state, so these tests run alone.
[CollectionDefinition("AvaloniaHeadless", DisableParallelization = true)]
public class AvaloniaHeadlessCollection
{
}

[Collection("AvaloniaHeadless")]
public class LauncherCardMenuTests
{
  internal static Task OnUiThread(Action action) =>
    HeadlessUnitTestSession.GetOrStartForAssembly(typeof(LauncherCardMenuTests).Assembly)
      .Dispatch(action, CancellationToken.None);

  // Without a theme a Window has no template, so its content would never join the visual tree. The layer
  // manager supplies the overlay layer the context menu's popup opens in, as a theme's window template does.
  internal static Window ShowInWindow(Control content, Control attached)
  {
    var window = new Window
    {
      Content = content,
      Template = new FuncControlTemplate<Window>((w, scope) =>
      {
        var presenter = new ContentPresenter { Name = "PART_ContentPresenter" }.RegisterInNameScope(scope);
        presenter.Bind(ContentPresenter.ContentProperty, w.GetObservable(ContentControl.ContentProperty));
        return new VisualLayerManager { Name = "PART_VisualLayerManager", Child = presenter }.RegisterInNameScope(scope);
      })
    };
    window.Show();
    Assert.NotNull(TopLevel.GetTopLevel(attached));
    return window;
  }

  private static (Button Button, ContextMenu Menu, Window Window) ShowCard()
  {
    var button = new Button();
    var menu = new ContextMenu { Items = { new MenuItem { Header = "Open" } } };
    var card = new Border { ContextMenu = menu, Child = new StackPanel { Children = { button } } };
    return (button, menu, ShowInWindow(card, button));
  }

  [Fact]
  public async Task MoreButtonOpensTheCardMenu()
  {
    await OnUiThread(() =>
    {
      var (button, menu, window) = ShowCard();

      Assert.Throws<ArgumentException>(() => menu.Open(button));
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
      Control? target = null;
      var placement = PlacementMode.Pointer;
      menu.Opened += (_, _) =>
      {
        target = menu.PlacementTarget;
        placement = menu.Placement;
      };

      Assert.True(CardMenu.OpenFrom(button));
      menu.Close();

      Assert.Same(button, target);
      Assert.Equal(PlacementMode.BottomEdgeAlignedRight, placement);
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
      var bareWindow = ShowInWindow(new StackPanel { Children = { bare } }, bare);

      Assert.True(CardMenu.OpenFrom(withMenu));
      Assert.True(menu.IsOpen);
      Assert.False(CardMenu.OpenFrom(bare));

      menu.Close();
      menuWindow.Close();
      bareWindow.Close();
    });
  }
}
