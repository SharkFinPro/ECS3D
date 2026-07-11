#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace net {

// The default port the server listens on and the client/editor connect to (singleplayer, local-MP, and
// remote all share the same wire). Apps use this unless overridden, so they find each other out of the
// box instead of defaulting to port 0.
inline constexpr int defaultPort = 3000;

enum class MessageType : uint8_t {
  undefined,
  join,         // client -> server: request the initial Snapshot (carries role + auth at handshake)
  snapshot,     // server -> client: full project/scene state (ProjectPacker::pack())
  stateDelta,   // server -> client: per-tick transform stream, packed binary (replication::packStateDelta)
  inputState,    // client -> server: local input for the scripts to read. Payload: focused (bool),
                 // key count (size_t) + that many key codes (int), then the mouse block: mouseX, mouseY,
                 // mouseDeltaX, mouseDeltaY, scrollY (5x float), buttons (uint8 bitmask L/R/M)
  editComponent, // editor -> server -> all: a single component value edit (replication::buildComponentEdit)
  sceneEdit,     // editor -> server: a structural edit (add/remove object/component); server re-snapshots
  sceneControl,  // editor -> server: scene lifecycle (SceneControlOp + optional scene uuid); server re-snapshots
  loadProject,   // editor -> server: replace the project with this serialized blob; server re-snapshots
  addAsset,      // editor -> server: register a new asset (model/texture/script/scene); server re-snapshots
  editStatus,    // server -> client: whether this server accepts edits ({ editable: bool }); sent on join
  sceneStatus,   // server -> client: current scene lifecycle state ({ status: "running"|"paused"|"stopped" })
  objectSpawned, // server -> client: one object created at runtime (Object::pack); spliced into the scene
  objectDestroyed, // server -> client: uuid of an object removed at runtime; the client drops it from the scene
  playerSlot,    // server -> all: (nonce uint64, slot int32) — the player slot bound to the client whose
                 // join carried this nonce. Broadcast + nonce correlation (no per-connection send path):
                 // every client hears it, only the one whose join nonce matches keeps it.
  renameAsset,   // editor -> server: set an asset's display-name override (replication::packRenameAsset); server re-snapshots
  removeAsset    // editor -> server: drop an asset record by uuid (replication::packRemoveAsset); server re-snapshots
  // editComponent/sceneEdit/sceneControl/loadProject/addAsset/renameAsset/removeAsset are the editor's mutation path; the server
  // only honors them from a connection it authorized as Role::editor at the transport handshake (which
  // carries role + token out of band, ahead of any message here), and only on an edit-mode server. An
  // editor connecting to a non-edit server is admitted read-only (it views but cannot mutate).
};

enum class Role : uint8_t {
  player,
  editor
};

// The scene lifecycle ops carried by a sceneControl message. Packed as a leading uint8; loadScene is
// followed by a length-prefixed scene uuid string (the others carry no payload).
enum class SceneControlOp : uint8_t {
  start,
  pause,
  stop,
  loadScene
};

template <typename T>
concept Trivial = std::is_trivially_copyable_v<T>;

class Message {
public:
  explicit Message(const MessageType type) noexcept : type(type) {}
  Message() {}

  template <Trivial T>
  Message& write(const T& value) {
    const auto raw = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);
    m_payload.insert(m_payload.end(), raw.begin(), raw.end());
    return *this;
  }

  // Length-prefixed string (uint32 size + bytes). The pairing read is MessageReader::readString.
  Message& writeString(const std::string& value) {
    write(static_cast<uint32_t>(value.size()));
    m_payload.insert(m_payload.end(), value.begin(), value.end());
    return *this;
  }

  [[nodiscard]] std::size_t size() const noexcept { return m_payload.size(); }
  [[nodiscard]] std::span<const uint8_t> bytes() const noexcept { return m_payload; }

  [[nodiscard]] MessageType getType() const noexcept { return type; }

private:
  MessageType type = MessageType::undefined;
  std::vector<uint8_t> m_payload;
};

class MessageReader {
public:
  explicit MessageReader(const Message& message) noexcept : m_data(message.bytes()) {}

  template <Trivial T>
  [[nodiscard]] T read() {
    if (sizeof(T) > m_data.size() - m_offset)  // offset_ <= size() invariant; no overflow
      throw std::runtime_error("Message underflow");

    std::array<uint8_t, sizeof(T)> raw{};
    std::memcpy(raw.data(), m_data.data() + m_offset, sizeof(T));
    m_offset += sizeof(T);
    return std::bit_cast<T>(raw);
  }

  // Reads a length-prefixed string written by Message::writeString.
  [[nodiscard]] std::string readString() {
    const auto size = read<uint32_t>();
    if (size > m_data.size() - m_offset)
      throw std::runtime_error("Message underflow");

    std::string value(reinterpret_cast<const char*>(m_data.data() + m_offset), size);
    m_offset += size;
    return value;
  }

  [[nodiscard]] std::size_t remaining() const noexcept { return m_data.size() - m_offset; }

private:
  std::span<const uint8_t> m_data;
  std::size_t m_offset = 0;
};

}



#endif //PROTOCOL_H
