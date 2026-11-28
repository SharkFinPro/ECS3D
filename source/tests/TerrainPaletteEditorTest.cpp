#include <gtest/gtest.h>

#include "TerrainMaterialPrompts.h"
#include "TerrainPaletteEditor.h"
#include "terrain/TerrainStore.h"

#include <VulkanEngine/components/assets/volume/VolumeMaterial.h>
#include <glm/vec3.hpp>
#include <bitset>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <uuid.h>

namespace {
  const auto terrainId = uuids::uuid::from_string("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb").value();
}

namespace {
  const auto albedoId = uuids::uuid::from_string("22222222-2222-4222-8222-222222222222").value();
  const auto specularId = uuids::uuid::from_string("33333333-3333-4333-8333-333333333333").value();

  std::vector<terrain::TerrainMaterial> paletteOf(const size_t count)
  {
    std::vector<terrain::TerrainMaterial> palette;
    for (size_t i = 0; i < count; ++i)
    {
      palette.push_back({ "m" + std::to_string(i), albedoId, specularId });
    }

    return palette;
  }

  std::bitset<256> usedOnly(const size_t index)
  {
    std::bitset<256> used;
    used.set(index);
    return used;
  }

  TerrainPaletteEditor::Context allowed()
  {
    return { true, true, true };
  }

  using Name = MaterialNameField;
  using Color = MaterialColorField;

  const auto terrainB = uuids::uuid::from_string("dddddddd-dddd-4ddd-8ddd-dddddddddddd").value();

  terrain::TerrainMaterial materialNamed(const std::string& name, const glm::vec3& color = glm::vec3(0.5f))
  {
    return { name, {}, {}, color };
  }

  std::optional<Name::Commit> typeName(Name& field, const terrain::TerrainMaterial& material, const bool settled,
                                       const size_t index = 1, const uuids::uuid& terrain = terrainId,
                                       const bool changed = true, const bool canSend = true)
  {
    return field.frame(terrain, index, material, canSend, [&](std::string& value) {
      if (changed)
      {
        value = "typed";
      }

      return Name::Report{ changed, settled };
    });
  }

  std::optional<Name::Commit> idleName(Name& field, const terrain::TerrainMaterial& material, const size_t index,
                                       const uuids::uuid& terrain = terrainId)
  {
    return field.frame(terrain, index, material, true, [](std::string&) { return Name::Report{ false, true }; });
  }

  std::optional<Color::Commit> dragColor(Color& field, const terrain::TerrainMaterial& material, const glm::vec3& to,
                                         const bool changed, const bool settled, const bool canSend = true)
  {
    return field.frame(terrainId, 1, material, canSend, [&](glm::vec3& value) {
      if (changed)
      {
        value = to;
      }

      return Color::Report{ changed, settled };
    });
  }

  std::optional<terrain::TerrainMaterial> sameMaterial(const uuids::uuid&, const size_t)
  {
    return materialNamed("rock");
  }
}

TEST(TerrainPaletteEditorIntents, AddAppendsADefaultNamedEntryAndRefusesThe17th)
{
  const auto added = TerrainPaletteEditor::add(paletteOf(2));
  ASSERT_TRUE(added.ok());
  ASSERT_EQ(added.palette.size(), 3u);
  EXPECT_EQ(added.palette[2].name, "Material 2");
  EXPECT_TRUE(added.palette[2].albedoTexture.is_nil());
  EXPECT_TRUE(added.palette[2].specularTexture.is_nil());
  EXPECT_EQ(added.palette[2].color, vke::s_defaultVolumeColors[2]);
  EXPECT_NE(added.palette[2].color, TerrainPaletteEditor::add(paletteOf(3)).palette[3].color);
  EXPECT_EQ(TerrainPaletteEditor::add(paletteOf(8)).palette[8].color, vke::s_defaultVolumeColors[0]);

  const auto lastFit = TerrainPaletteEditor::add(paletteOf(terrain::maxPaletteSize - 1));
  EXPECT_TRUE(lastFit.ok());
  EXPECT_EQ(lastFit.palette.size(), terrain::maxPaletteSize);

  const auto full = TerrainPaletteEditor::add(paletteOf(terrain::maxPaletteSize));
  ASSERT_FALSE(full.ok());
  EXPECT_FALSE(full.refusal->empty());
  EXPECT_EQ(full.palette, paletteOf(terrain::maxPaletteSize));
}

