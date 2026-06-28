#ifndef PROJECTPACKER_H
#define PROJECTPACKER_H

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
// file I/O (save/load).
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
  void unpack(const net::Message& message) const;

private:
  AssetRegistry* m_assetRegistry;

  SceneManager* m_sceneManager;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
};



#endif //PROJECTPACKER_H
