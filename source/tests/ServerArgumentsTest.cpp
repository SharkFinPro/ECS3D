#include <gtest/gtest.h>

#include "CommandLineSplit.h"
#include "Protocol.h"
#include "ServerArguments.h"

#include <string>
#include <vector>

namespace {
  using Tokens = std::vector<std::string>;
}

TEST(ServerArguments, PortArgumentSplitsIntoFlagAndValue)
{
  EXPECT_EQ(net::splitArguments("--edit" + net::serverPortArgument(3300)), (Tokens{ "--edit", "--port", "3300" }));
}

TEST(ServerArguments, DefaultPortIsForwardedLikeAnyOther)
{
  EXPECT_EQ(net::splitArguments(net::serverPortArgument(net::defaultPort)), (Tokens{ "--port", "3000" }));
}

TEST(ServerArguments, PortArgumentKeepsTheTokenLastArgumentLast)
{
  const std::string arguments = "--edit --ephemeral" + net::serverPortArgument(3100) + " --token abc";

  EXPECT_EQ(net::splitArguments(arguments), (Tokens{ "--edit", "--ephemeral", "--port", "3100", "--token", "abc" }));
}

TEST(ServerArguments, ParsePortArgumentAcceptsWholeDecimalPortsInRange)
{
  EXPECT_EQ(net::parsePortArgument("0"), 0);
  EXPECT_EQ(net::parsePortArgument("3000"), 3000);
  EXPECT_EQ(net::parsePortArgument("65535"), 65535);
}

TEST(ServerArguments, ParsePortArgumentRejectsEverythingElse)
{
  for (const char* text : { "", "65536", "-5", "+5", "41000abc", "abc", " 3000", "3000 ", "99999",
                            "99999999999999999999" })
  {
    EXPECT_FALSE(net::parsePortArgument(text).has_value()) << "'" << text << "'";
  }
}