TEST(TerrainPaletteEditorIntents, AnUnpaintedRemoveErasesTheEntryWithNoRemap)
{
  const auto clear = TerrainPaletteEditor::remove(paletteOf(3), 1, usedOnly(0), 0);
  ASSERT_TRUE(clear.ok());
  ASSERT_EQ(clear.palette.size(), 2u);
  EXPECT_EQ(clear.palette[1].name, "m2");
  EXPECT_FALSE(clear.remap.has_value());

  EXPECT_FALSE(TerrainPaletteEditor::remove(paletteOf(3), 3, {}, 2).ok());
}

TEST(TerrainPaletteEditorIntents, APaintedRemoveCarriesTheRemapToItsReplacementAndShiftsLaterEntries)
{
  const auto painted = TerrainPaletteEditor::remove(paletteOf(3), 1, usedOnly(1), 2);
  ASSERT_TRUE(painted.ok());
  ASSERT_EQ(painted.palette.size(), 2u);
  EXPECT_EQ(painted.palette[1].name, "m2");
  ASSERT_TRUE(painted.remap.has_value());
  EXPECT_EQ((*painted.remap)[1], 1);
  EXPECT_EQ((*painted.remap)[2], 1);
  EXPECT_EQ((*painted.remap)[0], 0);

  // Only a later entry is painted: its samples move down with it.
  const auto later = TerrainPaletteEditor::remove(paletteOf(3), 0, usedOnly(2), 1);
  ASSERT_TRUE(later.ok());
  ASSERT_TRUE(later.remap.has_value());
  EXPECT_EQ((*later.remap)[2], 1);

  const auto toItself = TerrainPaletteEditor::remove(paletteOf(3), 1, usedOnly(1), 1);
  EXPECT_FALSE(toItself.ok());
  EXPECT_EQ(toItself.palette, paletteOf(3));
}

TEST(TerrainPaletteEditorIntents, TheLastMaterialIsNeverRemoved)
{
  const auto last = TerrainPaletteEditor::remove(paletteOf(1), 0, {}, 1);
  ASSERT_FALSE(last.ok());
  EXPECT_NE(last.refusal->find("at least one"), std::string::npos);
  EXPECT_EQ(last.palette, paletteOf(1));

  EXPECT_TRUE(TerrainPaletteEditor::remove(paletteOf(2), 0, {}, 1).ok());
}

TEST(TerrainPaletteEditorIntents, OnlyARemoveOfAPaintedEntryAsksAndItDefaultsToThePreviousEntry)
{
  EXPECT_TRUE(TerrainPaletteEditor::removeNeedsChoice(3, 1, usedOnly(1)));
  EXPECT_FALSE(TerrainPaletteEditor::removeNeedsChoice(3, 1, usedOnly(2)));
  EXPECT_FALSE(TerrainPaletteEditor::removeNeedsChoice(1, 0, usedOnly(0)));
  EXPECT_FALSE(TerrainPaletteEditor::removeNeedsChoice(3, 3, usedOnly(3)));

  EXPECT_EQ(TerrainPaletteEditor::defaultReplacement(2), 1u);
  EXPECT_EQ(TerrainPaletteEditor::defaultReplacement(0), 1u);
}

TEST(TerrainPaletteEditorIntents, AMoveRemapsOnlyWhenTheGroundCarriesAnEntryItReorders)
{
  const auto painted = TerrainPaletteEditor::move(paletteOf(3), 1, 2, usedOnly(2));
  ASSERT_TRUE(painted.ok());
  ASSERT_TRUE(painted.remap.has_value());
  EXPECT_EQ((*painted.remap)[1], 2);
  EXPECT_EQ((*painted.remap)[2], 1);

  const auto unpainted = TerrainPaletteEditor::move(paletteOf(3), 1, 2, usedOnly(0));
  ASSERT_TRUE(unpainted.ok());
  EXPECT_FALSE(unpainted.remap.has_value());
  EXPECT_EQ(unpainted.palette, painted.palette);
}

