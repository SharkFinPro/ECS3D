#ifndef PROJECTPACKER_H
#define PROJECTPACKER_H

#include <uuid.h>
#include <cstdint>
#include <memory>

class AssetRegistry;
class SceneManager;
class ComponentRegistry;

namespace net {
  class Message;
}

// The binary counterpart of ProjectSerializer's serialize()/deserialize, used exclusively for the
// network Snapshot. It produces the same full project state (assets + scenes + current scene) as a
// tightly packed binary blob instead of JSON, reusing the existing pack()/unpack() on AssetRegistry,
// SceneAsset, ObjectManager, Object, and the components. ProjectSerializer stays the JSON path for
// file I/O (save/load). A snapshot also ends with a session-only project load id, which names the load of
// a project it belongs to so an editor can tell another editor's loadProject from a plain re-snapshot.
class ProjectPacker {
public:
  ProjectPacker(AssetRegistry* assetRegistry,
                SceneManager* sceneManager,
                std::shared_ptr<ComponentRegistry> componentRegistry);

  // Writes the full project state into message (typically a MessageType::snapshot message).
  void pack(net::Message& message) const;

  // Rebuilds the project from a packed snapshot. Like deserialize(), it parses into local instances
  // first and only commits (clearing + replacing the live state) once everything has parsed, so a
  // malformed packet leaves the current project intact.
  void unpack(const net::Message& message);

  void setProjectLoadId(const uuids::uuid& id);

  [[nodiscard]] const uuids::uuid& getProjectLoadId() const;

  // The fingerprint (ProjectFingerprint.h) of the project file the server loaded, zero for none. It ends the
  // snapshot after the load id, so an editor attached with --host can tell whether the server holds the
  // project it started with.
  void setProjectFingerprint(uint64_t fingerprint);

  [[nodiscard]] uint64_t getProjectFingerprint() const;

private:
  AssetRegistry* m_assetRegistry;

  SceneManager* m_sceneManager;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;

  uuids::uuid m_projectLoadId;

  uint64_t m_projectFingerprint = 0;
};



#endif //PROJECTPACKER_H
