#include <gtest/gtest.h>

#include "edits/AssetWireType.h"

#include <array>
#include <string>

namespace {
  constexpr std::array knownTypes{
    AssetType::Model, AssetType::Texture, AssetType::Script, AssetType::Prefab, AssetType::Scene
  };
}

TEST(AssetWireType, EveryKnownTypeRoundTripsThroughItsWireString)
{
  for (const auto type : knownTypes)
  {
    const auto wire = edits::assetTypeToWireString(type);

    EXPECT_FALSE(wire.empty());
    EXPECT_EQ(edits::assetTypeFromWireString(wire), type) << wire;
  }
}

TEST(AssetWireType, WireStringsArePinnedToTheDocumentedNames)
{
  EXPECT_EQ(edits::assetTypeToWireString(AssetType::Model), "model");
  EXPECT_EQ(edits::assetTypeToWireString(AssetType::Texture), "texture");
  EXPECT_EQ(edits::assetTypeToWireString(AssetType::Script), "script");
  EXPECT_EQ(edits::assetTypeToWireString(AssetType::Prefab), "prefab");
  EXPECT_EQ(edits::assetTypeToWireString(AssetType::Scene), "scene");
}

TEST(AssetWireType, UnknownTypeHasNoWireStringAndAnUnrecognizedStringIsUnknown)
{
  EXPECT_EQ(edits::assetTypeToWireString(AssetType::Unknown), "");

  EXPECT_EQ(edits::assetTypeFromWireString(""), AssetType::Unknown);
  EXPECT_EQ(edits::assetTypeFromWireString("Model"), AssetType::Unknown);
  EXPECT_EQ(edits::assetTypeFromWireString("sound"), AssetType::Unknown);
}