TEST(TerrainPaletteEditorIntents, MoveRenameAndTextureEditsChangeOnlyTheirEntry)
{
  const auto moved = TerrainPaletteEditor::move(paletteOf(3), 0, 2, {});
  ASSERT_TRUE(moved.ok());
  EXPECT_EQ(moved.palette[2].name, "m0");
  EXPECT_EQ(moved.palette[0].name, "m1");
  EXPECT_FALSE(TerrainPaletteEditor::move(paletteOf(3), 0, 3, usedOnly(0)).ok());

  const auto renamed = TerrainPaletteEditor::rename(paletteOf(3), 1, "rock");
  ASSERT_TRUE(renamed.ok());
  EXPECT_EQ(renamed.palette[1].name, "rock");
  EXPECT_EQ(renamed.palette[0].name, "m0");
  EXPECT_FALSE(TerrainPaletteEditor::rename(paletteOf(3), 3, "rock").ok());
  EXPECT_FALSE(TerrainPaletteEditor::rename(paletteOf(3), 1,
                                            std::string(terrain::maxMaterialNameLength + 1, 'a')).ok());

  const auto newTexture = uuids::uuid::from_string("44444444-4444-4444-8444-444444444444").value();
  const auto albedo = TerrainPaletteEditor::setAlbedo(paletteOf(2), 1, newTexture);
  ASSERT_TRUE(albedo.ok());
  EXPECT_EQ(albedo.palette[1].albedoTexture, newTexture);
  EXPECT_EQ(albedo.palette[1].specularTexture, specularId);

  const auto specular = TerrainPaletteEditor::setSpecular(paletteOf(2), 1, newTexture);
  ASSERT_TRUE(specular.ok());
  EXPECT_EQ(specular.palette[1].specularTexture, newTexture);
  EXPECT_EQ(specular.palette[1].albedoTexture, albedoId);

  EXPECT_FALSE(TerrainPaletteEditor::setAlbedo(paletteOf(2), 2, newTexture).ok());
  EXPECT_FALSE(TerrainPaletteEditor::setSpecular(paletteOf(2), 2, newTexture).ok());
}

TEST(TerrainPaletteEditorIntents, SetColorChangesOnlyTheColorAndRefusesAnInvalidOne)
{
  const auto colored = TerrainPaletteEditor::setColor(paletteOf(2), 1, glm::vec3(0.1f, 0.2f, 0.3f));
  ASSERT_TRUE(colored.ok());
  EXPECT_EQ(colored.palette[1].color, glm::vec3(0.1f, 0.2f, 0.3f));
  EXPECT_EQ(colored.palette[1].albedoTexture, albedoId);
  EXPECT_EQ(colored.palette[0], paletteOf(2)[0]);

  const auto outOfRange = TerrainPaletteEditor::setColor(paletteOf(2), 1, glm::vec3(1.5f, 0.0f, 0.0f));
  ASSERT_FALSE(outOfRange.ok());
  EXPECT_EQ(outOfRange.palette, paletteOf(2));

  EXPECT_FALSE(TerrainPaletteEditor::setColor(paletteOf(2), 2, glm::vec3(0.5f)).ok());
}

TEST(TerrainPaletteEditorIntents, UseColorDropsTheAlbedoKeepsTheSpecularAndTheColor)
{
  auto palette = paletteOf(2);
  palette[1].color = glm::vec3(0.1f, 0.2f, 0.3f);
  EXPECT_FALSE(TerrainPaletteEditor::isColorMode(palette[1]));

  const auto flat = TerrainPaletteEditor::useColor(palette, 1);
  ASSERT_TRUE(flat.ok());
  EXPECT_TRUE(TerrainPaletteEditor::isColorMode(flat.palette[1]));
  EXPECT_TRUE(flat.palette[1].albedoTexture.is_nil());
  EXPECT_EQ(flat.palette[1].specularTexture, specularId);
  EXPECT_EQ(flat.palette[1].color, glm::vec3(0.1f, 0.2f, 0.3f));
  EXPECT_FALSE(TerrainPaletteEditor::isColorMode(flat.palette[0]));

  EXPECT_FALSE(TerrainPaletteEditor::useColor(palette, 2).ok());

  // Picking a texture for a color entry leaves its color, so switching back and forth keeps it.
  const auto textured = TerrainPaletteEditor::setAlbedo(flat.palette, 1, albedoId);
  ASSERT_TRUE(textured.ok());
  EXPECT_EQ(textured.palette[1].color, glm::vec3(0.1f, 0.2f, 0.3f));
}

