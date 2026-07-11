#ifndef ASSETINSPECTOR_H
#define ASSETINSPECTOR_H

#include <nlohmann/json_fwd.hpp>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <uuid.h>

struct AssetRecord;
class AssetRegistry;
class ComponentRegistry;
class ComponentEditor;
class GpuAssetCache;
class ObjectInspector;
class TransientObject;

// The Inspector's renderer for the Asset selection kind: a common header (type chip, display name, uuid,
// path/source) shared by every asset type, plus a per-type body. Most views are read-only; the Prefab body
// is editable (Phase 4) — its contents are deserialized into a detached TransientObject and edited with the
// same component editors an object uses (a reused ObjectInspector), each edit re-serialized and sent as an
// asset body update. Peer to ObjectInspector behind InspectorPanel's per-selection-kind dispatch.
class AssetInspector {
public:
  // Switching the active scene: the same callback the asset browser's scene double-click uses.
  using LoadSceneCallback = std::function<void(const uuids::uuid& sceneUUID)>;
  // A display-name rename: the EditorApp applies it locally and sends a renameAsset op.
  using RenameCallback = std::function<void(const uuids::uuid& assetUUID, const std::string& displayName)>;
  // A delete: the EditorApp applies it locally and sends a removeAsset op.
  using RemoveCallback = std::function<void(const uuids::uuid& assetUUID)>;
  // How many objects (across the replicated scenes + prefab bodies) reference an asset by uuid, shown in
  // the delete-confirmation modal. Computed by the EditorApp, which owns the scenes and the registry.
  using ReferenceCountCallback = std::function<int(const uuids::uuid& assetUUID)>;
  // A prefab body edit: the EditorApp re-registers the prefab under its existing name (which updates the
  // body in place, keeping the uuid — the "Save as Prefab" over an existing name path) and re-snapshots.
  using UpdatePrefabBodyCallback = std::function<void(const uuids::uuid& assetUUID, const std::string& name,
                                                      const std::string& body)>;

  AssetInspector(std::shared_ptr<GpuAssetCache> assetCache,
                 std::shared_ptr<ComponentRegistry> componentRegistry,
                 std::shared_ptr<ComponentEditor> componentEditor);

  ~AssetInspector();

  void setLoadSceneCallback(LoadSceneCallback callback);

  void setRenameCallback(RenameCallback callback);

  void setRemoveCallback(RemoveCallback callback);

  void setReferenceCountCallback(ReferenceCountCallback callback);

  void setUpdatePrefabBodyCallback(UpdatePrefabBodyCallback callback);

  // The registry backs the reused prefab-body ObjectInspector's script drop zone (resolving a dropped
  // script asset to its class name). Same pointer the panel hands the object inspector.
  void setAssetRegistry(const AssetRegistry* registry);

  // When false (read-only server), the scene inspector's "Load Scene" button is disabled (mirroring the
  // browser's gated double-click) and the prefab body's editors are disabled (read-only inspection).
  void setEditable(bool editable);

  // The right-aligned type chip in the panel header, drawn on the current header row.
  void displayTypeChip(const AssetRecord& record) const;

  // The asset body: the common metadata header, then per-type detail. activeSceneUUID is the currently
  // loaded scene (for the scene inspector's is-active indicator); nullopt when no scene is loaded.
  void display(const AssetRecord& record, const std::optional<uuids::uuid>& activeSceneUUID);

private:
  std::shared_ptr<GpuAssetCache> m_assetCache;
  std::shared_ptr<ComponentRegistry> m_componentRegistry;

  LoadSceneCallback m_onLoadScene;
  RenameCallback m_onRename;
  RemoveCallback m_onRemove;
  ReferenceCountCallback m_onReferenceCount;
  UpdatePrefabBodyCallback m_onUpdatePrefabBody;

  bool m_editable = true;

  // The detached prefab body being edited + the object inspector that draws its component editors, reused
  // verbatim from the Object kind. The inspector's callbacks feed the two buffers below rather than the
  // network: structural edits are deferred (applied after its display returns, since applying mid-display
  // would invalidate the component map it iterates), value edits just flag a re-serialize.
  std::unique_ptr<TransientObject> m_prefabBody;
  std::unique_ptr<ObjectInspector> m_prefabInspector;
  std::vector<nlohmann::json> m_pendingPrefabEdits;
  bool m_prefabValueEdited = false;

  // Buffer for the in-place display-name field. Re-seeded (from the effective display name) whenever the
  // selected asset changes, so an external rename doesn't clobber what the user is typing — matching
  // ObjectInspector's name buffer.
  std::array<char, 256> m_nameEditBuffer{};
  std::optional<uuids::uuid> m_nameEditUUID;

  // The asset awaiting delete confirmation (set by the "Delete Asset" button). While set, the modal is
  // shown; m_pendingRefCount is the object count computed once when the prompt opened.
  std::optional<uuids::uuid> m_assetPendingDeletion;
  int m_pendingRefCount = 0;

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

  // Display name (an editable rename field for the flat file assets, gated on editable; read-only text for
  // scenes) + uuid + path/source rows common to every asset type.
  void displayHeader(const AssetRecord& record);

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

  // Editable prefab contents: syncs the detached TransientObject from the record body, draws it with the
  // reused ObjectInspector, then applies any deferred edit and sends the re-serialized body. Read-only
  // (disabled editors) when !m_editable. Shows a fallback when the body is missing/malformed.
  void displayPrefabBody(const AssetRecord& record);

  // A danger "Delete Asset" button (flat file assets only, gated on editable) that arms the modal below.
  void displayDeleteButton(const AssetRecord& record);

  // The "Delete Asset?" confirmation modal for m_assetPendingDeletion, warning how many objects reference
  // it (references are left to dangle — see ROADMAP B1). Confirming fires the remove callback.
  void displayDeleteConfirmationModal(const AssetRecord& record);
};

#endif //ASSETINSPECTOR_H
