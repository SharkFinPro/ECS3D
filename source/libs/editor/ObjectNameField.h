#ifndef OBJECTNAMEFIELD_H
#define OBJECTNAMEFIELD_H

#include <optional>
#include <string>
#include <uuid.h>

// The decisions behind the Inspector's object Name field, with no ImGui: a typed name becomes one rename,
// sent when the field is left, not one per keystroke. The field draws the text buffer and reports what
// ImGui saw each frame; this class says when to send and when to refill the buffer.
class ObjectNameField {
public:
  struct Rename {
    uuids::uuid objectUUID;
    std::string name;

    [[nodiscard]] bool operator==(const Rename&) const = default;
  };

  struct Result {
    std::optional<Rename> rename;
    std::optional<std::string> reseed; // replace the buffer with this text
  };

  [[nodiscard]] bool isShowing(const uuids::uuid& objectUUID) const { return m_objectUUID == objectUUID; }

  // Starts tracking another object and refills the buffer. A name typed for the previous object is lost
  // unless flush() ran first.
  [[nodiscard]] Result select(const uuids::uuid& objectUUID, const std::string& currentName)
  {
    m_objectUUID = objectUUID;
    m_dirty = false;
    m_seedName = currentName;
    m_echo.reset();

    return { std::nullopt, currentName };
  }

  // Call after drawing the field. `edited` is the InputText return value; `deactivatedAfterEdit` is
  // IsItemDeactivatedAfterEdit, which fires for Enter, Tab, a click away and Escape alike.
  [[nodiscard]] Result update(const std::string& currentName, const std::string& buffer, const bool active,
                              const bool deactivatedAfterEdit, const bool edited)
  {
    if (!m_objectUUID)
    {
      return {};
    }

    if (edited)
    {
      m_dirty = true;
      m_echo.reset();
    }

    if (deactivatedAfterEdit || (!active && m_dirty))
    {
      return { finish(buffer, currentName), std::nullopt };
    }

    if (active)
    {
      if (!m_dirty)
      {
        m_seedName = currentName;
      }

      return {};
    }

    m_seedName = currentName;

    // Right after a send the object still has its old name until the server answers; keep showing what
    // was typed rather than flashing the old name back.
    if (m_echo)
    {
      if (currentName == m_echo->sent || currentName != m_echo->before)
      {
        m_echo.reset();
      }
      else
      {
        return {};
      }
    }

    if (buffer != currentName)
    {
      return { std::nullopt, currentName };
    }

    return {};
  }

  // Resolves a name typed for the object last shown, for the frames that leave the field without ImGui
  // reporting its deactivation: another object selected, nothing or an asset selected, the object gone.
  [[nodiscard]] std::optional<Rename> flush(const std::string& buffer)
  {
    if (!m_objectUUID || !m_dirty)
    {
      return std::nullopt;
    }

    return finish(buffer, m_seedName);
  }

  [[nodiscard]] bool hasPendingName() const { return m_dirty; }

private:
  struct Echo {
    std::string before;
    std::string sent;
  };

  std::optional<uuids::uuid> m_objectUUID;
  std::string m_seedName;
  bool m_dirty = false;
  std::optional<Echo> m_echo;

  [[nodiscard]] std::optional<Rename> finish(const std::string& buffer, const std::string& referenceName)
  {
    m_dirty = false;

    if (buffer == referenceName)
    {
      return std::nullopt;
    }

    m_echo = Echo{ referenceName, buffer };
    m_seedName = referenceName;

    return Rename{ *m_objectUUID, buffer };
  }
};

#endif //OBJECTNAMEFIELD_H
