using System;
using Avalonia.Automation.Peers;
using Avalonia.Automation.Provider;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.LogicalTree;

namespace ECS3DLauncher.Shared.Controls;

// Avalonia's menu item peer offers no invoke pattern, so assistive technology can list a context menu's entries
// but not activate one. This item adds the pattern and runs the same click path a mouse click does.
public class InvokableMenuItem : MenuItem
{
    protected override Type StyleKeyOverride => typeof(MenuItem);

    protected override AutomationPeer OnCreateAutomationPeer() => new InvokableMenuItemAutomationPeer(this);
}

public class InvokableMenuItemAutomationPeer : MenuItemAutomationPeer, IInvokeProvider
{
    private readonly MenuItem _item;

    public InvokableMenuItemAutomationPeer(MenuItem item)
        : base(item)
    {
        _item = item;
    }

    public void Invoke()
    {
        if (_item.IsEffectivelyEnabled)
        {
            _item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
            if (!_item.StaysOpenOnClick)
            {
                _item.FindLogicalAncestorOfType<ContextMenu>()?.Close();
            }
        }
    }

    protected override object? GetProviderCore(Type providerType) =>
        providerType == typeof(IInvokeProvider) ? this : base.GetProviderCore(providerType);
}
