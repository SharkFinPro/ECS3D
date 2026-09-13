#ifndef LOGBINDINGS_H
#define LOGBINDINGS_H

struct LogBindings
{
  void(*write)(int level, const char* message);
};

class LogBindingsProvider {
public:
  [[nodiscard]] static LogBindings getBindings();

private:
  // level is ScriptBridge.Log's own 0..4 scale (trace..error); an out-of-range value logs as info.
  // Category is always LogCategory::script - the only category a gameplay script can write under.
  static void bindWrite(int level, const char* message);
};

#endif //LOGBINDINGS_H
