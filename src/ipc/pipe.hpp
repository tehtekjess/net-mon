#pragma once
#include "platform/windows.hpp"
#include <functional>
#include <thread>
namespace nm {
inline constexpr wchar_t service_pipe[] = L"\\\\.\\pipe\\NetMon.v1";
std::wstring demo_pipe();
Json call_pipe(const std::wstring &name, const Json &request, HANDLE cancel = nullptr);
class PipeServer {
    HANDLE stop_;
    std::wstring name_;
    std::string owner_;
    std::function<Json(const Json &, const std::function<bool()> &)> handler_;
    std::vector<Handle> pipes_;
    std::vector<std::thread> threads_;
    void run(HANDLE pipe);

  public:
    PipeServer(HANDLE stop, std::wstring name, std::string owner,
               std::function<Json(const Json &, const std::function<bool()> &)> handler);
    ~PipeServer();
};
} // namespace nm
