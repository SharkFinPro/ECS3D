using System;
using System.IO;
using System.Text;

namespace ECS3DLauncher.Services;

public sealed class PhysicalFileSystem : IFileSystem
{
    private static readonly UTF8Encoding Utf8NoBom = new(false);

    public bool FileExists(string path) => File.Exists(path);

    public bool DirectoryExists(string path) => Directory.Exists(path);

    public string ReadAllText(string path) => File.ReadAllText(path, Utf8NoBom);

    public void WriteAllTextAtomic(string path, string contents)
    {
        var temp = path + ".tmp";
        try
        {
            File.WriteAllText(temp, contents, Utf8NoBom);
            File.Move(temp, path, overwrite: true);
        }
        catch (Exception)
        {
            DeleteQuietly(temp);
            throw;
        }
    }

    public void CreateDirectory(string path) => Directory.CreateDirectory(path);

    private static void DeleteQuietly(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // The original failure is the one worth reporting; a stray temp file is replaced next write.
            return;
        }
    }
}
