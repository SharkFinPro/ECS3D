#include <gtest/gtest.h>

#include "terrain/TerrainBrush.h"

#include <glm/vec3.hpp>
#include <Protocol.h>
#include <limits>
#include <stdexcept>

namespace {
  using Kind = terrain::BrushOp::Kind;

  terrain::BrushOp sampleOp(const Kind kind)
  {
    terrain::BrushOp op;
    op.kind = kind;
    op.center = { 80.0f, 12.0f, 80.0f };
    op.radius = 20.0f;
    op.strength = 0.8f;
    op.falloff = 0.5f;
    op.material = 7;
    op.planePoint = { 80.0f, 14.0f, 80.0f };
    op.planeNormal = { 0.1f, 1.0f, 0.0f };
    return op;
  }

  net::Message packed(const terrain::BrushOp& op)
  {
    net::Message message(net::MessageType::snapshot);
    terrain::pack(message, op);
    return message;
  }
}

TEST(TerrainBrushWireTest, PackRoundTripsEveryKind)
{
  for (const Kind kind : { Kind::add, Kind::subtract, Kind::smooth, Kind::flatten, Kind::paint })
  {
    terrain::BrushOp op = sampleOp(kind);
    op.material = 12;

    const net::Message message = packed(op);
    net::MessageReader reader(message);
    const auto read = terrain::unpackBrushOp(reader);

    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->kind, op.kind);
    EXPECT_EQ(read->center, op.center);
    EXPECT_EQ(read->radius, op.radius);
    EXPECT_EQ(read->strength, op.strength);
    EXPECT_EQ(read->falloff, op.falloff);
    EXPECT_EQ(read->material, op.material);
    EXPECT_EQ(read->planePoint, op.planePoint);
    EXPECT_EQ(read->planeNormal, op.planeNormal);
    EXPECT_EQ(reader.remaining(), 0u);
  }
}

TEST(TerrainBrushWireTest, UnpackRefusesUntrustworthyFields)
{
  const auto refused = [](const auto& spoil)
  {
    terrain::BrushOp op = sampleOp(Kind::flatten);
    spoil(op);
    const net::Message message = packed(op);
    net::MessageReader reader(message);
    return !terrain::unpackBrushOp(reader).has_value();
  };

  EXPECT_FALSE(refused([](terrain::BrushOp&) {}));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.kind = static_cast<Kind>(9); }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.radius = std::numeric_limits<float>::quiet_NaN(); }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.strength = 1.5f; }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.material = 255; }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.radius = 0.0f; }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.center.y = std::numeric_limits<float>::quiet_NaN(); }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.planePoint.z = std::numeric_limits<float>::quiet_NaN(); }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.planeNormal.x = std::numeric_limits<float>::infinity(); }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.strength = std::numeric_limits<float>::quiet_NaN(); }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.falloff = std::numeric_limits<float>::quiet_NaN(); }));
}

TEST(TerrainBrushWireTest, UnpackThrowsOnATruncatedBuffer)
{
  const net::Message full = packed(sampleOp(Kind::add));
  const auto bytes = full.bytes();

  const net::Message whole(net::MessageType::snapshot, bytes);
  net::MessageReader wholeReader(whole);
  EXPECT_TRUE(terrain::unpackBrushOp(wholeReader).has_value());

  const net::Message cut(net::MessageType::snapshot, bytes.first(bytes.size() - 1));
  net::MessageReader cutReader(cut);
  EXPECT_THROW((void)terrain::unpackBrushOp(cutReader), std::runtime_error);
}
