using System;
using System.Collections.ObjectModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;

namespace ECS3DLauncher.Tabs.Learn;

// State for the Learn tab: the guide cards filtered by the shell's search, and the "What's in the box" banner.
public sealed class LearnViewModel : ObservableObject
{
    // The shell owns navigation and the OS browser call; the tab only asks it.
    private readonly Action<string> _openGuide;
    private string _searchText = "";
    private string? _exploreTooltip;

    public ObservableCollection<LearnGuide> Guides { get; } = new(LearnCatalog.Guides);

    public bool HasNoMatches => Guides.Count == 0;

    public string BoxSummary => LearnCatalog.BoxSummary;

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                RebuildGuides();
            }
        }
    }

    public string? ExploreTooltip
    {
        get => _exploreTooltip;
        private set => SetProperty(ref _exploreTooltip, value);
    }

    public IRelayCommand<LearnGuide> OpenGuideCommand { get; }
    public IRelayCommand GoToSamplesCommand { get; }
    public IRelayCommand ExploreCommand { get; }

    public LearnViewModel(
        Action<string> openGuide,
        Action openSamples,
        IRelayCommand exploreCommand,
        string? exploreTooltip)
    {
        _openGuide = openGuide;
        ExploreCommand = exploreCommand;
        ExploreTooltip = exploreTooltip;
        OpenGuideCommand = new RelayCommand<LearnGuide>(guide =>
        {
            if (guide is not null)
            {
                _openGuide(guide.Url);
            }
        });
        GoToSamplesCommand = new RelayCommand(openSamples);
    }

    public void UpdateTooltip(string? exploreTooltip) => ExploreTooltip = exploreTooltip;

    private void RebuildGuides()
    {
        Guides.Clear();
        foreach (var guide in LearnCatalog.Guides.Where(item => SearchMatch.Matches(_searchText, item.Title, item.Summary)))
        {
            Guides.Add(guide);
        }

        OnPropertyChanged(nameof(HasNoMatches));
    }
}