TEST(TerrainPaletteEditorGate, EditingNeedsAnEditableConnectionAStoppedSceneAndAHeldTerrain)
{
  EXPECT_FALSE(TerrainPaletteEditor::editRefusal(allowed()).has_value());

  auto readOnly = allowed();
  readOnly.editable = false;
  EXPECT_TRUE(TerrainPaletteEditor::editRefusal(readOnly).has_value());

  auto playing = allowed();
  playing.sceneStopped = false;
  const auto playRefusal = TerrainPaletteEditor::editRefusal(playing);
  ASSERT_TRUE(playRefusal.has_value());
  EXPECT_NE(playRefusal->find("Stop"), std::string::npos);

  auto unknown = allowed();
  unknown.terrainKnown = false;
  EXPECT_TRUE(TerrainPaletteEditor::editRefusal(unknown).has_value());
}

TEST(MaterialNameFieldTest, ATypedNameCommitsOnceWhenTheFieldIsLeft)
{
  Name field;
  const auto material = materialNamed("rock");

  EXPECT_FALSE(typeName(field, material, false).has_value());
  EXPECT_FALSE(typeName(field, material, false).has_value());
  EXPECT_TRUE(field.dirty());

  const auto commit = typeName(field, material, true, 1, terrainId, false);
  ASSERT_TRUE(commit.has_value());
  EXPECT_EQ(commit->terrain, terrainId);
  EXPECT_EQ(commit->index, 1u);
  EXPECT_EQ(commit->value, "typed");
  EXPECT_FALSE(field.dirty());

  EXPECT_FALSE(idleName(field, materialNamed("typed"), 1).has_value());
}

TEST(MaterialNameFieldTest, AnUnchangedNameOrAnUnsendableOneCommitsNothing)
{
  Name unchanged;
  EXPECT_FALSE(typeName(unchanged, materialNamed("typed"), false).has_value());
  EXPECT_FALSE(typeName(unchanged, materialNamed("typed"), true, 1, terrainId, false).has_value());

  Name unsendable;
  EXPECT_FALSE(typeName(unsendable, materialNamed("rock"), false, 1, terrainId, true, false).has_value());
  EXPECT_FALSE(typeName(unsendable, materialNamed("rock"), true, 1, terrainId, false, false).has_value());
  EXPECT_FALSE(unsendable.dirty());

  Name control;
  EXPECT_FALSE(typeName(control, materialNamed("rock"), false).has_value());
  EXPECT_TRUE(typeName(control, materialNamed("rock"), true, 1, terrainId, false).has_value());
}

TEST(MaterialNameFieldTest, AnIdleRowFollowsThePaletteAndOnlyTheEditedRowKeepsItsText)
{
  Name field;
  std::string shown;
  const auto probe = [&shown](std::string& value) {
    shown = value;
    return Name::Report{ false, false };
  };

  static_cast<void>(field.frame(terrainId, 1, materialNamed("rock"), true, probe));
  EXPECT_EQ(shown, "rock");
  static_cast<void>(field.frame(terrainId, 1, materialNamed("sand"), true, probe));
  EXPECT_EQ(shown, "sand");

  EXPECT_FALSE(typeName(field, materialNamed("rock"), false).has_value());
  static_cast<void>(field.frame(terrainId, 1, materialNamed("rock"), true, probe));
  EXPECT_EQ(shown, "typed");

  static_cast<void>(field.frame(terrainId, 2, materialNamed("other"), true, probe));
  EXPECT_EQ(shown, "other");
}

TEST(MaterialNameFieldTest, ANameTypedInABlockThatStoppedDrawingIsCommittedByTheNextFrameEnd)
{
  Name field;
  const auto material = materialNamed("rock");

  EXPECT_FALSE(typeName(field, material, false).has_value());

  // The frame that drew the row does not commit; the next one, which did not draw it, does.
  EXPECT_FALSE(field.endFrame(true, sameMaterial).has_value());
  const auto flushed = field.endFrame(true, sameMaterial);
  ASSERT_TRUE(flushed.has_value());
  EXPECT_EQ(flushed->value, "typed");
  EXPECT_FALSE(field.dirty());
  EXPECT_FALSE(field.endFrame(true, sameMaterial).has_value());
}

