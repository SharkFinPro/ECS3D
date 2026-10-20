#include "objects/components/Script.h"
#include <Protocol.h>
#include <components/ScriptFieldLabel.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {
  const nlohmann::json labeledFields = nlohmann::json::array({
    { { "name", "m_speed" }, { "displayName", "Top Speed" }, { "type", "float" }, { "value", 2.0 } },
    { { "name", "count" }, { "type", "int" }, { "value", 4 } }
  });

  TEST(ScriptFieldLabel, UsesTheDisplayNameWhenPresent)
  {
    EXPECT_EQ(scriptFieldLabel(labeledFields.at(0)), "Top Speed");
  }

  TEST(ScriptFieldLabel, FallsBackToTheNameWhenTheDisplayNameIsAbsent)
  {
    EXPECT_EQ(scriptFieldLabel(labeledFields.at(1)), "count");
  }

  TEST(ScriptFieldLabel, FallsBackToTheNameWhenTheDisplayNameIsEmptyOrNotAString)
  {
    EXPECT_EQ(scriptFieldLabel({ { "name", "m_a" }, { "displayName", "" } }), "m_a");
    EXPECT_EQ(scriptFieldLabel({ { "name", "m_a" }, { "displayName", 5 } }), "m_a");
  }

  TEST(ScriptFieldLabel, ScriptSerializeAndLoadRoundTripTheDisplayName)
  {
    Script source("Mover");
    source.setFields(labeledFields);

    Script loaded;
    loaded.loadFromJSON(source.serialize());

    EXPECT_EQ(loaded.getFields(), labeledFields);
    EXPECT_EQ(loaded.getFields().at(0).at("displayName"), "Top Speed");
  }

  TEST(ScriptFieldLabel, ScriptPackAndUnpackRoundTripTheDisplayName)
  {
    Script source("Mover");
    source.setFields(labeledFields);
    net::Message message(net::MessageType::undefined);
    source.pack(message);

    net::MessageReader reader(message);
    static_cast<void>(reader.read<ComponentType>());
    static_cast<void>(reader.readString());
    Script loaded;
    loaded.unpack(reader);

    EXPECT_EQ(loaded.getFields(), labeledFields);
  }

  TEST(ScriptFieldLabel, AFieldEntryWithoutADisplayNameStillLoads)
  {
    const nlohmann::json old = {
      { "type", "Script" },
      { "className", "Mover" },
      { "fields", nlohmann::json::array({ { { "name", "count" }, { "type", "int" }, { "value", 4 } } }) }
    };

    Script loaded;
    loaded.loadFromJSON(old);

    ASSERT_EQ(loaded.getFields().size(), 1u);
    EXPECT_FALSE(loaded.getFields().at(0).contains("displayName"));
    EXPECT_EQ(scriptFieldLabel(loaded.getFields().at(0)), "count");
  }
}
