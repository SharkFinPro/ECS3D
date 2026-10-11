#include "ScriptSystemTestFixtures.h"

#include "ComponentRegistry.h"
#include "SceneObjectDiff.h"
#include "scenes/SceneAsset.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
  using namespace scriptSystemFixtures;
  using Uuids = std::vector<uuids::uuid>;

  const auto mainSceneUUID = uuids::uuid::from_string("77777777-7777-7777-7777-777777777777").value();

  constexpr int codeLoaded = 0;
  constexpr int codeUnloading = 1;
  constexpr int codeSpawned = 2;
  constexpr int codeDestroyed = 3;
  constexpr int codeJoined = 4;
  constexpr int codeLeft = 5;

  class SceneEventsTest : public ScriptSystemTest {
  protected:
    std::shared_ptr<SceneAsset> mainScene =
      std::make_shared<SceneAsset>(mainSceneUUID, "Main", scene.componentRegistry);

    std::shared_ptr<Script> addSceneScript(const std::string& className) const
    {
      const auto script = std::make_shared<Script>(className);
      script->setFields(nlohmann::json::array());
      EXPECT_TRUE(mainScene->addScript(script));

      return script;
    }

    std::shared_ptr<Object> addObjectScripts(const std::string& name, const std::vector<std::string>& classNames) const
    {
      const auto object = std::make_shared<Object>(name);
      mainScene->getObjectManager()->addObject(object);
      for (const auto& className : classNames)
      {
        addScript(object, className);
      }

      return object;
    }

    [[nodiscard]] static SceneEventCall event(const std::string& className, const int code,
                                              const std::string& object = "", const int slot = -1)
    {
      return { uuids::to_string(mainSceneUUID), className, code, object, slot, 0 };
    }

    [[nodiscard]] static std::string sceneCall(const char* verb, const char* className)
    {
      return describe(verb, uuids::to_string(mainSceneUUID).c_str(), className);
    }
  };

  using Events = std::vector<SceneEventCall>;

  TEST_F(SceneEventsTest, StartDeliversLoadedToEachStartedSceneScriptInListOrderAfterEveryScriptHasStarted)
  {
    addSceneScript("Alpha");
    addSceneScript("Beta");
    const auto object = addObjectScripts("Thing", { "Mover" });

    scriptSystem.start(*mainScene);

    ASSERT_EQ(state.sceneEvents, (Events{ event("Alpha", codeLoaded), event("Beta", codeLoaded) }));

    const auto lastStart = std::max({ indexOf(call("start", object, "Mover")),
                                      indexOf(sceneCall("start", "Alpha")),
                                      indexOf(sceneCall("start", "Beta")) });
    ASSERT_GE(indexOf(call("start", object, "Mover")), 0);
    for (const auto& recorded : state.sceneEvents)
    {
      EXPECT_GT(static_cast<std::ptrdiff_t>(recorded.callsBefore), lastStart);
    }
  }

  TEST_F(SceneEventsTest, StopDeliversUnloadingBeforeAnyScriptIsStopped)
  {
    addSceneScript("Alpha");
    addSceneScript("Beta");
    const auto object = addObjectScripts("Thing", { "Mover" });
    scriptSystem.start(*mainScene);
    state.sceneEvents.clear();

    scriptSystem.stop(*mainScene);

    ASSERT_EQ(state.sceneEvents, (Events{ event("Alpha", codeUnloading), event("Beta", codeUnloading) }));

    const auto firstStop = std::min({ indexOf(call("stop", object, "Mover")),
                                      indexOf(sceneCall("stop", "Alpha")),
                                      indexOf(sceneCall("stop", "Beta")) });
    ASSERT_GE(firstStop, 0);
    for (const auto& recorded : state.sceneEvents)
    {
      EXPECT_LE(static_cast<std::ptrdiff_t>(recorded.callsBefore), firstStop);
    }
  }

  TEST_F(SceneEventsTest, DispatchCarriesTheObjectAndSlotThroughToTheRuntime)
  {
    addSceneScript("Alpha");
    scriptSystem.start(*mainScene);
    state.sceneEvents.clear();

    const auto object = uuids::uuid::from_string("99999999-9999-9999-9999-999999999999").value();
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::objectSpawned, &object);
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::objectDestroyed, &object);
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 3);
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerLeft, nullptr, 7);

    const auto objectText = uuids::to_string(object);
    EXPECT_EQ(state.sceneEvents, (Events{ event("Alpha", codeSpawned, objectText),
                                          event("Alpha", codeDestroyed, objectText),
                                          event("Alpha", codeJoined, "", 3),
                                          event("Alpha", codeLeft, "", 7) }));
  }

  TEST_F(SceneEventsTest, DispatchReachesEveryStartedSceneScriptInListOrder)
  {
    addSceneScript("Beta");
    addSceneScript("Alpha");
    addSceneScript("Gamma");
    scriptSystem.start(*mainScene);
    state.sceneEvents.clear();

    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 1);

    EXPECT_EQ(state.sceneEvents, (Events{ event("Beta", codeJoined, "", 1), event("Alpha", codeJoined, "", 1),
                                          event("Gamma", codeJoined, "", 1) }));
  }

  TEST_F(SceneEventsTest, ASceneScriptThatIsAttachedButNotStartedHearsNothingUntilItStarts)
  {
    addSceneScript("Alpha");
    scriptSystem.attachAll(*mainScene);
    ASSERT_EQ(count(sceneCall("attachScene", "Alpha")), 1);
    ASSERT_EQ(count(sceneCall("start", "Alpha")), 0);

    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 2);

    EXPECT_TRUE(state.sceneEvents.empty());

    scriptSystem.start(*mainScene);
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 2);

    EXPECT_EQ(state.sceneEvents, (Events{ event("Alpha", codeLoaded), event("Alpha", codeJoined, "", 2) }));
  }

  TEST_F(SceneEventsTest, ObjectScriptsNeverReceiveSceneEvents)
  {
    addSceneScript("Alpha");
    const auto object = addObjectScripts("Thing", { "Mover" });
    scriptSystem.start(*mainScene);
    ASSERT_EQ(count(call("start", object, "Mover")), 1);

    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 4);

    ASSERT_EQ(state.sceneEvents.size(), 2u);
    EXPECT_EQ(state.sceneEvents[0], event("Alpha", codeLoaded));
    EXPECT_EQ(state.sceneEvents[1], event("Alpha", codeJoined, "", 4));
    for (const auto& recorded : state.sceneEvents)
    {
      EXPECT_NE(recorded.className, "Mover");
      EXPECT_NE(recorded.scene, uuids::to_string(object->getUUID()));
    }
  }

  TEST_F(SceneEventsTest, AnUnresolvedSceneScriptIsSkippedWhileItsNeighborStillHearsTheEvent)
  {
    state.unresolvedClasses.insert("Broken");
    addSceneScript("Broken");
    addSceneScript("Alpha");

    scriptSystem.start(*mainScene);
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerLeft, nullptr, 5);

    EXPECT_EQ(state.sceneEvents, (Events{ event("Alpha", codeLoaded), event("Alpha", codeLeft, "", 5) }));
  }

  TEST_F(SceneEventsTest, NothingReachesTheRuntimeWhenNoSceneScriptIsStarted)
  {
    addObjectScripts("Thing", { "Mover" });
    state.unresolvedClasses.insert("Broken");
    addSceneScript("Broken");

    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 1);
    scriptSystem.start(*mainScene);
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 1);
    scriptSystem.stop(*mainScene);
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 1);

    EXPECT_TRUE(state.sceneEvents.empty());

    addSceneScript("Alpha");
    scriptSystem.start(*mainScene);
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 1);

    EXPECT_EQ(state.sceneEvents.size(), 2u);
  }

  TEST_F(SceneEventsTest, AStoppedSceneHearsNothingMoreAfterItsUnloading)
  {
    addSceneScript("Alpha");
    scriptSystem.start(*mainScene);
    scriptSystem.stop(*mainScene);
    state.sceneEvents.clear();

    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 1);

    EXPECT_TRUE(state.sceneEvents.empty());

    scriptSystem.start(*mainScene);

    EXPECT_EQ(state.sceneEvents, (Events{ event("Alpha", codeLoaded) }));
  }

  TEST_F(SceneEventsTest, AHandlerThatSpawnsLeavesTheObjectPendingUntilTheNextFlush)
  {
    addSceneScript("Alpha");
    scriptSystem.start(*mainScene);
    const auto objects = mainScene->getObjectManager();
    const auto spawned = std::make_shared<Object>("Spawned");
    state.sceneEventHook = [&](const SceneEventCall&) { objects->addObject(spawned); };

    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 1);

    ASSERT_EQ(state.sceneEvents.back().event, codeJoined);
    EXPECT_EQ(std::ranges::count(objects->getAllObjects(), spawned), 0);
    EXPECT_EQ(objects->getObjectByUUID(spawned->getUUID()), spawned);

    objects->flushPendingAdditions();
    EXPECT_EQ(std::ranges::count(objects->getAllObjects(), spawned), 1);

    state.sceneEventHook = nullptr;
    const auto later = std::make_shared<Object>("Later");
    objects->addObject(later);
    EXPECT_EQ(std::ranges::count(objects->getAllObjects(), later), 1);
  }

  TEST_F(SceneEventsTest, DispatchPointsTheBindingsAtTheScenesObjectManager)
  {
    addSceneScript("Alpha");
    scriptSystem.start(*mainScene);
    BindingContext::setObjectManager(nullptr);

    ObjectManager* seen = nullptr;
    state.sceneEventHook = [&](const SceneEventCall&) { seen = BindingContext::getObjectManager(); };
    scriptSystem.dispatchSceneEvent(*mainScene, SceneEvent::playerJoined, nullptr, 1);

    EXPECT_EQ(seen, mainScene->getObjectManager().get());
  }

  class SceneObjectDiffTest : public ::testing::Test {
  protected:
    fixtures::Scene scene = fixtures::makeScene();

    [[nodiscard]] Uuids collect() const
    {
      return sceneObjectDiff::collectUUIDs(*scene.objectManager);
    }
  };

  TEST_F(SceneObjectDiffTest, CollectUUIDsListsParentsBeforeChildrenDepthFirst)
  {
    const auto a = addObject(scene, "A");
    const auto b = addChildObject(scene, "B", a);
    const auto c = addChildObject(scene, "C", a);
    const auto d = addChildObject(scene, "D", b);
    const auto e = addObject(scene, "E");

    EXPECT_EQ(collect(), (Uuids{ a->getUUID(), b->getUUID(), d->getUUID(), c->getUUID(), e->getUUID() }));
  }

  TEST_F(SceneObjectDiffTest, FlattenSubtreesIsRootFirstDepthFirstAcrossRoots)
  {
    const auto a = addObject(scene, "A");
    const auto b = addChildObject(scene, "B", a);
    const auto c = addChildObject(scene, "C", b);
    const auto other = addObject(scene, "Other");

    EXPECT_EQ(sceneObjectDiff::flattenSubtrees({ a, other }),
              (Uuids{ a->getUUID(), b->getUUID(), c->getUUID(), other->getUUID() }));
    EXPECT_EQ(sceneObjectDiff::flattenSubtrees({ b }), (Uuids{ b->getUUID(), c->getUUID() }));
    EXPECT_TRUE(sceneObjectDiff::flattenSubtrees({}).empty());
  }

  TEST_F(SceneObjectDiffTest, AddingASubtreeReportsEveryNewObjectRootFirst)
  {
    const auto existing = addObject(scene, "Existing");
    const auto before = collect();
    ASSERT_EQ(before, (Uuids{ existing->getUUID() }));

    const auto root = addObject(scene, "Root");
    const auto child = addChildObject(scene, "Child", root);
    const auto grandchild = addChildObject(scene, "Grandchild", child);

    const auto result = sceneObjectDiff::diff(before, collect());

    EXPECT_EQ(result.added, (Uuids{ root->getUUID(), child->getUUID(), grandchild->getUUID() }));
    EXPECT_TRUE(result.removed.empty());
  }

  TEST_F(SceneObjectDiffTest, RemovingAnObjectPromotesItsChildrenSoOnlyTheObjectIsReported)
  {
    const auto parent = addObject(scene, "Parent");
    const auto childA = addChildObject(scene, "ChildA", parent);
    const auto childB = addChildObject(scene, "ChildB", parent);
    const auto before = collect();

    scene.objectManager->removeObject(parent);
    scene.objectManager->deleteObjectsMarkedForDeletion();

    const auto after = collect();
    const auto result = sceneObjectDiff::diff(before, after);

    EXPECT_EQ(result.removed, (Uuids{ parent->getUUID() }));
    EXPECT_TRUE(result.added.empty());
    EXPECT_EQ(after, (Uuids{ childA->getUUID(), childB->getUUID() }));
  }

  TEST_F(SceneObjectDiffTest, RemovingASubtreeReportsEveryObjectInIt)
  {
    const auto keep = addObject(scene, "Keep");
    const auto parent = addObject(scene, "Parent");
    const auto childA = addChildObject(scene, "ChildA", parent);
    const auto grandchild = addChildObject(scene, "Grandchild", childA);
    const auto childB = addChildObject(scene, "ChildB", parent);
    const auto before = collect();

    scene.objectManager->removeSubtree(parent);

    const auto after = collect();
    const auto result = sceneObjectDiff::diff(before, after);

    EXPECT_EQ(result.removed, (Uuids{ parent->getUUID(), childA->getUUID(), grandchild->getUUID(),
                                      childB->getUUID() }));
    EXPECT_TRUE(result.added.empty());
    EXPECT_EQ(after, (Uuids{ keep->getUUID() }));
  }

  TEST_F(SceneObjectDiffTest, AnUnchangedSceneReportsNothingAndAnAddPlusRemoveReportsBoth)
  {
    const auto a = addObject(scene, "A");
    const auto b = addObject(scene, "B");
    const auto before = collect();

    const auto unchanged = sceneObjectDiff::diff(before, collect());
    EXPECT_TRUE(unchanged.added.empty());
    EXPECT_TRUE(unchanged.removed.empty());

    scene.objectManager->removeSubtree(a);
    const auto c = addObject(scene, "C");
    const auto result = sceneObjectDiff::diff(before, collect());

    EXPECT_EQ(result.added, (Uuids{ c->getUUID() }));
    EXPECT_EQ(result.removed, (Uuids{ a->getUUID() }));
    EXPECT_EQ(collect(), (Uuids{ b->getUUID(), c->getUUID() }));
  }
}
