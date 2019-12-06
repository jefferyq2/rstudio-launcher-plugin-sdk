/*
 * StderrDestination.cpp
 * 
 * Copyright (C) 2019 by RStudio, Inc.
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

#include "StderrLogDestination.hpp"

#include <iostream>
#include <unistd.h>

namespace rstudio {
namespace launcher_plugins {
namespace logging {

bool StderrDestination::isStderrTty()
{
   return ::isatty(STDERR_FILENO) == 1;
}

unsigned int StderrDestination::getStderrId()
{
   return 0;
}

unsigned int StderrDestination::getId() const
{
   return getStderrId();
}

void StderrDestination::writeLog(LogLevel, const std::string& in_message)
{
   if (isStderrTty())
      std::cerr << in_message;
}

} // namespace logger
} // namespace launcher_plugins
} // namespace rstudio
