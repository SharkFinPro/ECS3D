using System;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia.Automation.Peers;
using Avalonia.Automation.Provider;
using Avalonia.Controls;
using ECS3DLauncher.Shared.Controls;
using Xunit;

namespace ECS3DManagedTests;

[Collection("AvaloniaHeadless")]
public class LauncherInvokableMenuItemTests
{
  private sealed class CountingCommand : ICommand
  {
    public int Runs { get; private set; }

    public object? LastParameter { get; private set; }

    public bool Enabled { get; init; } = true;

    public event EventHandler? CanExecuteChanged
    {
      add { }
      remove { }
    }

    public bool CanExecute(object? parameter) => Enabled;

    public void Execute(object? parameter)
    {
      Runs++;
      LastParameter = parameter;
    }
  }

  private static IInvokeProvider? InvokeProviderOf(Control item) =>
    ControlAutomationPeer.CreatePeerForElement(item).GetProvider<IInvokeProvider>();

  private static (ContextMenu Menu, Window Window) Show(params MenuItem[] items)
  {
    var menu = new ContextMenu();
    foreach (var item in items)
    {
      menu.Items.Add(item);
    }

    var anchor = new Button();
    var owner = new Border { ContextMenu = menu, Child = anchor };
    var window = LauncherCardMenuTests.ShowInWindow(owner, anchor);
    menu.Open(owner);
    return (menu, window);
  }

  // The reason this class exists: a stock MenuItem has no invoke pattern.
  [Fact]
  public async Task AStockMenuItemCannotBeInvokedByAssistiveTechnology()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var (_, window) = Show(new MenuItem { Header = "Stock" });
      Assert.Null(InvokeProviderOf(new MenuItem { Header = "Stock" }));
      window.Close();
    });
  }

  [Fact]
  public async Task InvokingRunsTheCommandOnceWithItsParameter()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var command = new CountingCommand();
      var item = new InvokableMenuItem { Header = "Pin to top", Command = command, CommandParameter = "card" };
      var (menu, window) = Show(item);

      InvokeProviderOf(item)!.Invoke();

      Assert.Equal(1, command.Runs);
      Assert.Equal("card", command.LastParameter);
      Assert.False(menu.IsOpen);
      window.Close();
    });
  }

  [Fact]
  public async Task InvokingRaisesTheClickEvent()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var clicks = 0;
      var item = new InvokableMenuItem { Header = "Click" };
      item.Click += (_, _) => clicks++;
      var (_, window) = Show(item);

      InvokeProviderOf(item)!.Invoke();

      Assert.Equal(1, clicks);
      window.Close();
    });
  }

  [Fact]
  public async Task ADisabledItemDoesNothingWhenInvoked()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var command = new CountingCommand { Enabled = false };
      var item = new InvokableMenuItem { Header = "Play", Command = command };
      var (_, window) = Show(item);

      InvokeProviderOf(item)!.Invoke();

      Assert.Equal(0, command.Runs);
      window.Close();
    });
  }

  [Fact]
  public async Task TheItemIsNamedByItsHeader()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var item = new InvokableMenuItem { Header = "Remove from list" };
      var (_, window) = Show(item);

      Assert.Equal("Remove from list", ControlAutomationPeer.CreatePeerForElement(item).GetName());
      window.Close();
    });
  }
}
