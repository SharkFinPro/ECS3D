#include <gtest/gtest.h>

#include "CommandLineSplit.h"

#include <string>
#include <vector>

namespace {
  using Tokens = std::vector<std::string>;
}

TEST(CommandLineSplit, SplitsPlainTokensOnSpaces)
{
  EXPECT_EQ(net::splitArguments("--edit --port 7777"), (Tokens{ "--edit", "--port", "7777" }));
}

TEST(CommandLineSplit, SplitsOnTabsAsWellAsSpaces)
{
  EXPECT_EQ(net::splitArguments("a\tb c"), (Tokens{ "a", "b", "c" }));
}

TEST(CommandLineSplit, RunsOfSeparatorsAndEdgesProduceNoEmptyTokens)
{
  EXPECT_EQ(net::splitArguments("  a \t  b   "), (Tokens{ "a", "b" }));
}

TEST(CommandLineSplit, EmptyOrBlankInputYieldsNoTokens)
{
  EXPECT_TRUE(net::splitArguments("").empty());
  EXPECT_TRUE(net::splitArguments(" \t ").empty());
  EXPECT_EQ(net::splitArguments("x").size(), 1u);
}

TEST(CommandLineSplit, DoubleQuotesGroupATokenWithSpaces)
{
  EXPECT_EQ(net::splitArguments("--log-file \"/Users/A B/logs/x.log\" --edit"),
            (Tokens{ "--log-file", "/Users/A B/logs/x.log", "--edit" }));
}

TEST(CommandLineSplit, QuotedTabsStayInsideTheToken)
{
  EXPECT_EQ(net::splitArguments("\"a\tb\" c"), (Tokens{ "a\tb", "c" }));
}

TEST(CommandLineSplit, AnEmptyQuotedPairIsAnEmptyToken)
{
  EXPECT_EQ(net::splitArguments("--token \"\" --edit"), (Tokens{ "--token", "", "--edit" }));
  EXPECT_EQ(net::splitArguments("\"\""), (Tokens{ "" }));
}

TEST(CommandLineSplit, AQuoteInTheMiddleOfATokenJoinsTheParts)
{
  EXPECT_EQ(net::splitArguments("ab\"c d\"ef gh"), (Tokens{ "abc def", "gh" }));
}

TEST(CommandLineSplit, AdjacentQuotedPartsMergeIntoOneToken)
{
  EXPECT_EQ(net::splitArguments("\"a b\"\"c d\""), (Tokens{ "a bc d" }));
}

// Pinned current behavior: an unterminated quote groups everything after it into the last token.
TEST(CommandLineSplit, AnUnterminatedQuoteSwallowsTheRestOfTheLine)
{
  EXPECT_EQ(net::splitArguments("a \"b c d"), (Tokens{ "a", "b c d" }));
  EXPECT_EQ(net::splitArguments("a \"b\" c"), (Tokens{ "a", "b", "c" }));
}

// Pinned current behavior: there is no escape, so a backslash is an ordinary character and does not
// protect a quote.
TEST(CommandLineSplit, BackslashesAreOrdinaryCharacters)
{
  EXPECT_EQ(net::splitArguments("C:\\a\\b \\\"x y\\\""), (Tokens{ "C:\\a\\b", "\\x y\\" }));
}
