#pragma once
#include <windows.h>
#include <shellapi.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
namespace nm::ui {
// Shell calls can wait for Explorer. Keep them off both the UI and data workers.
class TrayIcon {
  public:
    using Transport = std::function<BOOL(DWORD, PNOTIFYICONDATAW)>;
    TrayIcon(HWND window, UINT callback, Transport transport = Shell_NotifyIconW);
    ~TrayIcon();
    TrayIcon(const TrayIcon &) = delete;
    TrayIcon &operator=(const TrayIcon &) = delete;
    void recreate();
    void set_tip(std::wstring tip);
    void notify(std::wstring title, std::wstring body);

  private:
    struct Balloon {
        std::wstring title, body;
    };
    void run();
    HWND window_;
    UINT callback_;
    Transport transport_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false, reset_ = true, dirty_ = true;
    std::wstring tip_ = L"NetMon - starting";
    std::optional<Balloon> balloon_;
    std::thread thread_;
};
} // namespace nm::ui
