using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;
using ECS3DLauncher.Tabs.Samples;
using ECS3DLauncher.Tabs.Templates;

namespace ECS3DLauncher.Tabs.Library;

// State for the Library tab: template cards, then the Welcome scene and the sample rows, all narrowed by one search.
public sealed class LibraryViewModel : ObservableObject
{
    private const string WelcomeNameText = "Welcome scene";
    private const string WelcomeDescriptionText =
        "The engine's built-in tour. Open it in the editor and look around before making anything.";
    private const string WelcomeTagText = "tour";

    // Not shown on the row: the Projects first-run card and Learn call this entry Explore, so a search for that finds it.
    private const string WelcomeKeyword = "explore";

    private string _searchText = "";
    private string? _exploreTooltip;

    public string WelcomeName => WelcomeNameText;

    public string WelcomeDescription => WelcomeDescriptionText;

    public string WelcomeTag => WelcomeTagText;

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

    // One query for every section; the shell sets it and the children refilter.
    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                Templates.SearchText = value;
                Samples.SearchText = value;
                RaiseSections();
            }
        }
    }

    public bool ShowWelcome => SearchMatch.Matches(_searchText, WelcomeNameText, WelcomeDescriptionText, WelcomeTagText, WelcomeKeyword);

    // An empty catalog keeps the section for its explanation, unless a search is narrowing the page.
    public bool ShowTemplatesSection =>
        Templates.IsEmpty ? string.IsNullOrWhiteSpace(_searchText) : Templates.VisibleTemplates.Count > 0;

    public bool ShowExamplesSection => ShowWelcome || Samples.VisibleSamples.Count > 0;

    public bool ShowNoResults => !ShowTemplatesSection && !ShowExamplesSection;

    public LibraryViewModel(
        TemplatesViewModel templates, SamplesViewModel samples, IRelayCommand exploreCommand, string? exploreTooltip)
    {
        Templates = templates;
        Samples = samples;
        ExploreCommand = exploreCommand;
        _exploreTooltip = exploreTooltip;
        Templates.PropertyChanged += OnChildChanged;
        Samples.PropertyChanged += OnChildChanged;
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
            RaiseSections();
        }
    }

    private void RaiseSections()
    {
        OnPropertyChanged(nameof(ShowWelcome));
        OnPropertyChanged(nameof(ShowTemplatesSection));
        OnPropertyChanged(nameof(ShowExamplesSection));
        OnPropertyChanged(nameof(ShowNoResults));
    }
}
