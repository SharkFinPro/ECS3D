#include <gtest/gtest.h>

#include "TerrainPaletteBinding.h"

#include <terrain/TerrainStore.h>
#include <map>
#include <memory>
#include <vector>
#include <uuid.h>

namespace {
  struct FakeTexture {
    int id = 0;
  };

  using Handles = TerrainPaletteHandles<FakeTexture>;

  uuids::uuid uuidFrom(const char* text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const uuids::uuid grassUUID = uuidFrom("00000000-0000-0000-0000-000000000001");
  const uuids::uuid rockUUID = uuidFrom("00000000-0000-0000-0000-000000000002");
  const uuids::uuid lostUUID = uuidFrom("00000000-0000-0000-0000-000000000003");

  struct Fixture {
    std::shared_ptr<FakeTexture> grass = std::make_shared<FakeTexture>(FakeTexture{ 1 });
    std::shared_ptr<FakeTexture> rock = std::make_shared<FakeTexture>(FakeTexture{ 2 });
    std::shared_ptr<FakeTexture> placeholder = std::make_shared<FakeTexture>(FakeTexture{ 99 });
    std::map<uuids::uuid, std::shared_ptr<FakeTexture>> known{ { grassUUID, grass }, { rockUUID, rock } };
    std::vector<uuids::uuid> missing;

    Handles resolve(const std::vector<terrain::TerrainMaterial>& palette)
    {
      return resolveTerrainPalette<FakeTexture>(
        palette,
        [this](const uuids::uuid& uuid) {
          const auto it = known.find(uuid);
          return it == known.end() ? std::shared_ptr<FakeTexture>() : it->second;
        },
        placeholder, [this](const uuids::uuid& uuid) { missing.push_back(uuid); });
    }
  };

  terrain::TerrainMaterial material(const uuids::uuid& albedo, const uuids::uuid& specular = {})
  {
    return { "m", albedo, specular };
  }
}

TEST(TerrainPaletteBinding, NilTexturesResolveToNull)
{
  Fixture f;
  const auto resolved = f.resolve({ material({}) });

  ASSERT_EQ(resolved.size(), 1u);
  EXPECT_EQ(resolved[0].albedo, nullptr);
  EXPECT_EQ(resolved[0].specular, nullptr);
  EXPECT_TRUE(f.missing.empty());
}

TEST(TerrainPaletteBinding, ResolvedTexturesKeepTheirHandlesAndTheRepeatScale)
{
  Fixture f;
  const auto resolved = f.resolve({ material(grassUUID, rockUUID) });

  ASSERT_EQ(resolved.size(), 1u);
  EXPECT_EQ(resolved[0].albedo, f.grass);
  EXPECT_EQ(resolved[0].specular, f.rock);
  EXPECT_FLOAT_EQ(resolved[0].textureScale, terrainTextureRepeatsPerUnit);
}

TEST(TerrainPaletteBinding, UnresolvedTexturesDrawThePlaceholderAndAreReported)
{
  Fixture f;
  const auto resolved = f.resolve({ material(lostUUID, grassUUID) });

  ASSERT_EQ(resolved.size(), 1u);
  EXPECT_EQ(resolved[0].albedo, f.placeholder);
  EXPECT_EQ(resolved[0].specular, f.grass);
  ASSERT_EQ(f.missing.size(), 1u);
  EXPECT_EQ(f.missing[0], lostUUID);
}

TEST(TerrainPaletteBinding, EntriesPastTheEngineLimitAreDropped)
{
  Fixture f;
  std::vector<terrain::TerrainMaterial> palette;
  for (std::size_t i = 0; i < maxTerrainPaletteMaterials + 5; ++i)
  {
    palette.push_back(material(i % 2 == 0 ? grassUUID : rockUUID));
  }

  const auto resolved = f.resolve(palette);

  ASSERT_EQ(resolved.size(), maxTerrainPaletteMaterials);
  EXPECT_EQ(resolved[0].albedo, f.grass);
  EXPECT_EQ(resolved[1].albedo, f.rock);

  palette.resize(maxTerrainPaletteMaterials);
  EXPECT_EQ(f.resolve(palette).size(), maxTerrainPaletteMaterials);
}

TEST(TerrainPaletteBinding, SamePaletteComparesPointersOrderLengthAndScale)
{
  Fixture f;
  const Handles base = f.resolve({ material(grassUUID), material(rockUUID) });

  EXPECT_TRUE(samePalette(base, f.resolve({ material(grassUUID), material(rockUUID) })));

  EXPECT_FALSE(samePalette(base, f.resolve({ material(rockUUID), material(grassUUID) })));
  EXPECT_FALSE(samePalette(base, f.resolve({ material(grassUUID) })));
  EXPECT_FALSE(samePalette(base, f.resolve({ material(grassUUID), material(lostUUID) })));

  Handles rescaled = base;
  rescaled[0].textureScale *= 2.0f;
  EXPECT_FALSE(samePalette(base, rescaled));
}

TEST(TerrainPaletteApplication, UnchangedPaletteOnAnExistingSurfaceIsNotAppliedAgain)
{
  Fixture f;
  TerrainPaletteApplication<FakeTexture> surface;
  const Handles palette = f.resolve({ material(grassUUID) });

  surface.reset();
  ASSERT_TRUE(surface.needsApply(palette));
  surface.markApplied(palette);

  EXPECT_FALSE(surface.needsApply(f.resolve({ material(grassUUID) })));
}

TEST(TerrainPaletteApplication, AChangedTextureIsApplied)
{
  Fixture f;
  TerrainPaletteApplication<FakeTexture> surface;
  surface.markApplied(f.resolve({ material(grassUUID) }));

  EXPECT_TRUE(surface.needsApply(f.resolve({ material(rockUUID) })));
}

TEST(TerrainPaletteApplication, APlaceholderReplacedByTheRealTextureIsApplied)
{
  Fixture f;
  f.known.erase(grassUUID);

  TerrainPaletteApplication<FakeTexture> surface;
  const Handles withPlaceholder = f.resolve({ material(grassUUID) });
  surface.markApplied(withPlaceholder);
  EXPECT_FALSE(surface.needsApply(f.resolve({ material(grassUUID) })));

  f.known[grassUUID] = f.grass;
  EXPECT_TRUE(surface.needsApply(f.resolve({ material(grassUUID) })));
}

TEST(TerrainPaletteApplication, AnEmptyPaletteAfterANonEmptyOneIsApplied)
{
  Fixture f;
  TerrainPaletteApplication<FakeTexture> surface;
  surface.markApplied(f.resolve({ material(grassUUID) }));

  EXPECT_TRUE(surface.needsApply(f.resolve({})));
}

TEST(TerrainPaletteApplication, ANewSurfaceStartsOnTheDefaultPalette)
{
  Fixture f;
  TerrainPaletteApplication<FakeTexture> surface;
  const Handles palette = f.resolve({ material(grassUUID) });
  surface.markApplied(palette);
  ASSERT_FALSE(surface.needsApply(palette));

  surface.reset();

  EXPECT_TRUE(surface.needsApply(palette));
  EXPECT_FALSE(surface.needsApply(f.resolve({})));
}

TEST(TerrainPaletteApplication, APaletteThatFailedToApplyIsNotRetriedUntilItChanges)
{
  Fixture f;
  TerrainPaletteApplication<FakeTexture> surface;
  const Handles rejected = f.resolve({ material(grassUUID) });

  ASSERT_TRUE(surface.needsApply(rejected));
  surface.markApplied(rejected);

  EXPECT_FALSE(surface.needsApply(rejected));
  EXPECT_TRUE(surface.needsApply(f.resolve({ material(rockUUID) })));
}
