/*
 * Process.cpp
 *
 * Copyright (C) 2020 by RStudio, PBC
 *
 * Unless you have received this program directly from RStudio pursuant to the terms of a commercial license agreement
 * with RStudio, then this program is licensed to you under the following terms:
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

#include <system/Process.hpp>

#include <cstring>
#include <dirent.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <boost/regex.hpp>

#include <json/Json.hpp>
#include <options/Options.hpp>
#include <boost/algorithm/string/join.hpp>

#include "PosixSystem.hpp"
#include "../utils/ErrorUtils.hpp"

namespace rstudio {
namespace launcher_plugins {
namespace system {
namespace process {

namespace {

// Exit code for when a thread-safe spawn fails. Chosen to be something "unique" enough to identify since thread-safe
// forks cannot log until ::execve is invoked (because mutexes cannot be acquired before that point).
const int s_threadSafeExitError = 153;

// Pipe Constants
const int s_readPipe = 0;
const int s_writePipe = 1;

struct FileDescriptors
{
   int Input[2] = {0, 0};
   int Output[2] = {0, 0};
   int Error[2] = {0, 0};
   int CloseFd[2] = {0, 0};
};

class CStringList final
{
public:
   CStringList() :
      m_length(0),
      m_data(nullptr)
   {
   }

   explicit CStringList(const std::vector<std::string>& in_vector) :
      CStringList()
   {
      setData(in_vector);
   }

   ~CStringList() noexcept
   {
      try
      {
         free();
      }
      catch(...)
      {
         // Swallow exceptions for an exception-safe destructor.
      }
   }

   char** getData() const
   {
      return !isEmpty() ? m_data : nullptr;
   }

   size_t getSize() const
   {
      return m_length;
   }

   bool isEmpty() const
   {
      return m_length == 0;
   }

private:
   void setData(const std::vector<std::string>& in_vector)
   {
      free();

      m_length = in_vector.size();
      m_data = new char*[m_length + 1];

      for (size_t i = 0; i < m_length; ++i)
      {
         const std::string& str = in_vector[i];
         m_data[i] = new char[str.size() + 1];
         str.copy(m_data[i], str.size());

         // Null terminate all strings.
         m_data[i][str.size()] = '\0';
      }

      // Null terminate the list of strings.
      m_data[m_length] = nullptr;
   }

   void free()
   {
      if (m_data != nullptr)
      {
         for (size_t i = 0; i < m_length; ++i)
            delete[] m_data[i];
      }

      delete[] m_data;
   }

   size_t m_length;
   char** m_data;
};

int clearSignalMask()
{
   sigset_t emptyMask;
   sigemptyset(&emptyMask);
   return ::pthread_sigmask(SIG_SETMASK, &emptyMask, nullptr);
}

void closeFd(uint32_t in_fd)
{
   // Keep trying to close the file descriptor if the operation was interrupted. Otherwise, an error means the
   // FD isn't open so just ignore it and exit.
   while(::close(in_fd) == -1)
   {
      if (errno != EINTR)
         break;
   }
}

void closePipe(int in_pipeFd, const ErrorLocation& in_errorLocation)
{
   Error error = posix::posixCall<int>(std::bind(::close, in_pipeFd), in_errorLocation);
   if (error)
      logging::logError(error);
}

void closePipe(int* in_pipeFds, const ErrorLocation& in_errorLocation)
{
   closePipe(in_pipeFds[s_readPipe], in_errorLocation);
   closePipe(in_pipeFds[s_writePipe], in_errorLocation);
}

void closeParentFds(int in_pipeFd, rlim_t in_maxFd)
{
   // The parent process will send its open FDs on in_pipeFd. Read them and close them (except the pipe).
   static const uint32_t startFd = STDERR_FILENO + 1;
   bool error = false;
   bool fdsRead = false;

   int32_t buffer = -2;
   while (true)
   {
      ssize_t bytesRead = ::read(in_pipeFd, &buffer, 4);

      if (bytesRead <= 0)
      {
         if ((errno != EINTR) && (errno != EAGAIN))
         {
            error = true;
            break;
         }

         continue;
      }

      // The parent sends -1 after all other FDs have been sent.
      if (buffer == -1)
         break;

      fdsRead = true;
      uint32_t fd = static_cast<uint32_t>(buffer);

      if ((fd >= startFd) && (fd < in_maxFd) && (buffer != in_pipeFd))
         closeFd(fd);
   }

   // If we didn't manage to read the file descriptors, loop through all the FDs (besides the current pipe, STDIN,
   // STDOUT, and STDERR) and close them all. This is much slower,
   if (error || !fdsRead)
   {
      // Get the max fd, or a default of 1024 (the default maximum on linux) if the maximum is "infinity".
      uint32_t maxFd = (in_maxFd == RLIM_INFINITY) ? 1024 : in_maxFd;
      for (uint32_t fd = startFd; fd < maxFd; ++fd)
         if (fd != in_pipeFd)
            closeFd(fd);
   }
}

Error createPipes(FileDescriptors& out_fds)
{
   Error error = posix::posixCall<int>(std::bind(::pipe, out_fds.Input), ERROR_LOCATION);
   if (error)
      return error;

   error = posix::posixCall<int>(std::bind(::pipe, out_fds.Output), ERROR_LOCATION);
   if (error)
   {
      closePipe(out_fds.Input, ERROR_LOCATION);
      return error;
   }

   error = posix::posixCall<int>(std::bind(::pipe, out_fds.Error), ERROR_LOCATION);
   if (error)
   {
      closePipe(out_fds.Input, ERROR_LOCATION);
      closePipe(out_fds.Output, ERROR_LOCATION);
      return error;
   }

   error = posix::posixCall<int>(std::bind(::pipe, out_fds.CloseFd), ERROR_LOCATION);
   if (error)
   {
      closePipe(out_fds.Input, ERROR_LOCATION);
      closePipe(out_fds.Output, ERROR_LOCATION);
      closePipe(out_fds.Error, ERROR_LOCATION);
      return error;
   }

   return error;
}

std::string escape(const std::string& arg)
{
   boost::regex pattern("'");
   return "'" + boost::regex_replace(arg, pattern, R"('"'"')") + "'";
}

std::string escape(const FilePath &path)
{
   return escape(path.getAbsolutePath());
}

Error getFilesLimit(rlim_t& out_softLimit, rlim_t& out_hardLimit)
{
   struct rlimit fileLimit;
   if (::getrlimit(RLIMIT_NOFILE, &fileLimit))
      return systemError(errno, ERROR_LOCATION);

   out_softLimit = fileLimit.rlim_cur;
   out_hardLimit = fileLimit.rlim_max;
   return Success();
}

Error getOpenFds(pid_t in_pid, std::vector<uint32_t>& out_fds)
{
   std::string pidStr = std::to_string(in_pid);
   FilePath filePath("/proc/" + pidStr + "/fd");

   // note: we use a FileScanner to list the pids instead of using boost
   // (FilePath class), because there is a bug in boost filesystem where
   // directory iterators can segfault under heavy load while reading the /proc filesystem
   // there aren't many details on this, but see https://svn.boost.org/trac10/ticket/10450

   // Scan the directory non-recursively.
   struct dirent **nameList;
   int childCount = ::scandir(
      filePath.getAbsolutePath().c_str(),
      &nameList,
      [](const struct dirent* in_entry)
      {
         return ((::strcmp(in_entry->d_name, ".") == 0) || (::strcmp(in_entry->d_name, "..") == 0)) ? 0 : 1;
      },
      [](const dirent** in_lhs, const dirent** in_rhs)
      {
         return ::strcmp((*in_lhs)->d_name, (*in_rhs)->d_name);
      });

   if (childCount < 0)
   {
      Error error = systemError(errno, ERROR_LOCATION);
      error.addProperty("path", filePath.getAbsolutePath());
      return error;
   }

   // Iterate over the names, converting them to uint32_t values. If they can't be converted to an int, just skip them.
   for (int i = 0; i < childCount; ++i)
   {
      try
      {
         uint32_t fd = std::strtoul(nameList[i]->d_name, nullptr, 10);
         out_fds.push_back(fd);
      }
      catch (...)
      {
         // Swallow exceptions - they don't matter here.
      }

      // Free the value.
      ::free(nameList[i]);
   }

   // Free the whole name list.
   ::free(nameList);

   return Success();
}

Error readFromPipe(int in_fd, std::string& out_data, bool* out_eof = nullptr)
{
   if (out_eof != nullptr)
      *out_eof = false;

   static const size_t bufferSize = 512;
   char buffer[bufferSize];
   size_t bytesRead = posix::posixCall<size_t>(std::bind(::read, in_fd, buffer, bufferSize));

   while (true)
   {
      // check for error
      if (bytesRead == -1)
      {
         if (errno == EAGAIN) // carve-out for O_NONBLOCK pipes
            return Success();
         else
            return systemError(errno, ERROR_LOCATION);
      }
      // check for eof
      else if (bytesRead == 0)
      {
         if (out_eof)
            *out_eof = true;

         return Success();
      }

      // Otherwise, we read some data.
      out_data.append(buffer, bytesRead);
      bytesRead = posix::posixCall<size_t>(std::bind(::read, in_fd, buffer, bufferSize));
   }

   return Success();
}

Error sendFileDescriptors(int closeFd, pid_t in_childPid)
{
   size_t bytesWritten = 0;

   // Get list of FileDescriptors open in the child process.
   std::vector<uint32_t> openFds;
   Error error = getOpenFds(in_childPid, openFds);
   if (error)
      logging::logError(error);
   else
   {
      // Write them to the closeFd.
      for (uint32_t openFd : openFds)
      {
         error = posix::posixCall<size_t>(
            std::bind(::write, closeFd, &openFd, 4),
            ERROR_LOCATION,
            &bytesWritten);

         if (error)
            return error;
      }
   }

   int streamEnd = -1;
   Error lastWriteError = posix::posixCall<size_t>(
      std::bind(::write, closeFd, &streamEnd, 4),
      ERROR_LOCATION,
      &bytesWritten);

   if (lastWriteError)
      return error;

   return Success();
}

Error writeToPipe(int in_fd, const std::string& in_data, bool in_eof)
{
   size_t bytesWritten = 0;

   Error error = posix::posixCall<size_t>(
      std::bind(::write, in_fd, in_data.c_str(), in_data.length()),
      ERROR_LOCATION,
      &bytesWritten);
   if (error)
      return error;

   if (in_eof)
      closePipe(in_fd, ERROR_LOCATION);

   if (bytesWritten != in_data.length())
      return utils::createErrorFromBoostError(boost::system::errc::io_error, ERROR_LOCATION);

   return Success();
}

} // anonymous namespace


// AbstractChildProcess ================================================================================================
struct AbstractChildProcess::Impl
{
   explicit Impl(const ProcessOptions& in_options) :
      Pid(-1),
      StdErrFd(-1),
      StdInFd(-1),
      StdOutFd(-1)
   {
      RSandbox = options::Options::getInstance().getRSandboxPath().getAbsolutePath();
      Arguments.push_back(RSandbox);
      createEnvironmentVars(in_options);
      createLaunchProfile(in_options);
      createSandboxArguments(in_options);
   }

   void createEnvironmentVars(const ProcessOptions& in_options)
   {
      bool pathFound = false;
      for (const api::EnvVariable& envVar: in_options.Environment)
      {
         if (envVar.first == "PATH")
            pathFound = true;

         Environment.emplace_back(envVar.first + "=" + envVar.second);
      }

      if (!pathFound)
         Environment.emplace_back("PATH=" + posix::getEnvironmentVariable("PATH"));
   }

   void createLaunchProfile(const ProcessOptions& in_options)
   {
      json::Object contextObj;
      contextObj["username"] = (in_options.RunAsUser.isAllUsers() || in_options.RunAsUser.isEmpty()) ?
         "" : in_options.RunAsUser.getUsername();
      contextObj["project"] = "";
      contextObj["id"] = "";

      json::Object configObj;
      configObj["args"] = json::Array();
      configObj["environment"] = json::Object();
      configObj["stdInput"] = in_options.StandardInput;
      configObj["stdStreamBehavior"] = 2; // Inherit
      configObj["priority"] = 0;
      configObj["memoryLimitBytes"] = 0;
      configObj["stackLimitBytes"] = 0;
      configObj["userProcessesLimit"] = 0;
      configObj["cpuLimit"] = 0;
      configObj["niceLimit"] = 0;
      configObj["filesLimit"] = 0;
      configObj["cpuAffinity"] = json::Array();

      json::Object profileObj;
      profileObj["context"] = contextObj;
      profileObj["password"] = in_options.Password;
      profileObj["executablePath"] = in_options.Executable;
      profileObj["config"] = configObj;

      StandardInput = profileObj.write();
   }

   void createSandboxArguments(const ProcessOptions& in_options)
   {
      std::string shellCommand = in_options.Executable;
      for (const std::string& arg: in_options.Arguments)
         shellCommand.append(" ").append(escape(arg));

      bool redirectStdout = !in_options.StandardOutputFile.isEmpty(),
           redirectStderr = !in_options.StandardErrorFile.isEmpty(),
           redirectToSame = redirectStdout &&
              redirectStderr &&
              (in_options.StandardOutputFile == in_options.StandardErrorFile);

      if (in_options.IsShellCommand && (redirectStdout || redirectStderr))
         shellCommand = "(" + shellCommand.append(")");

      if (redirectStdout)
         shellCommand.append(" > ").append(escape(in_options.StandardOutputFile));

      if (redirectToSame)
         shellCommand.append(" 2>&1");
      else if (redirectStderr)
         shellCommand.append(" 2> ").append(escape(in_options.StandardErrorFile));

      if (!in_options.RunAsUser.isAllUsers() && !in_options.RunAsUser.isAllUsers())
      {
         Arguments.emplace_back("--username");
         Arguments.push_back(in_options.RunAsUser.getUsername());
      }

      if (!in_options.WorkingDirectory.isEmpty())
      {
         Arguments.emplace_back("--workingdir");
         Arguments.push_back(in_options.WorkingDirectory.getAbsolutePath());
      }

      if (!in_options.PamProfile.empty())
      {
         Arguments.emplace_back("--pam-profile");
         Arguments.push_back(in_options.PamProfile);
      }

      for (const api::Mount& mount: in_options.Mounts)
      {
         if (mount.HostSourcePath)
         {
            Arguments.emplace_back("--mount");
            Arguments.emplace_back(
               escape(mount.HostSourcePath.getValueOr(api::HostMountSource()).Path +
               ":" +
               mount.DestinationPath +
               (mount.IsReadOnly ? ":ro" : "")));
         }
      }

      Arguments.emplace_back("/bin/sh");
      Arguments.emplace_back("-c");
      Arguments.emplace_back(shellCommand);
   }

   Error execChild(
      const FileDescriptors& in_fds,
      rlim_t in_maxFd,
      const CStringList& in_arguments,
      const CStringList& in_environment) const
   {
      // Set up the parent group id to ensure all children of this child process will belong to its process group, and
      // as such can be cleaned up by the parent.
      if (::setpgid(0, 0) == -1)
         ::exit(s_threadSafeExitError);

      if (clearSignalMask() != 0)
         ::exit(s_threadSafeExitError);

      // Close the side of the pipe that won't be used in the child.
      ::close(in_fds.Input[s_writePipe]);
      ::close(in_fds.Output[s_readPipe]);
      ::close(in_fds.Error[s_readPipe]);
      ::close(in_fds.CloseFd[s_writePipe]);

      // Connect the pipes to the appropriate stream.
      int result = ::dup2(in_fds.Input[s_readPipe], STDIN_FILENO);
      if (result == -1)
         ::_exit(s_threadSafeExitError);

      result = ::dup2(in_fds.Output[s_writePipe], STDOUT_FILENO);
      if (result == -1)
         ::_exit(s_threadSafeExitError);

      result = ::dup2(in_fds.Error[s_writePipe], STDERR_FILENO);
      if (result == -1)
         ::_exit(s_threadSafeExitError);

      // Close any file descriptors that were already open in the parent process. If these FDs are left open, it's
      // possible that this child will clobber the parent's FDs and make it miss notifications that children have
      // exit if the clobbered FDs were being used in epoll calls.
      closeParentFds(in_fds.CloseFd[s_readPipe], in_maxFd);
      ::close(in_fds.CloseFd[s_readPipe]);

      if (in_environment.isEmpty())
         ::execv(RSandbox.c_str(), in_arguments.getData());
      else
         ::execve(RSandbox.c_str(), in_arguments.getData(), in_environment.getData());

      // If we get here the execv(e) call failed.
      ::_exit(s_threadSafeExitError);
   }

   void logProcessSpawn()
   {
      // Before logging, take out the password from the standard input, if any.
      std::string strippedStdin;

      static const int beforeLen = 12;
      int startPos1 = StandardInput.find(R"("password":")");
      int startPos2 = startPos1 + beforeLen;

      bool wasEmpty = true;
      if (startPos1 != std::string::npos)
      {
         while (true)
         {
            startPos2 = StandardInput.find('"', startPos2);
            // This shouldn't be possible.
            assert(startPos2 != std::string::npos);

            // If the password field was empty, don't redact.
            wasEmpty = (startPos2 == (startPos1 + beforeLen));
            if (wasEmpty)
               break;

            int lastSlashPos = startPos2 - 1;
            while (lastSlashPos > (startPos1 + beforeLen))
            {
               if (StandardInput[lastSlashPos] == '\\')
                  --lastSlashPos;
               else
                  break;
            }

            if ((startPos2 - lastSlashPos) % 2 == 0)
               break;
         }

         // Again, not possible - we must have formed the launch profile incorrectly.
         assert(startPos2 != std::string::npos);

         if (wasEmpty)
            strippedStdin = StandardInput;
         else
            strippedStdin =
               StandardInput.substr(0, (startPos1 + beforeLen)) + "<redacted>" + StandardInput.substr(startPos2);
      }

      logging::logDebugMessage(
         "Launching rsandbox. \nArgs " + boost::algorithm::join(Arguments, " ") + "\nLaunch Profile: " + strippedStdin,
         ERROR_LOCATION);
   }

   std::vector<std::string> Arguments;

   std::vector<std::string> Environment;

   std::string StandardInput;

   int StdErrFd;

   int StdInFd;

   int StdOutFd;

   pid_t Pid;

   std::string RSandbox;
};

PRIVATE_IMPL_DELETER_IMPL(AbstractChildProcess)

pid_t AbstractChildProcess::getPid() const
{
   return m_baseImpl->Pid;
}

Error AbstractChildProcess::terminate()
{
   // Don't send a signal if the child isn't running.
   if (m_baseImpl->Pid == -1)
      return systemError(ESRCH, ERROR_LOCATION);

   // Try sending SIGTERM for the whole process group of the child.
   if (::kill(-m_baseImpl->Pid, SIGTERM) == -1)
   {
      // When killing an entire process group EPERM can be returned if even a single one of the subprocesses couldn't be
      // killed. In this case the signal is still delivered and other subprocesses may have been killed so we don't log
      // an error.
      // We also don't consider it an error if the process couldn't be killed because it had already exited (ESRCH).
      if ((errno == EPERM) || (errno == ESRCH))
         return Success();

      return systemError(errno, ERROR_LOCATION);
   }

   return Success();
}

AbstractChildProcess::AbstractChildProcess(const ProcessOptions& in_options) :
   m_baseImpl(new Impl(in_options))
{
}

Error AbstractChildProcess::run()
{
   m_baseImpl->logProcessSpawn();

   // Get the system resource limit for open files. This is needed to allow the child to properly close its files in
   // an async-safe way.
   rlim_t softLimit, hardLimit;
   Error error = getFilesLimit(softLimit, hardLimit);
   if (error)
      return error;

   // Set up process communication pipes.
   FileDescriptors fds;
   error = createPipes(fds);
   if (error)
      return error;

   // Now fork the process.
   pid_t pid;
   error = posix::posixCall<pid_t>(::fork, ERROR_LOCATION, &pid);

   // If this is the child process, execute the requested process.
   if (pid == 0)
      m_baseImpl->execChild(
         fds,
         hardLimit,
         CStringList(m_baseImpl->Arguments),
         CStringList(m_baseImpl->Environment));
   // Otherwise, this is still the parent.
   else
   {
      // Close the unused pipes from the parent's perspective.
      closePipe(fds.Input[s_readPipe], ERROR_LOCATION);
      closePipe(fds.Output[s_writePipe], ERROR_LOCATION);
      closePipe(fds.Error[s_writePipe], ERROR_LOCATION);
      closePipe(fds.CloseFd[s_readPipe], ERROR_LOCATION);

      // Save the relevant StdIn, StdErr, and StdOut pipes for future use.
      m_baseImpl->StdInFd = fds.Input[s_writePipe];
      m_baseImpl->StdOutFd = fds.Output[s_readPipe];
      m_baseImpl->StdErrFd = fds.Error[s_readPipe];

      // Send the list of the child's open pipes to it.
      error = sendFileDescriptors(fds.CloseFd[s_writePipe], pid);
      closePipe(fds.CloseFd[s_writePipe], ERROR_LOCATION);

      return error;
   }

   return Success();
}

// SyncChildProcess ====================================================================================================
SyncChildProcess::SyncChildProcess(const ProcessOptions& in_options) :
   AbstractChildProcess(in_options)
{
}

Error SyncChildProcess::run(ProcessResult& out_result)
{
   // Start the child process and exec as requested.
   Error error = AbstractChildProcess::run();
   if (error)
      return error;

   // Send the requested StdIn, if any.
   if (!m_baseImpl->StandardInput.empty())
   {
      error = writeToPipe(m_baseImpl->StdInFd, m_baseImpl->StandardInput, true);
      if (error)
      {
         Error terminateError = terminate();
         if (terminateError)
            logging::logError(terminateError);
      }
   }

   // Don't return on previous errors because we need to wait for the child process to exit before returning, but don't
   // keep attempting to perform operations on the child process either.

   // If no errors, read standard output.
   if (!error)
      error = readFromPipe(m_baseImpl->StdOutFd, out_result.StdOut);

   // If no errors, read standard error.
   if (!error)
      error = readFromPipe(m_baseImpl->StdErrFd, out_result.StdError);

   // Wait for the process to exit and record the exit code.
   int status;
   pid_t result = posix::posixCall<pid_t>(std::bind(::waitpid, m_baseImpl->Pid, &status, 0));

   if (result == -1)
   {
      out_result.ExitCode = -1;

      // If the child had already exited return success.
      if (errno == ECHILD)
         return Success();

      // Otherwise return an appropriate system error.
      return systemError(errno, ERROR_LOCATION);
   }
   else
      out_result.ExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : status;

   return Success();
}

} // namespace process
} // namespace system
} // namespace launcher_plugins
} // namespace rstudio
