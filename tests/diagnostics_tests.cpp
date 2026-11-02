#include "ui/diagnostics.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    const auto root =
        std::filesystem::temp_directory_path() / ("netmon-diagnostics-" + nm::random_id());
    const auto path = root / L"ui-diagnostics.json";
    HWND window = nullptr;
    try {
        window = CreateWindowW(L"STATIC", L"NETMON_DIAGNOSTIC_PRIVATE_SENTINEL", WS_OVERLAPPED, 0,
                               0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window)
            throw std::runtime_error("Test window creation failed");
        {
            nm::ui::Diagnostics observer(window, WM_APP + 20, path);
            MSG message{};
            message.hwnd = window;
            message.message = WM_CHAR;
            message.wParam = 'K';
            message.time = GetTickCount() - 120;
            const auto token = observer.begin(message);
            Sleep(75);
            observer.end(token);
        }
        const auto encoded = nm::read_text(path, 65536);
        const auto data = nm::Json::parse(encoded);
        if (data.at("input_messages") != 1 || data.at("max_input_queue_ms").get<unsigned>() < 100)
            throw std::runtime_error("Input queue delay not captured");
        if (data.at("max_handler_ms").get<double>() < 60.0 ||
            data.at("recent_slow_handlers").size() != 1)
            throw std::runtime_error("Slow handler not captured");
        if (encoded.find("NETMON_DIAGNOSTIC_PRIVATE_SENTINEL") != std::string::npos ||
            encoded.find("wParam") != std::string::npos || encoded.size() > 65536)
            throw std::runtime_error("Diagnostic privacy/size boundary failed");
        { nm::ui::Diagnostics restarted(window, WM_APP + 20, path); }
        const auto previous =
            nm::Json::parse(nm::read_text(root / L"ui-diagnostics.previous.json", 65536));
        if (previous.at("input_messages") != 1)
            throw std::runtime_error("Previous run was not preserved");
        DestroyWindow(window);
        window = nullptr;
        std::filesystem::remove(path);
        std::filesystem::remove(root / L"ui-diagnostics.previous.json");
        std::filesystem::remove(root);
        std::cout << "Passed 4 diagnostic checks.\n";
        return 0;
    } catch (const std::exception &e) {
        if (window)
            DestroyWindow(window);
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
