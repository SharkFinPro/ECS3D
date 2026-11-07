#include <gtest/gtest.h>

#include "ObjectManagerFixtures.h"
#include <ObjectGUIManager.h>
#include <Selection.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <memory>
#include <string>

namespace {
  constexpr float windowWidth = 300.0f;
  constexpr float windowHeight = 240.0f;

  class ObjectTreeRowCullingTest : public testing::Test {
  protected:
    void SetUp() override
    {
      m_context = ImGui::CreateContext();
      ImGuiIO& io = ImGui::GetIO();
      io.IniFilename = nullptr;
      io.LogFilename = nullptr;
      io.DisplaySize = ImVec2(800.0f, 600.0f);
      io.DeltaTime = 1.0f / 60.0f;
      io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

      m_gui.setSelection(std::make_shared<EditorSelection>());
      m_gui.setEditable(true);
      m_gui.setSceneEditCallback([](const nlohmann::json&) {});
    }

    void TearDown() override
    {
      ImGui::DestroyContext(m_context);
    }

    void addRoots(const int count)
    {
      for (int i = 0; i < count; ++i)
      {
        objectManagerFixtures::addObject(m_scene, "Object " + std::to_string(i));
      }
    }

    // Runs one frame of the panel in a fixed-size window and returns the vertices it produced.
    int renderFrame()
    {
      ImGui::NewFrame();
      ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);
      m_gui.displayGui(m_scene.objectManager.get());
      ImGui::Render();

      return ImGui::GetDrawData()->TotalVtxCount;
    }

    // Two frames, so layout and scroll limits have settled before the count is read.
    int settledVertexCount()
    {
      renderFrame();
      return renderFrame();
    }

    void scrollTo(const float y)
    {
      ImGui::SetScrollY(ImGui::FindWindowByName("Objects"), y);
    }

    [[nodiscard]] float maxScroll() const
    {
      return ImGui::FindWindowByName("Objects")->ScrollMax.y;
    }

    ImGuiContext* m_context = nullptr;
    objectManagerFixtures::Scene m_scene = objectManagerFixtures::makeScene();
    ObjectGUIManager m_gui;
  };
}

TEST_F(ObjectTreeRowCullingTest, VisibleRowsDrawGeometryThatGrowsWithTheRowCount)
{
  addRoots(2);
  const int fewer = settledVertexCount();

  addRoots(4);
  const int more = settledVertexCount();

  EXPECT_GT(fewer, 0);
  EXPECT_GT(more, fewer);
}

TEST_F(ObjectTreeRowCullingTest, RowsScrolledOutOfViewAddNoGeometry)
{
  addRoots(40);
  const int forty = settledVertexCount();
  ASSERT_GT(maxScroll(), 0.0f);

  addRoots(360);
  const int fourHundred = settledVertexCount();

  EXPECT_GT(forty, 0);
  EXPECT_LE(fourHundred, forty + forty / 10);
}

TEST_F(ObjectTreeRowCullingTest, ScrollingBringsRowsBackIntoView)
{
  addRoots(400);
  settledVertexCount();
  ASSERT_GT(maxScroll(), 0.0f);

  // Both positions are past the header, so they show the same number of rows apart from partial ones at the
  // window edges; a row's worth of vertices bounds that difference.
  scrollTo(maxScroll() * 0.25f);
  const int quarter = settledVertexCount();
  scrollTo(maxScroll() * 0.5f);
  const int middle = settledVertexCount();

  EXPECT_GT(quarter, 0);
  EXPECT_GT(middle, 0);
  EXPECT_NEAR(static_cast<double>(middle), static_cast<double>(quarter), quarter * 0.15);
}
