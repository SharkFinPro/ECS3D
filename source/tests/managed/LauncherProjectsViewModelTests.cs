using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Tabs.Projects;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherProjectsViewModelTests
{
  private static Project NewProject(string name, string path) => new() { Name = name, Path = path, Edited = "today" };

  private static ProjectsViewModel NewModel(
    IEnumerable<Project>? projects = null,
    Action? openTemplates = null) =>
    new(
      openTemplates ?? (() => { }),
      new RelayCommand(() => { }),
      new RelayCommand<Project>(_ => { }),
      "explore tip",
      "open tip",
      projects ?? []);

  [Fact]
  public void TilesStartWithTheNewProjectSentinelThenTheProjectsInOrder()
  {
    var alpha = NewProject("Alpha", "/work/alpha");
    var beta = NewProject("Beta", "/work/beta");

    var model = NewModel([alpha, beta]);

    Assert.Equal(3, model.ProjectTiles.Count);
    Assert.True(model.ProjectTiles[0].IsNewCard);
    Assert.Same(alpha, model.ProjectTiles[1]);
    Assert.Same(beta, model.ProjectTiles[2]);
    Assert.False(model.IsEmpty);
    Assert.True(model.ShowGrid);
    Assert.False(model.ShowList);
  }

  [Fact]
  public void NoProjectsLeavesOnlyTheSentinelAndShowsNeitherPanel()
  {
    var model = NewModel();

    Assert.Single(model.ProjectTiles);
    Assert.True(model.ProjectTiles[0].IsNewCard);
    Assert.True(model.IsEmpty);
    Assert.False(model.ShowGrid);
    Assert.False(model.ShowList);
  }

  [Fact]
  public void SearchFiltersByNameAndPathAndKeepsTheSentinelFirst()
  {
    var alpha = NewProject("Alpha", "/work/one");
    var beta = NewProject("Beta", "/games/two");
    var model = NewModel([alpha, beta]);

    model.SearchText = "ALPH";
    Assert.Equal(new[] { true, false }, model.ProjectTiles.Select(tile => tile.IsNewCard).ToArray());
    Assert.Same(alpha, model.ProjectTiles[1]);

    model.SearchText = "games";
    Assert.Equal(2, model.ProjectTiles.Count);
    Assert.Same(beta, model.ProjectTiles[1]);

    model.SearchText = "nothing matches";
    Assert.Single(model.ProjectTiles);
    Assert.True(model.ProjectTiles[0].IsNewCard);

    model.SearchText = "";
    Assert.Equal(3, model.ProjectTiles.Count);
  }

  [Fact]
  public void AddingAndRemovingProjectsRebuildsTilesAndRaisesTheVisibilityFlags()
  {
    var model = NewModel();
    var log = new PropertyLog(model);
    var project = NewProject("Alpha", "/work/alpha");

    model.Projects.Add(project);

    Assert.Equal(2, model.ProjectTiles.Count);
    Assert.False(model.IsEmpty);
    Assert.Contains(nameof(ProjectsViewModel.IsEmpty), log.Names);
    Assert.Contains(nameof(ProjectsViewModel.ShowGrid), log.Names);
    Assert.Contains(nameof(ProjectsViewModel.ShowList), log.Names);

    log.Names.Clear();
    model.Projects.Remove(project);

    Assert.Single(model.ProjectTiles);
    Assert.True(model.IsEmpty);
    Assert.Contains(nameof(ProjectsViewModel.IsEmpty), log.Names);
  }

  [Fact]
  public void ChoosingAViewFlipsItAndReportsEachRealChangeOnce()
  {
    var model = NewModel([NewProject("Alpha", "/work/alpha")]);
    var chosen = new List<bool>();
    model.ViewChosen = chosen.Add;
    var log = new PropertyLog(model);

    model.SetListCommand.Execute(null);

    Assert.False(model.IsGridView);
    Assert.True(model.IsListView);
    Assert.False(model.ShowGrid);
    Assert.True(model.ShowList);
    Assert.Equal(new[] { false }, chosen.ToArray());
    Assert.Contains(nameof(ProjectsViewModel.IsListView), log.Names);

    model.SetListCommand.Execute(null);
    Assert.Equal(new[] { false }, chosen.ToArray());

    model.SetGridCommand.Execute(null);
    Assert.True(model.IsGridView);
    Assert.True(model.ShowGrid);
    Assert.False(model.ShowList);
    Assert.Equal(new[] { false, true }, chosen.ToArray());
  }

  [Fact]
  public void NewProjectCommandOpensTemplates()
  {
    var opened = 0;
    var model = NewModel(openTemplates: () => opened++);

    model.NewProjectCommand.Execute(null);

    Assert.Equal(1, opened);
  }

  [Fact]
  public void UpdatingTooltipsReplacesBothAndRaisesTheirChange()
  {
    var model = NewModel();
    var log = new PropertyLog(model);

    model.UpdateTooltips("new explore", "new open");

    Assert.Equal("new explore", model.ExploreTooltip);
    Assert.Equal("new open", model.OpenTooltip);
    Assert.Contains(nameof(ProjectsViewModel.ExploreTooltip), log.Names);
    Assert.Contains(nameof(ProjectsViewModel.OpenTooltip), log.Names);

    log.Names.Clear();
    model.UpdateTooltips("new explore", "new open");
    Assert.Empty(log.Names);
  }
}
