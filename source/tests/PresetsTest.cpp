#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {
  nlohmann::json findPreset(const std::string& name)
  {
    const auto path = std::filesystem::path(ECS3D_SOURCE_DIR) / "CMakePresets.json";
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "cannot open " << path.string();
    if (!file.is_open())
    {
      return nullptr;
    }

    const auto presets = nlohmann::json::parse(file, nullptr, false);
    EXPECT_FALSE(presets.is_discarded()) << path.string() << " is not valid JSON";
    if (presets.is_discarded())
    {
      return nullptr;
    }

    for (const auto& preset : presets.value("configurePresets", nlohmann::json::array()))
    {
      if (preset.value("name", "") == name)
      {
        return preset;
      }
    }

    ADD_FAILURE() << "no configure preset named " << name;
    return nullptr;
  }

  std::string cacheVariable(const nlohmann::json& preset, const std::string& key)
  {
    return preset.value("cacheVariables", nlohmann::json::object()).value(key, "");
  }
}

TEST(PresetsTest, ProfilePresetIsAnOptimizedBuildWithDebugInfoAndTheProfileOption)
{
  const auto preset = findPreset("ecs3d-profile");
  ASSERT_FALSE(preset.is_null());

  EXPECT_EQ(preset.value("inherits", ""), "ecs3d-base");
  EXPECT_EQ(cacheVariable(preset, "CMAKE_BUILD_TYPE"), "RelWithDebInfo");
  EXPECT_EQ(cacheVariable(preset, "ECS3D_PROFILE"), "ON");
}

TEST(PresetsTest, ReleasePresetStaysApartFromTheProfilePreset)
{
  const auto preset = findPreset("ecs3d-release");
  ASSERT_FALSE(preset.is_null());

  EXPECT_EQ(cacheVariable(preset, "CMAKE_BUILD_TYPE"), "Release");
  EXPECT_EQ(cacheVariable(preset, "ECS3D_PROFILE"), "");
}