TEST(MaterialNameFieldTest, AnotherTerrainDrawingSettlesTheEditPendingOnThisOne)
{
  Name field;
  const auto material = materialNamed("rock");

  EXPECT_FALSE(typeName(field, material, false).has_value());
  EXPECT_FALSE(field.endFrame(true, sameMaterial).has_value());

  // Terrain B's block draws its own row 1 while the edit is pending on terrain A.
  EXPECT_FALSE(idleName(field, material, 1, terrainB).has_value());
  const auto flushed = field.endFrame(true, sameMaterial);
  ASSERT_TRUE(flushed.has_value());
  EXPECT_EQ(flushed->terrain, terrainId);
  EXPECT_EQ(flushed->value, "typed");

  // Control: while the pending row keeps drawing, the edit waits.
  Name waiting;
  EXPECT_FALSE(typeName(waiting, material, false).has_value());
  EXPECT_FALSE(idleName(waiting, material, 1, terrainB).has_value());
  EXPECT_FALSE(typeName(waiting, material, false, 1, terrainId, false).has_value());
  EXPECT_FALSE(waiting.endFrame(true, sameMaterial).has_value());
  EXPECT_TRUE(waiting.dirty());
}

TEST(MaterialNameFieldTest, ADifferentSetOfRowsDrawingSettlesTheEdit)
{
  Name field;
  const auto material = materialNamed("rock");

  EXPECT_FALSE(typeName(field, material, false, 5).has_value());
  EXPECT_FALSE(field.endFrame(true, sameMaterial).has_value());

  for (size_t row = 0; row < 3; ++row)
  {
    EXPECT_FALSE(idleName(field, material, row).has_value());
  }

  const auto flushed = field.endFrame(true, sameMaterial);
  ASSERT_TRUE(flushed.has_value());
  EXPECT_EQ(flushed->index, 5u);
}

TEST(MaterialNameFieldTest, AnEditIsDroppedWhenTheMaterialAtItsIndexChangedUnderneath)
{
  const auto material = materialNamed("rock", glm::vec3(0.2f));
  const auto changedUnderneath = [](const uuids::uuid&, const size_t) -> std::optional<terrain::TerrainMaterial> {
    return materialNamed("sand", glm::vec3(0.9f));
  };

  Name flushed;
  EXPECT_FALSE(typeName(flushed, material, false).has_value());
  static_cast<void>(flushed.endFrame(true, changedUnderneath));
  EXPECT_FALSE(flushed.endFrame(true, changedUnderneath).has_value());
  EXPECT_FALSE(flushed.dirty());

  Name settled;
  EXPECT_FALSE(typeName(settled, material, false).has_value());
  EXPECT_FALSE(typeName(settled, materialNamed("rock", glm::vec3(0.3f)), true, 1, terrainId, false).has_value());

  Name gone;
  EXPECT_FALSE(typeName(gone, material, false).has_value());
  static_cast<void>(gone.endFrame(true, [](const uuids::uuid&, const size_t) {
    return std::optional<terrain::TerrainMaterial>{};
  }));
  EXPECT_FALSE(gone.endFrame(true, [](const uuids::uuid&, const size_t) {
    return std::optional<terrain::TerrainMaterial>{};
  }).has_value());
  EXPECT_FALSE(gone.dirty());

  // Control: the same material underneath commits.
  Name control;
  EXPECT_FALSE(typeName(control, materialNamed("rock"), false).has_value());
  static_cast<void>(control.endFrame(true, sameMaterial));
  EXPECT_TRUE(control.endFrame(true, sameMaterial).has_value());
}

TEST(MaterialColorFieldTest, ADragCommitsOnceWhenItSettles)
{
  Color field;
  const auto material = materialNamed("rock", glm::vec3(0.5f));
  const glm::vec3 picked(0.1f, 0.2f, 0.3f);

  EXPECT_FALSE(dragColor(field, material, picked, true, false).has_value());
  EXPECT_FALSE(dragColor(field, material, picked, true, false).has_value());
  EXPECT_FALSE(dragColor(field, material, picked, false, false).has_value());
  EXPECT_TRUE(field.dirty());

  const auto commit = dragColor(field, material, picked, false, true);
  ASSERT_TRUE(commit.has_value());
  EXPECT_EQ(commit->terrain, terrainId);
  EXPECT_EQ(commit->index, 1u);
  EXPECT_EQ(commit->value, picked);
  EXPECT_FALSE(field.dirty());

  EXPECT_FALSE(dragColor(field, material, picked, false, true).has_value());
}

