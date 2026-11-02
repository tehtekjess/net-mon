#include "platform/windows.hpp"
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <sddl.h>
#include <shlobj.h>
#include <softpub.h>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <wintrust.h>
namespace nm {
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
                                  static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (!len)
        throw std::runtime_error("Invalid Unicode");
    std::string result(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                        result.data(), len, nullptr, nullptr);
    return result;
}
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                  static_cast<int>(s.size()), nullptr, 0);
    if (!len)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                        result.data(), len);
    return result;
}
std::string win_error(DWORD error) {
    wchar_t *msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<wchar_t *>(&msg), 0, nullptr);
    std::string out = msg ? utf8(msg) : "Windows error";
    if (msg)
        LocalFree(msg);
    return out + " (" + std::to_string(error) + ")";
}
std::filesystem::path data_dir() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &raw)))
        throw std::runtime_error("ProgramData unavailable");
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path / L"NetMon";
}
std::filesystem::path executable_path() {
    std::wstring value(32768, L'\0');
    DWORD count = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    if (!count || count >= value.size())
        throw std::runtime_error("Executable path unavailable");
    value.resize(count);
    return value;
}
std::string token_sid(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
        throw std::runtime_error(win_error());
    LPWSTR str = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &str))
        throw std::runtime_error(win_error());
    std::string result = utf8(str);
    LocalFree(str);
    return result;
}
std::string current_sid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::runtime_error(win_error());
    Handle h(token);
    return token_sid(h.get());
}
bool is_admin() {
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
    PSID sid = nullptr;
    BOOL member = FALSE;
    if (AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0,
                                 0, 0, 0, 0, 0, &sid)) {
        CheckTokenMembership(nullptr, sid, &member);
        FreeSid(sid);
    }
    return member != FALSE;
}
std::string read_text(const std::filesystem::path &path, std::size_t limit) {
    if (std::filesystem::file_size(path) > limit)
        throw std::runtime_error("File exceeds size limit");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read " + utf8(path.wstring()));
    std::string result((std::istreambuf_iterator<char>(in)), {});
    if (result.size() > limit)
        throw std::runtime_error("File exceeds size limit");
    if (result.size() >= 3 && result.substr(0, 3) == "\xEF\xBB\xBF")
        result.erase(0, 3);
    return result;
}
void atomic_write(const std::filesystem::path &path, const std::string &text) {
    if (text.size() > 5 * 1024 * 1024)
        throw std::runtime_error("Configuration exceeds 5 MiB limit");
    auto tmp = path;
    tmp += L".tmp";
    Handle file(CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        throw std::runtime_error(win_error());
    DWORD written = 0;
    if (!WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
        written != text.size() || !FlushFileBuffers(file.get()))
        throw std::runtime_error(win_error());
    file.reset();
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error(win_error());
}
std::string dos_path(const std::string &path) {
    auto w = wide(path);
    if (w.size() < 8 || _wcsnicmp(w.c_str(), L"\\Device\\", 8) != 0)
        return path;
    wchar_t target[32768]{};
    for (wchar_t drive = L'A'; drive <= L'Z'; ++drive) {
        wchar_t letter[] = {drive, L':', 0};
        if (QueryDosDeviceW(letter, target, 32768)) {
            std::wstring prefix(target);
            if (w.size() >= prefix.size() &&
                _wcsnicmp(w.c_str(), prefix.c_str(), prefix.size()) == 0 &&
                (w.size() == prefix.size() || w[prefix.size()] == L'\\'))
                return utf8(std::wstring(letter) + w.substr(prefix.size()));
        }
    }
    return path;
}
std::string hash_file(const std::filesystem::path &path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file)
        return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart > 128LL * 1024 * 1024)
        return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};
    std::string result;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        unsigned char buf[65536];
        DWORD count = 0;
        bool ok = true;
        for (;;) {
            if (!ReadFile(file.get(), buf, sizeof(buf), &count, nullptr)) {
                ok = false;
                break;
            }
            if (!count)
                break;
            if (BCryptHashData(hash, buf, count, 0) < 0) {
                ok = false;
                break;
            }
        }
        unsigned char digest[32]{};
        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0) {
            std::ostringstream out;
            for (auto b : digest)
                out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(b);
            result = out.str();
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}
static std::uint64_t ftvalue(FILETIME ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
void Enricher::enrich(Event &e) {
    e.app = dos_path(e.app);
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.pid));
    e.identity_status.clear();
    if (!process) {
        e.identity_status = "Process unavailable or access denied: " + win_error();
        return;
    }
    FILETIME created{}, exit{}, kernel{}, user{};
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!GetProcessTimes(process.get(), &created, &exit, &kernel, &user) ||
        !QueryFullProcessImageNameW(process.get(), 0, path.data(), &length)) {
        e.identity_status = "Process identity query failed: " + win_error();
        return;
    }
    path.resize(length);
    Millis born = static_cast<Millis>(ftvalue(created) / 10000) - 11644473600000LL;
    if (born > e.time) {
        e.identity_status = "PID now belongs to a newer process";
        return;
    }
    if (lower_ascii(utf8(path)) != lower_ascii(e.app)) {
        e.identity_status = "Process path differs from the source event";
        return;
    }
    e.process_key = std::to_string(e.pid) + ":" + std::to_string(ftvalue(created));
    e.process_verified = true;
    HANDLE raw = nullptr;
    if (OpenProcessToken(process.get(), TOKEN_QUERY, &raw)) {
        Handle token(raw);
        try {
            e.user = token_sid(token.get());
        } catch (...) { /* Optional metadata. */
        }
    }
    auto key = lower_ascii(e.app);
    if (cache_.size() > 2048)
        cache_.clear();
    auto &cache = cache_[key];
    const auto now = now_ms();
    if (now - cache.checked > 5000 || cache.process_key != e.process_key) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) {
            e.identity_status = "Executable metadata unavailable: " + win_error();
            return;
        }
        auto stamp = ftvalue(a.ftLastWriteTime) ^
                     (static_cast<std::uint64_t>(a.nFileSizeHigh) << 32) ^ a.nFileSizeLow;
        if (cache.checked == 0 || cache.stamp != stamp) {
            cache.hash = hash_file(path);
            cache.stamp = stamp;
            WINTRUST_FILE_INFO info{};
            info.cbStruct = sizeof(info);
            info.pcwszFilePath = path.c_str();
            WINTRUST_DATA data{};
            data.cbStruct = sizeof(data);
            data.dwUIChoice = WTD_UI_NONE;
            data.fdwRevocationChecks = WTD_REVOKE_NONE;
            data.dwUnionChoice = WTD_CHOICE_FILE;
            data.pFile = &info;
            data.dwStateAction = WTD_STATEACTION_VERIFY;
            data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
            GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
            LONG status = WinVerifyTrust(nullptr, &action, &data);
            cache.signature = status == ERROR_SUCCESS
                                  ? "trusted"
                                  : (status == TRUST_E_NOSIGNATURE ? "unsigned" : "unverified");
            data.dwStateAction = WTD_STATEACTION_CLOSE;
            WinVerifyTrust(nullptr, &action, &data);
        }
        cache.checked = now;
        cache.process_key = e.process_key;
    }
    e.hash = cache.hash;
    e.signature = cache.signature;
    e.identity_status = e.hash.empty()
                            ? "Executable could not be hashed (locked, unreadable, or over 128 MiB)"
                            : "Live process and executable hash verified";
}
} // namespace nm
