using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.VisualTree;
using ECS3DLauncher.Shared.Controls;
using ECS3DLauncher.Shared.Models;

namespace ECS3DLauncher.Tabs.Projects;

public class ProjectsView : UserControl
{
    public ProjectsView()
    {
        AvaloniaXamlLoader.Load(this);
        AddHandler(DoubleTappedEvent, OnCardDoubleTapped);
    }

    // The ... button opens the same menu as a right-click on its card.
    private void OnMoreClick(object? sender, RoutedEventArgs e)
    {
        if (sender is Control button)
        {
            CardMenu.OpenFrom(button);
        }
    }

    // A double-click on a card opens it; a double-click on a button inside it is the button's own.
    private void OnCardDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (e.Source is Visual source && source.FindAncestorOfType<Button>(true) is null &&
            source.DataContext is Project project &&
            DataContext is ProjectsViewModel viewModel)
        {
            viewModel.OpenProjectCommand.Execute(project);
        }
    }
}
