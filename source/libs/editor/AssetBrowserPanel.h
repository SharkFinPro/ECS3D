#ifndef ASSETBROWSERPANEL_H
#define ASSETBROWSERPANEL_H

#include <assets/AssetRegistry.h>

class AssetRegistry;

// The editor's "Assets" panel: lists the replicated AssetRegistry records (searchable, type-filtered)
// and exposes each as a drag-drop source carrying its uuid, so the component editors (e.g.
// ModelRenderer) can accept a dragged asset.
class AssetBrowserPanel {
public:
  explicit AssetBrowserPanel(const AssetRegistry* assetRegistry);

  void displayGui();

  void displayMenuWidget();

private:
  const AssetRegistry* m_assetRegistry;

  AssetType m_filter = AssetType::Unknown;

  char m_search[128] = {};

  [[nodiscard]] static const char* assetTypeLabel(AssetType type);
};



#endif //ASSETBROWSERPANEL_H
