#ifndef PROJECTFINGERPRINT_H
#define PROJECTFINGERPRINT_H

#include <cstdint>
#include <filesystem>
#include <string_view>

// Names the project file a process loaded by its content, so an editor and a server on different machines
// can tell whether they hold the same project. Zero means no file: the built-in project, one an editor
// sent, or an unreadable file. A real file never hashes to zero.
[[nodiscard]] uint64_t fingerprintBytes(std::string_view bytes);

[[nodiscard]] uint64_t fingerprintProjectFile(const std::filesystem::path& projectFile);

// The project file plus each terrain's index file (under <root>/terrain/, which records every region and chunk hash), so
// two projects with the same scenes but different terrain differ. A project with no terrain index gets exactly
// fingerprintProjectFile's value. Zero when the project file is empty or unreadable, or when the terrain folder or an
// index cannot be read: that project cannot be named, so the identity check skips it.
[[nodiscard]] uint64_t fingerprintProject(const std::filesystem::path& projectFile, const std::filesystem::path& root);

#endif //PROJECTFINGERPRINT_H
