#include "ui/tray.hpp"
#include <chrono>
#include <utility>
namespace nm::ui {
TrayIcon::TrayIcon(HWND window, UINT callback, Transport transport)
    : window_(window), callback_(callback), transport_(std::move(transport)),
      thread_([this] { run(); }) {}
TrayIcon::~TrayIcon() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable())
        thread_.join();
}
void TrayIcon::recreate() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reset_ = true;
    }
    wake_.notify_one();
}
void TrayIcon::set_tip(std::wstring tip) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tip_ == tip)
            return;
        tip_ = std::move(tip);
        dirty_ = true;
    }
    wake_.notify_one();
}
void TrayIcon::notify(std::wstring title, std::wstring body) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Bounded pending state: newest alert summary wins if Explorer is busy.
        balloon_ = Balloon{std::move(title), std::move(body)};
    }
    wake_.notify_one();
}
void TrayIcon::run() {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window_;
    icon.uID = 1;
    icon.uCallbackMessage = callback_;
    icon.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
    bool added = false;
    for (;;) {
        std::wstring tip;
        std::optional<Balloon> balloon;
        bool reset = false, changed = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto ready = [this] { return stop_ || reset_ || dirty_ || balloon_.has_value(); };
            if (added)
                wake_.wait(lock, ready);
            else
                wake_.wait_for(lock, std::chrono::seconds(2), ready);
            if (stop_)
                break;
            reset = std::exchange(reset_, false);
            changed = std::exchange(dirty_, false);
            tip = tip_;
            balloon.swap(balloon_);
        }
        // Never hold the producer mutex while entering the Windows shell.
        if (reset)
            added = false;
        if (!added) {
            icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
            wcsncpy_s(icon.szTip, tip.c_str(), _TRUNCATE);
            added = transport_(NIM_ADD, &icon) != FALSE;
            if (added) {
                icon.uVersion = NOTIFYICON_VERSION_4;
                transport_(NIM_SETVERSION, &icon);
            }
        } else if (changed) {
            icon.uFlags = NIF_TIP;
            wcsncpy_s(icon.szTip, tip.c_str(), _TRUNCATE);
            if (!transport_(NIM_MODIFY, &icon))
                added = false;
        }
        if (added && balloon) {
            icon.uFlags = NIF_INFO;
            icon.dwInfoFlags = NIIF_WARNING;
            wcsncpy_s(icon.szInfoTitle, balloon->title.c_str(), _TRUNCATE);
            wcsncpy_s(icon.szInfo, balloon->body.c_str(), _TRUNCATE);
            transport_(NIM_MODIFY, &icon);
        }
    }
    // Delete even after a failed modify; an old icon might still exist.
    transport_(NIM_DELETE, &icon);
}
} // namespace nm::ui
