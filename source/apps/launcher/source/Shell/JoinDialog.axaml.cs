using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;

namespace ECS3DLauncher.Shell;

public class JoinDialog : UserControl
{
    public JoinDialog()
    {
        AvaloniaXamlLoader.Load(this);
        var addressBox = this.FindControl<TextBox>("AddressBox");
        // Focus moves after the attach finishes: UI Automation builds the focused control's peers at once, and
        // peers built mid-attach lose their parent link and break sibling navigation for the whole window.
        AttachedToVisualTree += (_, _) => Dispatcher.UIThread.Post(() => addressBox?.Focus());
    }
}
