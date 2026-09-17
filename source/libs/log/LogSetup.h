#ifndef LOGSETUP_H
#define LOGSETUP_H

#include "FileSink.h"
#include "LogEntry.h"
#include <memory>
#include <string>
#include <string_view>

// Every app registers the same file sink the same way, and the GUI-subsystem builds have no console to
// fall back on, so the argument handling lives here rather than three times over in main().
//
// Honors --log-file <path> (write there instead of the default) and --no-log-file (register nothing and
// return null). Never throws: a file that cannot be opened is a warning through Log and a null return,
// not a failure to start. The returned sink is the one registered, for a caller that wants to remove it
// again; the apps ignore it.
std::shared_ptr<FileSink> addFileSinkFromArguments(int argc, char** argv, std::string_view appName,
                                                   LogCategory category);

// A --log-file flag for a child process's command line, naming an absolute path so it resolves against
// this process's working directory rather than the child's. The path is double quoted, since the user
// data directory contains a space on macOS and the command line is one string the launcher splits.
// There is no escape for a quote inside the path, so a path containing one yields an empty string: no
// flag, and the child falls back to its own default rather than being handed a truncated path.
[[nodiscard]] std::string logFileArgument(std::string_view appName);

#endif  // LOGSETUP_H
