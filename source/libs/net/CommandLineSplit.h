#ifndef COMMANDLINESPLIT_H
#define COMMANDLINESPLIT_H

#include <string>
#include <vector>

namespace net {

// Splits one command line into argv tokens. Space and tab separate tokens; double quotes toggle a
// grouping state and are dropped, so a token can hold spaces (and a "" pair yields an empty token). There
// is no escape for a quote.
[[nodiscard]] std::vector<std::string> splitArguments(const std::string& arguments);

}

#endif //COMMANDLINESPLIT_H
