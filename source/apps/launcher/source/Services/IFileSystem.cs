using System.Collections.Generic;

namespace ECS3DLauncher.Services;

// Read, write and create members throw IOException or UnauthorizedAccessException on failure, as the BCL does.
public interface IFileSystem
{
    bool FileExists(string path);

    bool DirectoryExists(string path);

    // Full paths of the immediate subdirectories; empty when the folder does not exist.
    IReadOnlyList<string> EnumerateDirectories(string path);

    // Full paths of the immediate files, ordinal-sorted; empty when the folder does not exist.
    IReadOnlyList<string> EnumerateFiles(string path);

    string ReadAllText(string path);

    // Writes UTF-8 to a uniquely named temp file beside the target, then replaces the target; the temp file never survives.
    void WriteAllTextAtomic(string path, string contents);

    void CreateDirectory(string path);

    // Never overwrites an existing destination; the copy is never read-only, whatever the source was.
    void CopyFile(string source, string destination);

    void MoveDirectory(string source, string destination);

    void DeleteDirectory(string path, bool recursive);
}
