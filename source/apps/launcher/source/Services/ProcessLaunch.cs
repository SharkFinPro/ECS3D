using System.Collections.Generic;

namespace ECS3DLauncher.Services;

public sealed record ProcessLaunch(
    string FileName,
    IReadOnlyList<string> Arguments,
    string WorkingDirectory,
    bool HideWindow);
