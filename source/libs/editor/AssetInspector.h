#ifndef ASSETINSPECTOR_H
#define ASSETINSPECTOR_H

#include <memory>
#include <string>
#include <uuid.h>

struct AssetRecord;
class GpuAssetCache;

// The Inspector's renderer for the Asset selection kind: a common header (type chip, display name, uuid,
// path/source) shared by every asset type, plus a per-type body. Read-only in Phase 2 — there is no
// protocol for asset mutation yet. Peer to ObjectInspector behind InspectorPanel's per-selection-kind
// dispatch.
class AssetInspector {
public:
  explicit AssetInspector(std::shared_ptr<GpuAssetCache> assetCache);

  // The right-aligned type chip in the panel header, drawn on the current header row.
  void displayTypeChip(const AssetRecord& record) const;

  // The asset body: the common metadata header, then per-type detail.
  void display(const AssetRecord& record);

private:
  std::shared_ptr<GpuAssetCache> m_assetCache;

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
};

#endif //ASSETINSPECTOR_H
