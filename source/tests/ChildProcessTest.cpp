#include <gtest/gtest.h>

#if !defined(_WIN32)

#include <ChildProcess.h>

#include <chrono>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
  using Clock = std::chrono::steady_clock;

  // The child runs only async-signal-safe calls and leaves through _exit, so it never reaches gtest code.
  [[nodiscard]] pid_t forkChildThatIgnoresSigtermOrNot(const bool ignoreSigterm)
  {
    int readyPipe[2];
    if (pipe(readyPipe) != 0)
    {
      return -1;
    }

    const pid_t pid = fork();
    if (pid == 0)
    {
      close(readyPipe[0]);
      // Set both ways: the child inherits whatever disposition an earlier test left in this process.
      signal(SIGTERM, ignoreSigterm ? SIG_IGN : SIG_DFL);

      const char ready = 1;
      if (write(readyPipe[1], &ready, 1) != 1)
      {
        _exit(2);
      }

      for (;;)
      {
        pause();
      }
    }

    close(readyPipe[1]);
    char ready = 0;
    const ssize_t count = read(readyPipe[0], &ready, 1);
    close(readyPipe[0]);

    return count == 1 ? pid : -1;
  }
}

TEST(ChildProcessTest, ReapsAChildThatExitsOnSigtermBeforeTheGraceExpires)
{
  const pid_t pid = forkChildThatIgnoresSigtermOrNot(false);
  ASSERT_GT(pid, 0);

  const auto start = Clock::now();
  const int status = net::terminateChildProcess(pid, std::chrono::seconds(5));
  const auto elapsed = Clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::seconds(2));
  ASSERT_TRUE(WIFSIGNALED(status));
  EXPECT_EQ(WTERMSIG(status), SIGTERM);
}

TEST(ChildProcessTest, KillsAChildThatIgnoresSigtermOnceTheGraceExpires)
{
  const pid_t pid = forkChildThatIgnoresSigtermOrNot(true);
  ASSERT_GT(pid, 0);

  const auto grace = std::chrono::milliseconds(200);
  const auto start = Clock::now();
  const int status = net::terminateChildProcess(pid, grace);
  const auto elapsed = Clock::now() - start;

  EXPECT_GE(elapsed, grace);
  EXPECT_LT(elapsed, std::chrono::seconds(3));
  ASSERT_TRUE(WIFSIGNALED(status));
  EXPECT_EQ(WTERMSIG(status), SIGKILL);
}

#endif
