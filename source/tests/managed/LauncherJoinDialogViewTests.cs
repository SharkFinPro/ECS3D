using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Automation.Peers;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

// The Join dialog is composed as MainWindow composes it: a ContentControl in a dimming overlay that sits after the
// scrolling content and opens on a view model. Windows UI Automation reads the peers of whatever has focus when
// a screen reader asks, so a peer created while the dialog is still attaching must not leave the tree inconsistent.
[Collection("AvaloniaHeadless")]
public class LauncherJoinDialogViewTests
{
  private const int PeerLimit = 500;

  private sealed class OverlayWindow
  {
    public Window Window { get; }

    public Border Overlay { get; }

    public ContentControl Host { get; }

    public int FocusPeers { get; private set; }

    public OverlayWindow()
    {
      LauncherNewProjectDialogViewTests.EnsureThemeResources();
      var content = new ScrollViewer { Content = new StackPanel { Children = { new Button { Content = "Card" } } } };
      Host = new ContentControl { HorizontalAlignment = HorizontalAlignment.Center };
      Host.DataTemplates.Add(new FuncDataTemplate<JoinDialogViewModel>((_, _) => new JoinDialog()));
      Overlay = new Border { Background = Brushes.Black, IsVisible = false, Child = Host };
      var root = new Grid { Children = { new Grid { IsEnabled = false, Children = { content } }, Overlay } };
      Window = LauncherCardMenuTests.ShowInWindow(root, root);
      // What the Windows provider does when focus moves: build the focused control's peer and read each
      // ancestor's children to place it.
      Window.AddHandler(InputElement.GotFocusEvent, (_, e) =>
      {
        FocusPeers++;
        for (var peer = ControlAutomationPeer.CreatePeerForElement((Control)e.Source!);
             peer is not null;
             peer = peer.GetParent())
        {
          peer.GetChildren();
        }
      });
    }

    public void Open()
    {
      Overlay.IsVisible = true;
      Host.Content = new JoinDialogViewModel(null, 3000, _ => { }, () => { });
      Dispatcher.UIThread.RunJobs();
    }
  }

  // Walks the way a UI Automation client does and records anything it could not follow.
  private static (List<AutomationPeer> Peers, List<string> Faults) Walk(AutomationPeer root)
  {
    var peers = new List<AutomationPeer>();
    var faults = new List<string>();
    var pending = new Queue<AutomationPeer>();
    pending.Enqueue(root);
    while (pending.Count > 0 && peers.Count < PeerLimit)
    {
      var peer = pending.Dequeue();
      if (peers.Contains(peer))
      {
        faults.Add("visited twice: " + Describe(peer));
        continue;
      }

      peers.Add(peer);
      var children = peer.GetChildren();
      foreach (var child in children)
      {
        if (children.Count(other => ReferenceEquals(other, child)) != 1)
        {
          faults.Add("listed more than once: " + Describe(child));
        }

        if (!ReferenceEquals(child.GetParent(), peer))
        {
          faults.Add("parent does not list it: " + Describe(child));
        }

        pending.Enqueue(child);
      }
    }

    if (pending.Count > 0)
    {
      faults.Add("walk did not end within " + PeerLimit + " peers");
    }

    return (peers, faults);
  }

  private static string Describe(AutomationPeer peer) => peer.GetClassName() + " '" + peer.GetName() + "'";

  private static IEnumerable<AutomationPeer> Named(IEnumerable<AutomationPeer> peers, string className, string name) =>
    peers.Where(peer => peer.GetClassName() == className && peer.GetName() == name);

  [Fact]
  public async Task OpeningTheDialogLeavesAConsistentAutomationTree()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var shown = new OverlayWindow();
      var rootPeer = ControlAutomationPeer.CreatePeerForElement(shown.Window);
      Assert.Empty(Walk(rootPeer).Faults);

      shown.Open();

      var (peers, faults) = Walk(rootPeer);
      Assert.Empty(faults);
      Assert.True(shown.FocusPeers > 0, "the address box took focus, so the provider built its peer");
      Assert.Equal(2, peers.Count(peer => peer.GetClassName() == "TextBox"));
      Assert.Single(Named(peers, "Button", "Cancel"));
      Assert.Single(Named(peers, "Button", "Join"));
      Assert.Single(Named(peers, "Button", "Card"));
      shown.Window.Close();
    });
  }

  [Fact]
  public async Task TheAddressBoxHasFocusOnceTheDialogIsOpen()
  {
    await LauncherCardMenuTests.OnUiThread(() =>
    {
      var shown = new OverlayWindow();

      shown.Open();

      var box = Assert.IsType<TextBox>(shown.Window.FocusManager!.GetFocusedElement());
      Assert.Equal("AddressBox", box.Name);
      shown.Window.Close();
    });
  }
}
