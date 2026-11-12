using System.Collections.ObjectModel;
using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Tabs.Samples;
using ECS3DLauncher.Tabs.Templates;

namespace ECS3DLauncher.Tabs.Library;

// State for the Library tab: one flat list of the Welcome scene, then the templates, then the samples, all narrowed
// by one search.
public sealed class LibraryViewModel : ObservableObject
{
    private const string WelcomeName = "Welcome scene";
    private const string WelcomeDescription =
        "The engine's built-in tour. Open it in the editor and look around before making anything.";
    private const string WelcomeTag = "tour";

    // Not shown on the row: the Projects first-run card and Learn call this entry Explore, so a search for that finds it.
    private const string WelcomeKeyword = "explore";

    private string _searchText = "";
    private string? _exploreTooltip;

    // The rows that match the search, in display order.
    public ObservableCollection<LibraryItem> Items { get; } = [];

    public string NoResultsHeading => "No results for your search";

    public TemplatesViewModel Templates { get; }

    public SamplesViewModel Samples { get; }

    // The shell's Explore command: opens the editor on the built-in Welcome project.
    public IRelayCommand ExploreCommand { get; }

    public string? ExploreTooltip
    {
        get => _exploreTooltip;
        private set => SetProperty(ref _exploreTooltip, value);
    }

    // One query for the whole list; the shell sets it and the children refilter.
    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                Templates.SearchText = value;
                Samples.SearchText = value;
                Rebuild();
            }
        }
    }

    public bool ShowNoResults => Items.Count == 0;

    public LibraryViewModel(
        TemplatesViewModel templates, SamplesViewModel samples, IRelayCommand exploreCommand, string? exploreTooltip)
    {
        Templates = templates;
        Samples = samples;
        ExploreCommand = exploreCommand;
        _exploreTooltip = exploreTooltip;
        Templates.PropertyChanged += OnChildChanged;
        Samples.PropertyChanged += OnChildChanged;
        Rebuild();
    }

    public void UpdateTooltip(string? exploreTooltip) => ExploreTooltip = exploreTooltip;

    public void Reload(TemplateCatalogContents contents)
    {
        Templates.Reload(contents);
        Samples.Reload(contents);
    }

    // Each child ends a refilter with ShowEmptyState, so it marks the point where its visible items settled.
    private void OnChildChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName == nameof(TemplatesViewModel.ShowEmptyState))
        {
            Rebuild();
        }
    }

    private void Rebuild()
    {
        Items.Clear();
        if (SearchMatch.Matches(_searchText, WelcomeName, WelcomeDescription, WelcomeTag, WelcomeKeyword))
        {
            Items.Add(new LibraryItem
            {
                Kind = LibraryItemKind.Tour, Name = WelcomeName, Description = WelcomeDescription, Tags = [WelcomeTag],
            });
        }

        // An empty catalog keeps its explanation where the templates would be, unless a search is narrowing the list.
        if (Templates.IsEmpty && string.IsNullOrWhiteSpace(_searchText))
        {
            Items.Add(new LibraryItem
            {
                Kind = LibraryItemKind.Notice, Name = Templates.EmptyHeading, Description = Templates.EmptyDetail ?? "",
            });
        }

        foreach (var card in Templates.VisibleTemplates)
        {
            Items.Add(new LibraryItem
            {
                Kind = LibraryItemKind.Template, Name = card.Name, Description = card.Description, Tags = card.Tags,
                PlayersLabel = card.PlayersLabel, Thumbnail = card.Thumbnail, Template = card,
            });
        }

        foreach (var card in Samples.VisibleSamples)
        {
            Items.Add(new LibraryItem
            {
                Kind = LibraryItemKind.Sample, Name = card.Name, Description = card.Description, Tags = card.Tags,
                PlayersLabel = card.PlayersLabel, Thumbnail = card.Thumbnail, Sample = card,
            });
        }

        OnPropertyChanged(nameof(ShowNoResults));
    }
}
