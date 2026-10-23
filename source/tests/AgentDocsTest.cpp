#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {
  // The root file loads into every agent session, so its size is a cost paid on every request.
  constexpr std::size_t rootMaxBytes = 18 * 1024;
  constexpr std::size_t rootMaxLines = 200;
  constexpr std::size_t nestedMaxBytes = 8 * 1024;

  const std::filesystem::path sourceDir{ECS3D_SOURCE_DIR};

  std::string fileText(const std::filesystem::path& path)
  {
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.is_open()) << "cannot open " << path.string();
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
  }

  bool isBuildOutput(const std::filesystem::path& path)
  {
    const auto name = path.filename().string();
    return name == "bin" || name == "obj" || name.starts_with(".");
  }

  // The launcher is a standalone project whose AGENTS.md follows its own conventions.
  bool isLauncher(const std::filesystem::path& path)
  {
    return path.lexically_relative(sourceDir).generic_string().starts_with("source/apps/launcher");
  }

  std::vector<std::filesystem::path> nestedAgentsFiles()
  {
    std::vector<std::filesystem::path> files;
    auto it = std::filesystem::recursive_directory_iterator(
      sourceDir / "source", std::filesystem::directory_options::skip_permission_denied);
    for (; it != std::filesystem::recursive_directory_iterator(); ++it)
    {
      if (it->is_directory() && isBuildOutput(it->path()))
      {
        it.disable_recursion_pending();
        continue;
      }
      if (it->is_regular_file() && it->path().filename() == "AGENTS.md")
      {
        files.push_back(it->path());
      }
    }
    return files;
  }

  std::vector<std::filesystem::path> designNotes()
  {
    std::vector<std::filesystem::path> notes;
    for (const auto& entry : std::filesystem::directory_iterator(sourceDir / "docs" / "architecture"))
    {
      if (entry.path().extension() == ".md")
      {
        notes.push_back(entry.path());
      }
    }
    return notes;
  }

  // Relative markdown link targets, with any #anchor removed.
  std::vector<std::string> relativeLinkTargets(const std::string& text)
  {
    static const std::regex link(R"(\]\(([^)\s]+)\))");
    std::vector<std::string> targets;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), link); it != std::sregex_iterator(); ++it)
    {
      auto target = (*it)[1].str();
      if (target.starts_with("http://") || target.starts_with("https://") || target.starts_with("mailto:") ||
          target.starts_with("#"))
      {
        continue;
      }
      targets.push_back(target.substr(0, target.find('#')));
    }
    return targets;
  }

  void expectLinksResolve(const std::filesystem::path& file)
  {
    for (const auto& target : relativeLinkTargets(fileText(file)))
    {
      EXPECT_TRUE(std::filesystem::exists(file.parent_path() / target))
        << file.lexically_relative(sourceDir).generic_string() << " links to missing " << target;
    }
  }
}

TEST(AgentDocsTest, RootAgentsFileStaysWithinItsBudget)
{
  const auto text = fileText(sourceDir / "AGENTS.md");

  EXPECT_LE(text.size(), rootMaxBytes) << "move detail into a folder's AGENTS.md or docs/architecture/";
  EXPECT_LE(static_cast<std::size_t>(std::ranges::count(text, '\n')), rootMaxLines);
}

TEST(AgentDocsTest, NestedAgentsFilesStayWithinTheirBudget)
{
  const auto files = nestedAgentsFiles();
  ASSERT_FALSE(files.empty());

  for (const auto& file : files)
  {
    if (!isLauncher(file))
    {
      EXPECT_LE(fileText(file).size(), nestedMaxBytes) << file.lexically_relative(sourceDir).generic_string();
    }
  }
}

TEST(AgentDocsTest, LinksInAgentDocsResolve)
{
  expectLinksResolve(sourceDir / "AGENTS.md");
  for (const auto& file : nestedAgentsFiles())
  {
    expectLinksResolve(file);
  }
  for (const auto& note : designNotes())
  {
    expectLinksResolve(note);
  }
}

TEST(AgentDocsTest, EveryDesignNoteIsInTheDocumentationMap)
{
  const auto notes = designNotes();
  ASSERT_FALSE(notes.empty());

  const auto targets = relativeLinkTargets(fileText(sourceDir / "AGENTS.md"));
  for (const auto& note : notes)
  {
    const auto expected = "docs/architecture/" + note.filename().string();
    EXPECT_NE(std::ranges::find(targets, expected), targets.end()) << expected << " is not linked from AGENTS.md";
  }
}

TEST(AgentDocsTest, LinkTargetsDropAnchorsAndSkipExternalLinks)
{
  const auto targets = relativeLinkTargets(
    "[a](docs/a.md#part) [b](https://example.com) [c](#local) [d](../d.md) [e](mailto:x@y.z)");

  EXPECT_EQ(targets, (std::vector<std::string>{"docs/a.md", "../d.md"}));
}
