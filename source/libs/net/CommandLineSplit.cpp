#include "CommandLineSplit.h"

namespace net {

std::vector<std::string> splitArguments(const std::string& arguments)
{
  std::vector<std::string> tokens;

  std::string token;
  bool quoted = false;
  bool started = false;

  for (const char character : arguments)
  {
    if (character == '"')
    {
      quoted = !quoted;
      started = true;
    }
    else if (!quoted && (character == ' ' || character == '\t'))
    {
      if (started)
      {
        tokens.push_back(token);
        token.clear();
        started = false;
      }
    }
    else
    {
      token.push_back(character);
      started = true;
    }
  }

  if (started)
  {
    tokens.push_back(token);
  }

  return tokens;
}

}
