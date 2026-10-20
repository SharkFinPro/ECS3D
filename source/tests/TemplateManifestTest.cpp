#include <gtest/gtest.h>

#include "templates/TemplateManifest.h"

#include <nlohmann/json.hpp>

#include <string>

namespace
{
  nlohmann::json validManifest()
  {
    return {
      { "id", "coin-rush" },
      { "name", "Coin Rush" },
      { "description", "An arena." },
      { "category", "sample" },
      { "tags", { "multiplayer", "game" } },
      { "thumbnail", "thumbnail.png" },
      { "entryScene", "Arena" },
      { "minEngineVersion", "0.0.1" }
    };
  }

  void expectRefused(const nlohmann::json& data, const std::string& field)
  {
    std::string error;
    EXPECT_FALSE(parseTemplateManifest(data, &error).has_value());
    EXPECT_NE(error.find("'" + field + "'"), std::string::npos) << error;
  }
}

TEST(TemplateManifest, ParsesAManifestWithoutPlayers)
{
  const auto manifest = parseTemplateManifest(validManifest());

  ASSERT_TRUE(manifest.has_value());
  EXPECT_EQ(manifest->id, "coin-rush");
  EXPECT_EQ(manifest->name, "Coin Rush");
  EXPECT_EQ(manifest->category, "sample");
  EXPECT_EQ(manifest->entryScene, "Arena");
  EXPECT_EQ(manifest->minEngineVersion, "0.0.1");
  ASSERT_EQ(manifest->tags.size(), 2u);
  EXPECT_EQ(manifest->tags[1], "game");
  EXPECT_FALSE(manifest->players.has_value());
}

TEST(TemplateManifest, ParsesPlayersAndIgnoresUnknownFields)
{
  auto data = validManifest();
  data["players"] = { { "min", 1 }, { "max", 4 } };
  data["somethingNew"] = 7;

  const auto manifest = parseTemplateManifest(data);

  ASSERT_TRUE(manifest.has_value());
  ASSERT_TRUE(manifest->players.has_value());
  EXPECT_EQ(manifest->players->min, 1);
  EXPECT_EQ(manifest->players->max, 4);
}

TEST(TemplateManifest, EveryRequiredFieldIsRequired)
{
  for (const char* field : { "id", "name", "description", "category", "tags", "thumbnail", "entryScene",
                             "minEngineVersion" })
  {
    SCOPED_TRACE(field);
    auto data = validManifest();
    data.erase(field);
    expectRefused(data, field);
  }
}

TEST(TemplateManifest, AWrongTypeNamesTheField)
{
  auto data = validManifest();
  data["name"] = 5;
  expectRefused(data, "name");

  data = validManifest();
  data["tags"] = "multiplayer";
  expectRefused(data, "tags");

  data = validManifest();
  data["tags"] = nlohmann::json::array({ "ok", 3 });
  expectRefused(data, "tags");
}

TEST(TemplateManifest, EmptyStringsAreRefused)
{
  for (const char* field : { "name", "description", "entryScene", "minEngineVersion" })
  {
    SCOPED_TRACE(field);
    auto data = validManifest();
    data[field] = "";
    expectRefused(data, field);
  }
}

TEST(TemplateManifest, AnUnknownCategoryIsRefused)
{
  auto data = validManifest();
  data["category"] = "demo";

  expectRefused(data, "category");
}

TEST(TemplateManifest, TheIdRuleIsEnforced)
{
  for (const char* bad : { "Coin-Rush", "coin_rush", "-coin", "coin-", "coin--rush", "", "coin rush" })
  {
    SCOPED_TRACE(bad);
    auto data = validManifest();
    data["id"] = bad;
    expectRefused(data, "id");
  }

  for (const char* good : { "empty-3d", "a", "3d", "physics-playground" })
  {
    SCOPED_TRACE(good);
    auto data = validManifest();
    data["id"] = good;
    EXPECT_TRUE(parseTemplateManifest(data).has_value());
  }
}

TEST(TemplateManifest, BadPlayersAreRefused)
{
  const nlohmann::json bad[] = {
    { { "min", 0 }, { "max", 2 } },
    { { "min", 3 }, { "max", 2 } },
    { { "min", 1 } },
    { { "min", "1" }, { "max", 2 } },
    { { "min", 1 }, { "max", 65 } },
    { { "min", 1 }, { "max", 4294967297 } },
    { { "min", 1 }, { "max", 2.5 } },
    "many"
  };

  for (const auto& players : bad)
  {
    SCOPED_TRACE(players.dump());
    auto data = validManifest();
    data["players"] = players;
    expectRefused(data, "players");
  }

  auto equal = validManifest();
  equal["players"] = { { "min", 2 }, { "max", 2 } };
  EXPECT_TRUE(parseTemplateManifest(equal).has_value());
}

TEST(TemplateManifest, ANonObjectIsRefused)
{
  std::string error;

  EXPECT_FALSE(parseTemplateManifest(nlohmann::json::array(), &error).has_value());
  EXPECT_FALSE(error.empty());
}

TEST(TemplateManifest, LoadingAMissingFolderReportsInsteadOfThrowing)
{
  std::string error;

  EXPECT_FALSE(loadTemplateManifest("definitely/not/a/folder", &error).has_value());
  EXPECT_FALSE(error.empty());
}
