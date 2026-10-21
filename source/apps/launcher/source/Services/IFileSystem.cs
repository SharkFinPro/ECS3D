namespace ECS3DLauncher.Services;

public interface IFileSystem
{
    bool FileExists(string path);

    bool DirectoryExists(string path);
}
