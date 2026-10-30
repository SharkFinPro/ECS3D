#ifndef PREFABBODYEDITS_H
#define PREFABBODYEDITS_H

#include "TransientObject.h"
#include <Replication.h>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

class ComponentRegistry;

// The decisions behind editing a prefab body in place, without any drawing: the detached object being
// edited, which asset it belongs to, and when an edit is sent.
//
// Unlike a live object's component edit (a small editComponent message the server just rebroadcasts), a
// prefab body update is an addAsset that makes the server re-snapshot the whole project. Sending one per
// drag frame swamps every view, so edits mutate the detached object live, set the dirty flag, and are only
// serialized + sent once the user stops interacting (no active widget) or switches selection - turning a
// whole slider drag into a single body update. The record uuid/name say which asset a pending edit belongs
// to, so a flush triggered by a selection change sends it under the right key.
class PrefabBodyEdits {
public:
  // Re-registers the prefab under its name: the EditorApp updates the body in place, keeping the uuid.
  using SendCallback = std::function<void(const uuids::uuid& assetUUID, const std::string& name,
                                          const std::string& body)>;

  explicit PrefabBodyEdits(std::shared_ptr<ComponentRegistry> componentRegistry)
    : m_body(std::move(componentRegistry))
  {}

  void setSendCallback(SendCallback callback)
  {
    m_send = std::move(callback);
  }

  // Run by settle() before the queued edits are applied, so an edit the reused inspector is still holding
  // (a name typed but not yet committed) lands in the queue and the dirty flag first.
  void setCommitCallback(std::function<void()> callback)
  {
    m_commit = std::move(callback);
  }

  // A value edit already mutated the component in place.
  void markValueEdit()
  {
    m_dirty = true;
  }

  // A structural edit is deferred: applying it while the inspector is still iterating the component map
  // would invalidate that map.
  void queueStructuralEdit(const nlohmann::json& edit)
  {
    m_pendingEdits.push_back(edit);
  }

  void clearQueuedEdits()
  {
    m_pendingEdits.clear();
  }

  // Moves to the record being shown, (re)building the detached object when its body changed under us (a
  // different prefab selected, or an external snapshot). Switching to a different prefab with an edit still
  // pending flushes it against the asset it belongs to (the record held until now) before the rebuild. A
  // flushed edit markSyncs the sent body, so its own echo does not rebuild. Returns whether it rebuilt.
  bool select(const uuids::uuid& assetUUID, const std::string& name, const std::string& body)
  {
    if (assetUUID != m_recordUUID)
    {
      settle();
    }

    if (!m_body.syncFromBody(body))
    {
      return false;
    }

    // The pending edit (if any) belongs to whatever we just loaded.
    m_recordUUID = assetUUID;
    m_recordName = name;

    return true;
  }

  // Once the inspector's display returned: applies the queued structural edits, then sends the body if it
  // is dirty and no widget is active.
  void endFrame(const bool anyItemActive)
  {
    applyQueuedEdits();

    if (m_dirty && !anyItemActive)
    {
      flush();
    }
  }

  // Resolves everything pending against the body it was made on, before that body is swapped or stops
  // being drawn.
  void settle()
  {
    m_pendingEdits.clear();

    if (m_commit)
    {
      m_commit();
    }

    applyQueuedEdits();
    flush();
  }

  [[nodiscard]] const std::shared_ptr<Object>& object() const
  {
    return m_body.object();
  }

private:
  TransientObject m_body;
  SendCallback m_send;
  std::function<void()> m_commit;
  std::vector<nlohmann::json> m_pendingEdits;

  bool m_dirty = false;
  uuids::uuid m_recordUUID{};
  std::string m_recordName;

  void applyQueuedEdits()
  {
    if (auto* manager = m_body.manager())
    {
      for (const auto& edit : m_pendingEdits)
      {
        // Only a change that took makes the body dirty. Flagging it regardless re-sent an unchanged prefab
        // body to the server, and a prefab body update makes it re-snapshot the whole project.
        if (replication::applySceneEdit(*manager, edit) == replication::SceneEditResult::applied)
        {
          m_dirty = true;
        }
      }
    }

    m_pendingEdits.clear();
  }

  void flush()
  {
    if (!m_dirty)
    {
      return;
    }

    m_dirty = false;

    if (!m_send || !m_body.object())
    {
      return;
    }

    // markSynced so the resulting registry update (and the server's snapshot echo) does not rebuild the
    // object.
    const auto newBody = m_body.serialize();
    m_send(m_recordUUID, m_recordName, newBody);
    m_body.markSynced(newBody);
  }
};

#endif //PREFABBODYEDITS_H
