#include <gtest/gtest.h>

#include "Gizmo.h"
#include "GizmoVisibility.h"
#include "EditorGizmoSettings.h"

TEST(GizmoVisibilityTest, DrawnThroughTheEditorCamera)
{
  EXPECT_TRUE(shouldDrawTransformGizmo(false));
}

TEST(GizmoVisibilityTest, HiddenThroughASceneCamera)
{
  EXPECT_TRUE(shouldDrawTransformGizmo(false));
  EXPECT_FALSE(shouldDrawTransformGizmo(true));
}

TEST(GizmoDefaultsTest, StateStartsInLocalSpace)
{
  const gizmo::State state;

  EXPECT_EQ(state.space, gizmo::Space::local);
  EXPECT_EQ(state.dragSpace, gizmo::Space::local);
}

TEST(GizmoDefaultsTest, SnapIsOffByDefault)
{
  EXPECT_FALSE(editorGizmoSettings::defaultSnapEnabled);
}
