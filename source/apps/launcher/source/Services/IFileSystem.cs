namespace ECS3DLauncher.Services;

// Read, write and create members throw IOException or UnauthorizedAccessException on failure, as the BCL does.
public interface IFileSystem
{
    bool FileExists(string path);

    bool DirectoryExists(string path);

    string ReadAllText(string path);

    // Writes UTF-8 to "<path>.tmp" beside the target, then replaces the target; the temp file never survives.
    void WriteAllTextAtomic(string path, string contents);

    void CreateDirectory(string path);
}
