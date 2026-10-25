#ifndef OBJECTNAMEFIELD_H
#define OBJECTNAMEFIELD_H

#include <optional>
#include <string>
#include <utility>
#include <uuid.h>
#include <vector>

// The decisions behind the Inspector's object Name field, with no ImGui: a typed name becomes one rename,
// sent when the field is left, not one per keystroke. The inspector hands frame() a callable that draws the
// text field and reports what ImGui saw; frame() says which renames to send and keeps the text in step.
class ObjectNameField {
public:
  // How many idle frames a just-sent name is kept on screen while waiting for the server to apply it.
  static constexpr int echoHoldFrames = 120;

  struct Rename {
    uuids::uuid objectUUID;
    std::string name;

    [[nodiscard]] bool operator==(const Rename&) const = default;
  };

  // What ImGui reported for the field this frame: InputText's return value, IsItemActive and
  // IsItemDeactivatedAfterEdit (which fires for Enter, Tab, a click away and Escape alike).
  struct Report {
    bool edited = false;
    bool active = false;
    bool deactivatedAfterEdit = false;
  };

  [[nodiscard]] bool isShowing(const uuids::uuid& objectUUID) const { return m_objectUUID == objectUUID; }

  // One frame of the field for the object being shown. `draw` is called with the text buffer and returns
  // the Report. `canSend` is false when a rename would be dropped (read-only, or no sink): the typed text is
  // then abandoned and the field shows the object's real name again. Returns the renames to send: one for a
  // name typed for the previously shown object, and one for this object.
  template <typename DrawFn>
  [[nodiscard]] std::vector<Rename> frame(const uuids::uuid& objectUUID, const std::string& currentName,
                                          std::string& buffer, const bool canSend, DrawFn&& draw)
  {
    std::vector<Rename> renames;

    if (!isShowing(objectUUID))
    {
      if (auto rename = leave(buffer, canSend))
      {
        renames.push_back(std::move(*rename));
      }

      m_objectUUID = objectUUID;
      m_dirty = false;
      m_seedName = currentName;
      m_echo.reset();
      buffer = currentName;
    }

    const Report report = draw(buffer);

    if (auto rename = settle(currentName, buffer, report, canSend))
    {
      renames.push_back(std::move(*rename));
    }

    return renames;
  }

  // Resolves a name typed for the object last shown, for the frames that stop drawing the field without
  // ImGui reporting its deactivation: nothing or an asset selected, the object gone, another view shown.
  // Never call it on a frame that is about to draw the same object: the field may be mid-typing.
  [[nodiscard]] std::optional<Rename> leave(const std::string& buffer, const bool canSend)
  {
    if (!m_objectUUID || !m_dirty)
    {
      return std::nullopt;
    }

    return finish(buffer, m_seedName, canSend);
  }

private:
  struct Echo {
    std::string before;
    std::string sent;
    int idleFrames = 0;
  };

  std::optional<uuids::uuid> m_objectUUID;
  std::string m_seedName;
  bool m_dirty = false;
  std::optional<Echo> m_echo;

  [[nodiscard]] std::optional<Rename> settle(const std::string& currentName, std::string& buffer,
                                             const Report& report, const bool canSend)
  {
    if (report.edited)
    {
      m_dirty = true;
      m_echo.reset();
    }

    if (report.deactivatedAfterEdit || (!report.active && m_dirty))
    {
      return finish(buffer, currentName, canSend);
    }

    if (report.active)
    {
      if (!m_dirty)
      {
        m_seedName = currentName;
      }

      return std::nullopt;
    }

    m_seedName = currentName;

    // Right after a send the object still has its old name until the server answers; keep showing what
    // was typed rather than flashing the old name back, but only for a bounded time.
    if (m_echo)
    {
      if (currentName == m_echo->sent || currentName != m_echo->before || ++m_echo->idleFrames >= echoHoldFrames)
      {
        m_echo.reset();
      }
      else
      {
        return std::nullopt;
      }
    }

    buffer = currentName;

    return std::nullopt;
  }

  [[nodiscard]] std::optional<Rename> finish(const std::string& buffer, const std::string& referenceName,
                                             const bool canSend)
  {
    m_dirty = false;

    if (!canSend || buffer == referenceName)
    {
      return std::nullopt;
    }

    m_echo = Echo{ referenceName, buffer, 0 };
    m_seedName = referenceName;

    return Rename{ *m_objectUUID, buffer };
  }
};

#endif //OBJECTNAMEFIELD_H
