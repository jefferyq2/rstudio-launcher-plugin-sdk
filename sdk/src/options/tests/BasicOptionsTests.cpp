/*
 * BasicOptionsTests.cpp
 * 
 * Copyright (C) 2019-20 by RStudio, PBC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <TestMain.hpp>

#include <options/Options.hpp>
#include <system/FilePath.hpp>
#include <system/User.hpp>

namespace rstudio {
namespace launcher_plugins {
namespace options {

TEST_CASE("basic options")
{
   system::User user3;
   REQUIRE_FALSE(system::User::getUserFromIdentifier(USER_THREE, user3));

   SECTION("read options")
   {
      constexpr const char* argv[] = {};
      constexpr int argc = 0;

      Options& opts = Options::getInstance();
      Error error = opts.readOptions(argc, argv, system::FilePath("conf-files/Basic.conf"));
      REQUIRE_FALSE(error);
   }

   SECTION("check values")
   {
      Options& opts = Options::getInstance();

      CHECK(opts.getJobExpiryHours() == system::TimeDuration::Hours(11));
      CHECK(opts.getHeartbeatIntervalSeconds() == system::TimeDuration::Seconds(4));
      CHECK(opts.getLogLevel() == logging::LogLevel::ERR);
      CHECK(opts.getRSandboxPath().getAbsolutePath() == "/usr/local/bin/rsandbox");
      CHECK(opts.getScratchPath().getAbsolutePath() == "/home/rlpstestusrthree/temp/");

      CHECK(opts.getThreadPoolSize() == 6);

      system::User serverUser;
      Error error = opts.getServerUser(serverUser);
      REQUIRE_FALSE(error);
      CHECK(serverUser == user3);
   }
}

} // namespace options
} // namespace launcher_plugins
} // namespace rstudio

