// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Match.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace renzu {
namespace fs = std::filesystem;
static std::mutex storeMutex;
static_assert(sizeof(float) == 4, "Reference format requires IEEE 32-bit float storage");
class LibraryWriteLock {
  public:
    explicit LibraryWriteLock(const fs::path &directory) {
#ifdef _WIN32
        handle_ = CreateFileW((directory / ".lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::system_error(int(GetLastError()), std::system_category(),
                                    "Cannot open reference library save lock");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle_, &info) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(handle_);
            throw std::runtime_error("Invalid reference library save lock");
        }
        OVERLAPPED position{};
        if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &position)) {
            auto error = GetLastError();
            CloseHandle(handle_);
            throw std::system_error(int(error), std::system_category(),
                                    "Cannot acquire reference library save lock");
        }
#else
        handle_ =
            ::open((directory / ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (handle_ < 0)
            throw std::runtime_error("Cannot lock reference library for saving");
        if (::flock(handle_, LOCK_EX) != 0) {
            ::close(handle_);
            throw std::runtime_error("Cannot acquire reference library save lock");
        }
#endif
    }
    ~LibraryWriteLock() {
#ifdef _WIN32
        OVERLAPPED position{};
        UnlockFileEx(handle_, 0, 1, 0, &position);
        CloseHandle(handle_);
#else
        ::flock(handle_, LOCK_UN);
        ::close(handle_);
#endif
    }
    LibraryWriteLock(const LibraryWriteLock &) = delete;
    LibraryWriteLock &operator=(const LibraryWriteLock &) = delete;

  private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
};
static constexpr std::size_t maxSerialized = 256 * 1024;
static std::uint64_t checksum(const std::string &s) {
    std::uint64_t v = 14695981039346656037ull;
    for (unsigned char c : s) {
        v ^= c;
        v *= 1099511628211ull;
    }
    return v;
}
static std::string identifier() {
    static std::atomic<std::uint64_t> serial{0};
    std::random_device random;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::hex << std::setfill('0') << std::setw(16)
        << (std::uint64_t(random()) << 32 | random()) << std::setw(16)
        << (std::uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count()) ^
            ++serial);
    return out.str();
}
static bool validId(const std::string &id) {
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}
static void validate(const ReferenceFrame &r) {
    if (!validId(r.id) || r.slot < 1 || r.slot > referenceSlots || r.space < 0 || r.space > 2 ||
        !std::isfinite(r.time) || r.name.empty() || r.name.size() > 240 ||
        r.name.find_first_of("\r\n\0", 0, 3) != std::string::npos || r.pixels.empty() ||
        r.pixels.size() > maxReferenceSamples)
        throw std::runtime_error("Invalid reference metadata");
    for (auto c : r.pixels)
        if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b) ||
            std::max({std::abs(c.r), std::abs(c.g), std::abs(c.b)}) > 1.e12f)
            throw std::runtime_error("Invalid reference pixel data");
}
std::string serializeReference(const ReferenceFrame &r) {
    validate(r);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "RENZU_REFERENCE 1\n"
        << r.id << ' ' << r.slot << ' ' << r.space << ' ' << std::setprecision(17) << r.time << '\n'
        << std::quoted(r.name) << '\n'
        << r.pixels.size() << '\n'
        << std::hex << std::setfill('0');
    for (auto c : r.pixels)
        for (float v : {c.r, c.g, c.b}) {
            std::uint32_t bits;
            std::memcpy(&bits, &v, 4);
            out << std::setw(8) << bits;
        }
    out << '\n';
    auto body = out.str();
    out << std::setw(16) << checksum(body) << '\n';
    return out.str();
}
ReferenceFrame deserializeReference(const std::string &text) {
    if (text.size() > maxSerialized)
        throw std::runtime_error("Reference exceeds size limit");
    auto split = text.rfind('\n', text.size() > 1 ? text.size() - 2 : 0);
    if (split == std::string::npos || text.empty() || text.back() != '\n')
        throw std::runtime_error("Truncated reference");
    std::string body = text.substr(0, split + 1), hashText = text.substr(split + 1);
    std::istringstream hashStream(hashText);
    hashStream.imbue(std::locale::classic());
    std::uint64_t hash = 0;
    if (!(hashStream >> std::hex >> hash) || hash != checksum(body))
        throw std::runtime_error("Reference checksum mismatch");
    std::istringstream in(body);
    in.imbue(std::locale::classic());
    std::string magic, payload;
    int version = 0;
    std::size_t count = 0;
    ReferenceFrame r;
    if (!(in >> magic >> version) || magic != "RENZU_REFERENCE" || version != 1 ||
        !(in >> r.id >> r.slot >> r.space >> r.time >> std::quoted(r.name) >> count >> payload) ||
        count < 1 || count > maxReferenceSamples || payload.size() != count * 24 ||
        payload.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Invalid reference format");
    r.pixels.reserve(count);
    for (std::size_t j = 0; j < count; ++j) {
        float channels[3];
        for (int c = 0; c < 3; ++c) {
            auto bits = std::uint32_t(std::stoul(payload.substr(j * 24 + c * 8, 8), nullptr, 16));
            std::memcpy(&channels[c], &bits, 4);
        }
        r.pixels.push_back({channels[0], channels[1], channels[2]});
    }
    validate(r);
    return r;
}
static std::string read(const fs::path &p) {
#ifdef _WIN32
    // Allow atomic replacement while a reader retains the previous snapshot.
    // The CRT's ifstream does not open files with FILE_SHARE_DELETE.
    HANDLE handle =
        CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::system_error(int(GetLastError()), std::system_category(),
                                "Reference file unavailable");
    auto closeHandle = [](void *h) { CloseHandle(h); };
    std::unique_ptr<void, decltype(closeHandle)> file(handle, closeHandle);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
        throw std::runtime_error("Invalid reference file or symbolic link");
    if (info.nFileSizeHigh != 0 || info.nFileSizeLow > maxSerialized)
        throw std::runtime_error("Reference file exceeds size limit");
    std::string value(info.nFileSizeLow, '\0');
    DWORD bytesRead = 0;
    if (!ReadFile(handle, value.data(), info.nFileSizeLow, &bytesRead, nullptr) ||
        bytesRead != info.nFileSizeLow)
        throw std::runtime_error("Cannot read complete reference file");
    return value;
#else
    if (fs::is_symlink(p))
        throw std::runtime_error("Refusing a reference file symbolic link");
    if (!fs::is_regular_file(p))
        throw std::runtime_error(
            "Reference file unavailable. Save a reference or choose another slot.");
    auto size = fs::file_size(p);
    if (size > maxSerialized)
        throw std::runtime_error("Reference file exceeds size limit");
    std::ifstream in(p, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read reference file");
    std::string value(std::size_t(size), '\0');
    in.read(value.data(), std::streamsize(size));
    if (in.gcount() != std::streamsize(size) || in.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Reference file changed while reading");
    return value;
#endif
}
static void atomicWrite(const fs::path &path, const std::string &value) {
    auto temporary = path;
    temporary += ".tmp-" + identifier();
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(value.data(), std::streamsize(value.size()));
        out.close();
        if (!out)
            throw std::runtime_error("Cannot write reference file");
#ifdef _WIN32
        // Unlike POSIX rename, Windows filesystem::rename cannot replace an
        // existing slot/latest file. Never delete the old snapshot first.
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(int(GetLastError()), std::system_category(),
                                    "Cannot replace reference file");
#else
        fs::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
fs::path ReferenceStore::defaultDirectory() {
    // Test harnesses supply an isolated directory; no reference storage is opened
    // during ordinary render. Environment variables are read, never changed.
#ifdef _WIN32
    // Wide Windows APIs also support non-ASCII user names and library locations.
    if (const wchar_t *overridePath = _wgetenv(L"RENZU_REFERENCE_DIRECTORY")) {
        if (*overridePath)
            return fs::path(overridePath);
    }
    PWSTR localAppData = nullptr;
    auto status =
        SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &localAppData);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(localAppData, &CoTaskMemFree);
    if (FAILED(status) || !localAppData || !*localAppData)
        throw std::runtime_error("Cannot locate the user's reference library");
    return fs::path(localAppData) / L"Renzu Grade" / L"References";
#else
    if (const char *overridePath = std::getenv("RENZU_REFERENCE_DIRECTORY")) {
        if (*overridePath)
            return fs::path(overridePath);
    }
#ifndef __APPLE__
    if (const char *data = std::getenv("XDG_DATA_HOME")) {
        if (*data && fs::path(data).is_absolute())
            return fs::path(data) / "renzu-grade/references";
    }
#endif
    const char *home = std::getenv("HOME");
    if (!home || !*home)
        throw std::runtime_error("Cannot locate the user's reference library");
#ifdef __APPLE__
    return fs::path(home) / "Library/Application Support/Renzu Grade/References";
#else
    return fs::path(home) / ".local/share/renzu-grade/references";
#endif
#endif
}
static fs::path slotPath(const fs::path &root, int slot) {
    if (slot < 1 || slot > referenceSlots)
        throw std::runtime_error("Reference slot out of range");
    return root / ("reference-" + std::to_string(slot) + ".rgr");
}
std::vector<ReferenceSummary> ReferenceStore::list() const {
    std::lock_guard<std::mutex> lock(storeMutex);
    std::vector<ReferenceSummary> refs;
    for (int slot = 1; slot <= referenceSlots; ++slot) {
        auto path = slotPath(directory_, slot);
        if (!fs::exists(path))
            continue;
        try {
            auto r = deserializeReference(read(path));
            if (r.slot == slot)
                refs.push_back({slot, r.id, r.name});
        } catch (const std::exception
                     &) { /* Corrupt slots remain isolated; explicit load reports the error. */
        }
    }
    return refs;
}
ReferenceFrame ReferenceStore::save(ReferenceFrame r, int slot) const {
    std::lock_guard<std::mutex> lock(storeMutex);
    fs::create_directories(directory_);
    if (fs::is_symlink(directory_))
        throw std::runtime_error("Refusing a reference library symbolic link");
    LibraryWriteLock writeLock(directory_);
    if (slot == 0) {
        for (int n = 1; n <= referenceSlots; ++n)
            if (!fs::exists(slotPath(directory_, n))) {
                slot = n;
                break;
            }
    }
    if (slot == 0)
        throw std::runtime_error("Reference library full: select a slot explicitly to replace it");
    r.slot = slot;
    r.id = identifier();
    if (r.name.empty())
        r.name = "Frame " + std::to_string(r.time);
    validate(r);
    atomicWrite(slotPath(directory_, slot), serializeReference(r));
    atomicWrite(directory_ / "latest", std::to_string(slot));
    return r;
}
ReferenceFrame ReferenceStore::load(int slot) const {
    std::lock_guard<std::mutex> lock(storeMutex);
    if (slot == 0) {
        auto text = read(directory_ / "latest");
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        if (!(in >> slot))
            throw std::runtime_error("No latest reference available");
    }
    auto r = deserializeReference(read(slotPath(directory_, slot)));
    if (r.slot != slot)
        throw std::runtime_error("Reference slot metadata mismatch");
    return r;
}
} // namespace renzu
