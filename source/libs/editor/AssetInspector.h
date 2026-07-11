#ifndef ASSETINSPECTOR_H
#define ASSETINSPECTOR_H

#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <uuid.h>

struct AssetRecord;
class GpuAssetCache;

// The Inspector's renderer for the Asset selection kind: a common header (type chip, display name, uuid,
// path/source) shared by every asset type, plus a per-type body. Read-only in Phase 2 — there is no
// protocol for asset mutation yet. Peer to ObjectInspector behind InspectorPanel's per-selection-kind
// dispatch.
class AssetInspector {
public:
  // Switching the active scene: the same callback the asset browser's scene double-click uses.
  using LoadSceneCallback = std::function<void(const uuids::uuid& sceneUUID)>;

  explicit AssetInspector(std::shared_ptr<GpuAssetCache> assetCache);

  void setLoadSceneCallback(LoadSceneCallback callback);

  // When false (read-only server), the scene inspector's "Load Scene" button is disabled, mirroring the
  // browser's gated double-click.
  void setEditable(bool editable);

  // The right-aligned type chip in the panel header, drawn on the current header row.
  void displayTypeChip(const AssetRecord& record) const;

  // The asset body: the common metadata header, then per-type detail. activeSceneUUID is the currently
  // loaded scene (for the scene inspector's is-active indicator); nullopt when no scene is loaded.
  void display(const AssetRecord& record, const std::optional<uuids::uuid>& activeSceneUUID);

private:
  std::shared_ptr<GpuAssetCache> m_assetCache;

  LoadSceneCallback m_onLoadScene;

  bool m_editable = true;

  // File metadata (size, image dimensions) for the currently-shown asset, recomputed only when the
  // selected asset changes so the panel isn't hitting disk every frame.
  uuids::uuid m_metaUUID{};
  bool m_metaLoaded = false;
  std::string m_fileSize;       // empty when the file isn't present editor-side
  bool m_haveImageSize = false;
  int m_imageWidth = 0;
  int m_imageHeight = 0;
  bool m_haveMeshStats = false; // model mesh stats resolved from the loaded vke::Model
  size_t m_vertexCount = 0;
  size_t m_indexCount = 0;
  bool m_haveScriptSource = false; // script .cs source read from disk
  std::string m_scriptSource;

  // A prefab body walked into a read-only tree (no ObjectManager instantiation): each node is an object
  // name + its component type names + child nodes.
  struct PrefabNode {
    std::string name;
    std::vector<std::string> components;
    std::vector<PrefabNode> children;
  };
  bool m_havePrefab = false;
  PrefabNode m_prefabRoot;

  // Display name + uuid + path/source rows common to every asset type.
  void displayHeader(const AssetRecord& record) const;

  // Refreshes the file-metadata cache above for `record` (no-op work when its file isn't reachable).
  void refreshMeta(const AssetRecord& record);

  // Large image preview (falling back to the type icon like the browser tiles) + dimensions + size.
  void displayTextureBody(const AssetRecord& record);

  // Type icon (3D preview deferred, see B3), file format + size, and mesh stats.
  void displayModelBody(const AssetRecord& record);

  // Read-only .cs source preview (from the cached source, loaded editor-side).
  void displayScriptBody();

  // Is-active indicator + a "Load Scene" button (reusing LoadSceneCallback, gated on editable).
  void displaySceneBody(const AssetRecord& record, const std::optional<uuids::uuid>& activeSceneUUID);

  // Read-only summary of the prefab body: root name, component types, child object tree.
  void displayPrefabBody();

  void displayPrefabNode(const PrefabNode& node, bool root) const;

  // Walks one serialized-Object JSON node into a PrefabNode (recursing into children).
  static PrefabNode parsePrefabNode(const nlohmann::json& node);
};

#endif //ASSETINSPECTOR_H
