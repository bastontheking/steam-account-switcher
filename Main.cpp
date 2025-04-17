#include <iostream>
#include "ImGuiDX11.hpp"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    ImGuiDX11 imguiDX11;
    if (!imguiDX11.Initialize(hInstance, nCmdShow)) {
        return 1;
    }
    while (!imguiDX11.ShouldClose()) {
        imguiDX11.PollEvents();
        imguiDX11.Render();
    }
    imguiDX11.Cleanup();
    return 0;
}