#pragma once

#include <windows.h>

#include <functional>

namespace WindowsHelper {

static LRESULT CALLBACK WndProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
    if (uMsg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static inline HWND createWindow(HINSTANCE hInstance) {
    WNDCLASS wndClass{};
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = reinterpret_cast<WNDPROC>(WindowsHelper::WndProc);
    wndClass.hInstance = hInstance;
    wndClass.hCursor = LoadCursor(hInstance, IDC_ARROW);
    wndClass.hbrBackground
        = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wndClass.lpszClassName = L"daily-snippet";

    const auto atom = RegisterClassW(&wndClass);

    if (!atom) {
        throw std::runtime_error("Window class registration failed");
    }

    auto hWnd = CreateWindowW(MAKEINTATOM(atom), L"daily-snippet",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 480, nullptr,
        nullptr, hInstance, nullptr);

    if (!hWnd) {
        throw std::runtime_error("Window creation failed");
    }

    return hWnd;
}

static inline SIZE getWindowSize(HWND hWnd)
{
    RECT r{};
    GetWindowRect(hWnd, &r);
    return SIZE{ r.left - r.right, r.top - r.bottom };
}

static inline int mainLoop(std::function<void()> update = [] {})
{
    MSG msg{};

    while (true) {
        if (!PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            update();
            continue;
        }

        if (msg.message == WM_QUIT) {
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}

} // namespace WindowsHelper
