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