TEST(MaterialColorFieldTest, AClickWithoutMovingOrAnUnsendableColorCommitsNothing)
{
  const auto material = materialNamed("rock", glm::vec3(0.5f));

  Color click;
  EXPECT_FALSE(dragColor(click, material, glm::vec3(0.5f + 1e-7f), true, false).has_value());
  EXPECT_FALSE(dragColor(click, material, glm::vec3(0.5f), false, true).has_value());

  Color moved;
  EXPECT_FALSE(dragColor(moved, material, glm::vec3(0.5f + 1e-3f), true, false).has_value());
  EXPECT_TRUE(dragColor(moved, material, glm::vec3(0.5f), false, true).has_value());

  Color unsendable;
  EXPECT_FALSE(dragColor(unsendable, material, glm::vec3(0.1f), true, false, false).has_value());
  EXPECT_FALSE(dragColor(unsendable, material, glm::vec3(0.1f), false, true, false).has_value());
}

TEST(TerrainPaletteEditorList, TheListGrowsWithItsEntriesUpToEightLinesThenScrolls)
{
  EXPECT_FLOAT_EQ(TerrainPaletteEditor::listHeight(3, 20.0f, 6.0f), 66.0f);
  EXPECT_FLOAT_EQ(TerrainPaletteEditor::listHeight(8, 20.0f, 6.0f), 166.0f);
  EXPECT_FLOAT_EQ(TerrainPaletteEditor::listHeight(16, 20.0f, 6.0f), 166.0f);
  EXPECT_FLOAT_EQ(TerrainPaletteEditor::listHeight(0, 20.0f, 6.0f), 26.0f);
}

TEST(TerrainPaletteEditorList, AMovedMaterialStaysSelectedAndARemovedOneSelectsItsNeighbor)
{
  using Kind = TextureModeRows::Change::Kind;

  EXPECT_EQ(materialSelectionAfter({ Kind::move, 2, 1 }, 2, 4), 1u);
  EXPECT_EQ(materialSelectionAfter({ Kind::move, 2, 3 }, 2, 4), 3u);
  EXPECT_EQ(materialSelectionAfter({ Kind::move, 0, 1 }, 1, 4), 0u);

  EXPECT_EQ(materialSelectionAfter({ Kind::remove, 1, 1 }, 1, 3), 1u);
  EXPECT_EQ(materialSelectionAfter({ Kind::remove, 3, 3 }, 3, 3), 2u);
  EXPECT_EQ(materialSelectionAfter({ Kind::remove, 0, 0 }, 2, 3), 1u);
  EXPECT_EQ(materialSelectionAfter({ Kind::remove, 0, 0 }, 0, 0), 0u);

  // An edit that keeps the order keeps the selection.
  EXPECT_EQ(materialSelectionAfter({}, 2, 4), 2u);
}

TEST(MaterialNoteTest, ARefusalShowsForItsSelectionAndIsClearedWhenTheSelectionChanges)
{
  MaterialNote note;
  note.raise(terrainId, 2, "Material 2 is painted on the terrain");
  EXPECT_EQ(note.textFor(terrainId, 2), "Material 2 is painted on the terrain");
  EXPECT_EQ(note.textFor(terrainId, 2), "Material 2 is painted on the terrain");

  EXPECT_TRUE(note.textFor(terrainId, 1).empty());
  EXPECT_TRUE(note.textFor(terrainId, 2).empty());

  note.raise(terrainId, 2, "refused");
  EXPECT_TRUE(note.textFor(terrainB, 2).empty());
  EXPECT_TRUE(note.textFor(terrainId, 2).empty());

  note.raise(terrainId, 0, "refused");
  note.clear();
  EXPECT_TRUE(note.textFor(terrainId, 0).empty());
}

namespace {
  std::vector<terrain::TerrainMaterial> flatPalette(const size_t count)
  {
    std::vector<terrain::TerrainMaterial> palette;
    for (size_t i = 0; i < count; ++i)
    {
      palette.push_back(materialNamed("m" + std::to_string(i), glm::vec3(0.1f * static_cast<float>(i + 1))));
    }

    return palette;
  }
}

