#ifndef TRANSPORTLOG_H
#define TRANSPORTLOG_H

// C# transport threads call this through a function pointer registered as Transport.setLogCallback.
namespace net {

void transportLog(int level, const char* message);

}

#endif  // TRANSPORTLOG_H
