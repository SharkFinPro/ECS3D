using System;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;

namespace ECS3DLauncher.Tabs.Templates;

// State for the Templates tab: one card per shipped template, narrowed by the header search.
public sealed class TemplatesViewModel : ObservableObject
{
    private readonly string _launcherVersion;
    private readonly Func<string, Bitmap?> _loadThumbnail;
    private string _searchText = "";
    private string _emptyHeading = "";

    // Every template the catalog found.
    public ObservableCollection<TemplateCard> Templates { get; } = [];

    // The templates matching the search, in catalog order.
    public ObservableCollection<TemplateCard> VisibleTemplates { get; } = [];

    public bool IsEmpty => Templates.Count == 0;

    public bool HasNoMatches => !IsEmpty && VisibleTemplates.Count == 0;

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

    public TemplatesViewModel(string launcherVersion, Func<string, Bitmap?> loadThumbnail)
    {
        _launcherVersion = launcherVersion;
        _loadThumbnail = loadThumbnail;
    }

    public void Reload(TemplateCatalogContents contents)
    {
        Templates.Clear();
        foreach (var entry in contents.Templates)
        {
            var manifest = entry.Manifest;
            Templates.Add(new TemplateCard
            {
                Id = manifest.Id,
                Name = manifest.Name,
                Description = manifest.Description,
                Tags = manifest.Tags,
                FolderPath = entry.FolderPath,
                PlayersLabel = manifest.PlayersLabel,
                Thumbnail = entry.ThumbnailPath is { } path ? _loadThumbnail(path) : null,
                NeedsNewerEngine = TemplateCompatibility.NeedsNewerEngine(manifest.MinEngineVersion, _launcherVersion),
            });
        }

        EmptyHeading = HeadingFor(contents);
        Refilter();
    }

    private static string HeadingFor(TemplateCatalogContents contents)
    {
        if (contents.Templates.Count > 0)
        {
            return "No templates match your search";
        }

        if (contents.Unreadable)
        {
            return $"Could not read {contents.TemplatesFolder}";
        }

        return contents.FolderFound
            ? $"No templates found in {contents.TemplatesFolder}"
            : $"No templates folder found at {contents.TemplatesFolder}";
    }

    private void Refilter()
    {
        VisibleTemplates.Clear();
        foreach (var card in Templates.Where(item => Matches(item)))
        {
            VisibleTemplates.Add(card);
        }

        OnPropertyChanged(nameof(IsEmpty));
        OnPropertyChanged(nameof(HasNoMatches));
        OnPropertyChanged(nameof(ShowEmptyState));
    }

    private bool Matches(TemplateCard card) =>
        SearchMatch.Matches(_searchText, [card.Name, card.Description, .. card.Tags]);
}
