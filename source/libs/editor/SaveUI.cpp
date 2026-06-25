#include "SaveUI.h"
#include <ProjectSerializer.h>

SaveUI::SaveUI(ProjectSerializer* projectSerializer)
  : m_projectSerializer(projectSerializer)
{
  // TODO: registerWindowEvents() — hook Ctrl+S / Ctrl+Shift+S and the drop event off the renderer
  // TODO:   window, the same as SaveManager's constructor did.
}

void SaveUI::save()
{
  if (m_saveFile.empty() && !createSaveFile())
  {
    return;
  }

  m_projectSerializer->save(m_saveFile);
}

void SaveUI::saveAs()
{
  if (createSaveFile())
  {
    m_projectSerializer->save(m_saveFile);
  }
}

void SaveUI::open()
{
  if (!chooseSaveFile())
  {
    return;
  }

  m_projectSerializer->load(m_saveFile);
}

void SaveUI::loadFromFile(const std::string& path)
{
  m_saveFile = path;

  m_projectSerializer->load(m_saveFile);
}

void SaveUI::createNewProject()
{
  // TODO: createSaveFile() then start an empty project. The old createNewProject used the ECS3D
  // TODO:   reset machinery; with ProjectSerializer this resets the AssetRegistry/SceneManager instead.
}

bool SaveUI::chooseSaveFile()
{
  // TODO: nfd open dialog (SaveManager::chooseSaveFile) -> set m_saveFile, return success.
  return false;
}

bool SaveUI::createSaveFile()
{
  // TODO: nfd save dialog (SaveManager::createSaveFile) -> set m_saveFile, return success.
  return false;
}
