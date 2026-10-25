#include <gtest/gtest.h>

#include <Log.h>
#include <MissingAssetFallback.h>
#include <RingBufferSink.h>

#include <memory>
#include <string>

namespace {
  struct DummyModel {};
  struct DummyTexture {};

  using Assets = RenderAssets<DummyModel, DummyTexture>;

  class MissingAssetFallbackTest : public testing::Test {
  protected:
    void SetUp() override
    {
      m_sink = std::make_shared<RingBufferSink>(100);
      Log::addSink(m_sink);
    }

    void TearDown() override
    {
      Log::removeSink(m_sink);
    }

    std::shared_ptr<RingBufferSink> m_sink;
    std::shared_ptr<DummyModel> m_model = std::make_shared<DummyModel>();
    std::shared_ptr<DummyTexture> m_texture = std::make_shared<DummyTexture>();
    std::shared_ptr<DummyTexture> m_specular = std::make_shared<DummyTexture>();
    std::shared_ptr<DummyModel> m_placeholderModel = std::make_shared<DummyModel>();
    std::shared_ptr<DummyTexture> m_placeholderTexture = std::make_shared<DummyTexture>();
  };
}

TEST_F(MissingAssetFallbackTest, CompleteAssetsAreReturnedUnchanged)
{
  const auto result = withPlaceholders(Assets{ m_model, m_texture, m_specular }, m_placeholderModel,
                                       m_placeholderTexture);

  EXPECT_EQ(result.model, m_model);
  EXPECT_EQ(result.texture, m_texture);
  EXPECT_EQ(result.specularMap, m_specular);
}

TEST_F(MissingAssetFallbackTest, MissingModelDrawsPlaceholderModelWithPlaceholderTexture)
{
  const auto result = withPlaceholders(Assets{ nullptr, m_texture, m_specular }, m_placeholderModel,
                                       m_placeholderTexture);

  EXPECT_EQ(result.model, m_placeholderModel);
  EXPECT_EQ(result.texture, m_placeholderTexture);
  EXPECT_EQ(result.specularMap, m_specular);
}

TEST_F(MissingAssetFallbackTest, MissingTextureKeepsTheModelAndSpecular)
{
  const auto result = withPlaceholders(Assets{ m_model, nullptr, m_specular }, m_placeholderModel,
                                       m_placeholderTexture);

  EXPECT_EQ(result.model, m_model);
  EXPECT_EQ(result.texture, m_placeholderTexture);
  EXPECT_EQ(result.specularMap, m_specular);
}

TEST_F(MissingAssetFallbackTest, MissingSpecularMapUsesThePlaceholderTexture)
{
  const auto result = withPlaceholders(Assets{ m_model, m_texture, nullptr }, m_placeholderModel,
                                       m_placeholderTexture);

  EXPECT_EQ(result.model, m_model);
  EXPECT_EQ(result.texture, m_texture);
  EXPECT_EQ(result.specularMap, m_placeholderTexture);
}

TEST_F(MissingAssetFallbackTest, AFailedPlaceholderLeavesItsSlotEmpty)
{
  const Assets missing{ nullptr, nullptr, nullptr };

  const auto filled = withPlaceholders(missing, m_placeholderModel, m_placeholderTexture);
  EXPECT_NE(filled.model, nullptr);
  EXPECT_NE(filled.texture, nullptr);
  EXPECT_NE(filled.specularMap, nullptr);

  const auto noModel = withPlaceholders(missing, std::shared_ptr<DummyModel>(), m_placeholderTexture);
  EXPECT_EQ(noModel.model, nullptr);
  EXPECT_NE(noModel.texture, nullptr);

  const auto noTexture = withPlaceholders(missing, m_placeholderModel, std::shared_ptr<DummyTexture>());
  EXPECT_NE(noTexture.model, nullptr);
  EXPECT_EQ(noTexture.texture, nullptr);
  EXPECT_EQ(noTexture.specularMap, nullptr);
}

TEST_F(MissingAssetFallbackTest, AssetsCompareByPointer)
{
  EXPECT_EQ((Assets{ m_model, m_texture, m_specular }), (Assets{ m_model, m_texture, m_specular }));
  EXPECT_NE((Assets{ m_model, m_texture, m_specular }), (Assets{ m_model, m_texture, nullptr }));
}

TEST_F(MissingAssetFallbackTest, LogsOncePerKeyAndNamesTheLabel)
{
  MissingAssetLog missing;

  missing.report("a", "models/gone.glb (a)");
  missing.report("a", "models/gone.glb (a)");
  ASSERT_EQ(m_sink->snapshot().size(), 1u);
  EXPECT_EQ(m_sink->snapshot()[0].level, LogLevel::warn);
  EXPECT_NE(m_sink->snapshot()[0].message.find("models/gone.glb (a)"), std::string::npos);

  missing.report("b", "textures/gone.png (b)");
  ASSERT_EQ(m_sink->snapshot().size(), 2u);
  EXPECT_NE(m_sink->snapshot()[1].message.find("textures/gone.png (b)"), std::string::npos);
}

TEST_F(MissingAssetFallbackTest, ClearLetsAKeyLogAgain)
{
  MissingAssetLog missing;

  missing.report("a", "first");
  missing.report("a", "first");
  ASSERT_EQ(m_sink->snapshot().size(), 1u);

  missing.clear();
  missing.report("a", "first");
  EXPECT_EQ(m_sink->snapshot().size(), 2u);
}
