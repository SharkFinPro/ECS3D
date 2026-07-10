#ifndef ASSETINSPECTOR_H
#define ASSETINSPECTOR_H

#include <memory>

struct AssetRecord;
class GpuAssetCache;

// The Inspector's renderer for the Asset selection kind: a common header (type chip, display name, uuid,
// path/source) shared by every asset type, plus a per-type body added in later Phase 2 tasks. Read-only
// in Phase 2 — there is no protocol for asset mutation yet. Peer to ObjectInspector behind
// InspectorPanel's per-selection-kind dispatch.
class AssetInspector {
public:
  explicit AssetInspector(std::shared_ptr<GpuAssetCache> assetCache);

  // The right-aligned type chip in the panel header, drawn on the current header row.
  void displayTypeChip(const AssetRecord& record) const;

  // The asset body: the common metadata header, then the per-type detail.
  void display(const AssetRecord& record);

private:
  std::shared_ptr<GpuAssetCache> m_assetCache;

  // Display name + uuid + path/source rows common to every asset type.
  void displayHeader(const AssetRecord& record) const;
};

#endif //ASSETINSPECTOR_H
