// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace test {
// Keep storage isolated even when the user's profile/test path contains Unicode.
class Environment {
  public:
    Environment(const char *name, const std::filesystem::path &value) : name_(name) {
#ifdef _WIN32
        auto wideName = std::filesystem::path(name_).wstring();
        if (const auto *previous = _wgetenv(wideName.c_str())) {
            previous_ = previous;
            hadPrevious_ = true;
        }
        if (_wputenv_s(wideName.c_str(), value.c_str()) != 0)
            throw std::runtime_error("Cannot set isolated test environment");
#else
        if (const auto *previous = std::getenv(name)) {
            previous_ = previous;
            hadPrevious_ = true;
        }
        if (setenv(name, value.c_str(), 1) != 0)
            throw std::runtime_error("Cannot set isolated test environment");
#endif
    }
    ~Environment() {
#ifdef _WIN32
        _wputenv_s(std::filesystem::path(name_).c_str(), hadPrevious_ ? previous_.c_str() : L"");
#else
        if (hadPrevious_)
            setenv(name_.c_str(), previous_.c_str(), 1);
        else
            unsetenv(name_.c_str());
#endif
    }
    Environment(const Environment &) = delete;
    Environment &operator=(const Environment &) = delete;

  private:
    std::string name_;
    std::filesystem::path::string_type previous_;
    bool hadPrevious_ = false;
};

class SharedLibrary {
  public:
    explicit SharedLibrary(const std::filesystem::path &path) {
#ifdef _WIN32
        handle_ = LoadLibraryW(path.c_str());
        if (!handle_)
            throw std::runtime_error("LoadLibraryW failed: " + std::to_string(GetLastError()));
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_)
            throw std::runtime_error(dlerror());
#endif
    }
    ~SharedLibrary() {
#ifdef _WIN32
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
    }
    template <class Function> Function symbol(const char *name) const {
#ifdef _WIN32
        return reinterpret_cast<Function>(GetProcAddress(handle_, name));
#else
        return reinterpret_cast<Function>(dlsym(handle_, name));
#endif
    }
    SharedLibrary(const SharedLibrary &) = delete;
    SharedLibrary &operator=(const SharedLibrary &) = delete;

  private:
#ifdef _WIN32
    HMODULE handle_ = nullptr;
#else
    void *handle_ = nullptr;
#endif
};

#ifdef _WIN32
class ReferenceWriter {
  public:
    ReferenceWriter(const std::filesystem::path &executable,
                    const std::filesystem::path &directory) {
        // Both arguments are absolute file paths with no trailing separator;
        // quotes are not legal Windows filename characters.
        std::wstring command =
            L"\"" + executable.native() + L"\" --save-reference \"" + directory.native() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            nullptr, &startup, &process))
            throw std::runtime_error("Cannot spawn reference writer: " +
                                     std::to_string(GetLastError()));
        handle_ = process.hProcess;
        CloseHandle(process.hThread);
    }
    bool wait() {
        if (WaitForSingleObject(handle_, 30000) != WAIT_OBJECT_0)
            return false;
        DWORD code = 1;
        return GetExitCodeProcess(handle_, &code) && code == 0;
    }
    ~ReferenceWriter() {
        if (WaitForSingleObject(handle_, 0) == WAIT_TIMEOUT) {
            TerminateProcess(handle_, 1);
            WaitForSingleObject(handle_, 5000);
        }
        CloseHandle(handle_);
    }
    ReferenceWriter(const ReferenceWriter &) = delete;
    ReferenceWriter &operator=(const ReferenceWriter &) = delete;

  private:
    HANDLE handle_ = nullptr;
};
#endif
} // namespace test
