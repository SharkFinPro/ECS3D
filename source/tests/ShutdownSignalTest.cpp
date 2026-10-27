#include <gtest/gtest.h>

#include <ShutdownSignal.h>

#include <csignal>

namespace {
  class ShutdownSignalTest : public testing::Test
  {
  protected:
    void SetUp() override
    {
      clearShutdownRequest();
    }

    void TearDown() override
    {
      clearShutdownRequest();
    }

    static void expectRaisedSignalRequestsShutdown(const int signalNumber)
    {
      installShutdownSignalHandlers();
      ASSERT_FALSE(shutdownRequested());

      std::raise(signalNumber);

      EXPECT_TRUE(shutdownRequested());
      EXPECT_EQ(std::signal(signalNumber, SIG_DFL), SIG_DFL);
    }
  };
}

TEST_F(ShutdownSignalTest, NoShutdownIsRequestedAfterClearing)
{
  EXPECT_FALSE(shutdownRequested());
}

TEST_F(ShutdownSignalTest, InterruptRequestsShutdownAndRestoresTheDefault)
{
  expectRaisedSignalRequestsShutdown(SIGINT);
}

TEST_F(ShutdownSignalTest, TerminateRequestsShutdownAndRestoresTheDefault)
{
  expectRaisedSignalRequestsShutdown(SIGTERM);
}

#if defined(SIGBREAK)
TEST_F(ShutdownSignalTest, BreakRequestsShutdownAndRestoresTheDefault)
{
  expectRaisedSignalRequestsShutdown(SIGBREAK);
}
#endif
