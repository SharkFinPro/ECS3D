using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;

namespace ECS3DLauncher.Tabs.Templates;

public class NewProjectDialog : UserControl
{
    public NewProjectDialog()
    {
        AvaloniaXamlLoader.Load(this);
        var nameBox = this.FindControl<TextBox>("NameBox");
        AttachedToVisualTree += (_, _) => Dispatcher.UIThread.Post(() =>
        {
            nameBox?.Focus();
            nameBox?.SelectAll();
        });
    }
}
