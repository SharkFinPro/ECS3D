using System;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;

namespace ECS3DLauncher.Tabs.Samples;

// State for the Samples tab: one card per shipped sample, narrowed by the header search.
public class SamplesViewModel : ObservableObject
{
    private readonly Func<string, Bitmap?> _loadThumbnail;
    private string _searchText = "";
    private string? _playTooltip;
    private string _emptyHeading = "";

    // Every sample the catalog found.
    public ObservableCollection<SampleCard> Samples { get; } = [];

    // The samples matching the search, in catalog order.
    public ObservableCollection<SampleCard> VisibleSamples { get; } = [];

    public bool IsEmpty => Samples.Count == 0;

    public bool HasNoMatches => !IsEmpty && VisibleSamples.Count == 0;

    public bool ShowEmptyState => IsEmpty || HasNoMatches;

    public string EmptyHeading
    {
        get => _emptyHeading;
        private set => SetProperty(ref _emptyHeading, value);
    }

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                Refilter();
            }
        }
    }

    public string? PlayTooltip
    {
        get => _playTooltip;
        private set => SetProperty(ref _playTooltip, value);
    }

    public IRelayCommand<SampleCard> PlayCommand { get; }

    public SamplesViewModel(IRelayCommand<SampleCard> playCommand, string? playTooltip, Func<string, Bitmap?> loadThumbnail)
    {
        PlayCommand = playCommand;
        _playTooltip = playTooltip;
        _loadThumbnail = loadThumbnail;
        Reload(new TemplateCatalogContents("", false, [], []));
    }

    public void UpdatePlay(string? playTooltip) => PlayTooltip = playTooltip;

    public void Reload(TemplateCatalogContents contents)
    {
        Samples.Clear();
        foreach (var entry in contents.Samples)
        {
            Samples.Add(new SampleCard
            {
                Name = entry.Manifest.Name,
                Description = entry.Manifest.Description,
                Tags = entry.Manifest.Tags,
                FolderPath = entry.FolderPath,
                PlayersLabel = entry.Manifest.PlayersLabel,
                Thumbnail = entry.ThumbnailPath is { } path ? _loadThumbnail(path) : null,
            });
        }

        EmptyHeading = HeadingFor(contents);
        Refilter();
    }

    private static string HeadingFor(TemplateCatalogContents contents)
    {
        if (contents.Samples.Count > 0)
        {
            return "No samples match your search";
        }

        if (contents.Unreadable)
        {
            return $"Could not read {contents.TemplatesFolder}";
        }

        return contents.FolderFound
            ? $"No samples found in {contents.TemplatesFolder}"
            : $"No templates folder found at {contents.TemplatesFolder}";
    }

    private void Refilter()
    {
        VisibleSamples.Clear();
        foreach (var card in Samples.Where(item => Matches(item)))
        {
            VisibleSamples.Add(card);
        }

        OnPropertyChanged(nameof(IsEmpty));
        OnPropertyChanged(nameof(HasNoMatches));
        OnPropertyChanged(nameof(ShowEmptyState));
    }

    private bool Matches(SampleCard card) =>
        SearchMatch.Matches(_searchText, [card.Name, card.Description, .. card.Tags]);
}