TEST(TextureModeRowsTest, RowsFollowTheirMaterialThroughMovesAndRemoves)
{
  TextureModeRows rows;
  auto palette = flatPalette(5);
  rows.sync(terrainId, palette);
  rows.insert(1);
  rows.insert(3);

  auto moved = palette;
  ASSERT_FALSE(terrain::moveMaterial(moved, 1, 3).has_value());
  rows.follow({ TextureModeRows::Change::Kind::move, 1, 3 }, moved);
  EXPECT_TRUE(rows.contains(3));
  EXPECT_TRUE(rows.contains(2));
  EXPECT_FALSE(rows.contains(1));

  auto movedBack = moved;
  ASSERT_FALSE(terrain::moveMaterial(movedBack, 0, 4).has_value());
  rows.follow({ TextureModeRows::Change::Kind::move, 0, 4 }, movedBack);
  EXPECT_TRUE(rows.contains(2));
  EXPECT_TRUE(rows.contains(1));

  auto removed = movedBack;
  ASSERT_FALSE(terrain::removeMaterial(removed, 1, {}).has_value());
  rows.follow({ TextureModeRows::Change::Kind::remove, 1, 1 }, removed);
  EXPECT_FALSE(rows.contains(2));
  EXPECT_TRUE(rows.contains(1));
  EXPECT_EQ(removed.size(), 4u);

  // The live palette equals what the block recorded, so nothing is dropped.
  rows.sync(terrainId, removed);
  EXPECT_TRUE(rows.contains(1));
}

TEST(TextureModeRowsTest, APaletteChangedAnyOtherWayOrAnotherTerrainDropsTheRows)
{
  TextureModeRows rows;
  const auto palette = flatPalette(3);
  rows.sync(terrainId, palette);
  rows.insert(1);

  rows.sync(terrainId, palette);
  EXPECT_TRUE(rows.contains(1));

  auto undone = palette;
  undone[0].name = "other";
  rows.sync(terrainId, undone);
  EXPECT_FALSE(rows.contains(1));

  rows.insert(1);
  rows.sync(terrainB, undone);
  EXPECT_FALSE(rows.contains(1));
}

TEST(TextureModeRowsTest, AnUnrelatedEditKeepsTheRowsAndPickingATextureEndsOnlyThatRow)
{
  TextureModeRows rows;
  auto palette = flatPalette(3);
  rows.sync(terrainId, palette);
  rows.insert(1);
  rows.insert(2);

  palette[0].name = "renamed";
  rows.follow({}, palette);
  EXPECT_TRUE(rows.contains(1));
  EXPECT_TRUE(rows.contains(2));

  palette[1].albedoTexture = uuids::uuid::from_string("22222222-2222-4222-8222-222222222222").value();
  rows.follow({}, palette);
  EXPECT_FALSE(rows.contains(1));
  EXPECT_TRUE(rows.contains(2));

  rows.erase(2);
  EXPECT_FALSE(rows.contains(2));
}

TEST(MaterialRemovePromptTest, ThePromptDefaultsToTheNeighborAndNeverChoosesTheRemovedEntry)
{
  MaterialRemovePrompt prompt;
  EXPECT_FALSE(prompt.isOpen());

  prompt.open(terrainId, paletteOf(3), 0);
  ASSERT_TRUE(prompt.isOpen());
  EXPECT_EQ(prompt.index(), 0u);
  EXPECT_EQ(prompt.replacement(), 1u);

  prompt.choose(0);
  EXPECT_EQ(prompt.replacement(), 1u);
  prompt.choose(3);
  EXPECT_EQ(prompt.replacement(), 1u);
  prompt.choose(2);
  EXPECT_EQ(prompt.replacement(), 2u);

  prompt.close();
  EXPECT_FALSE(prompt.isOpen());
}

TEST(MaterialRemovePromptTest, APaletteThatChangedUnderneathOrAnotherTerrainClosesThePrompt)
{
  MaterialRemovePrompt prompt;
  prompt.open(terrainId, paletteOf(3), 2);
  EXPECT_TRUE(prompt.matches(terrainId, paletteOf(3)));
  EXPECT_TRUE(prompt.isOpen());

  EXPECT_FALSE(prompt.matches(terrainId, paletteOf(4)));
  EXPECT_FALSE(prompt.isOpen());
  EXPECT_FALSE(prompt.matches(terrainId, paletteOf(3)));

  prompt.open(terrainId, paletteOf(3), 2);
  EXPECT_FALSE(prompt.matches(terrainB, paletteOf(3)));

  // A palette with nothing to fall back to never opens.
  prompt.open(terrainId, paletteOf(1), 0);
  EXPECT_FALSE(prompt.isOpen());
}
