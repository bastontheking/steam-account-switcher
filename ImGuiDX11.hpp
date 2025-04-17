#pragma once
#define NOMINMAX               // desativa macros min/max do Windows

#include <d3d11.h>
#include <d3dx11tex.h>        // para D3DX11CreateShaderResourceViewFromFileA
#include <tchar.h>
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <Windows.h>
#include <Shellapi.h>

#include <regex>
#include <sstream>
#include <ShlObj.h> 
#include <tlhelp32.h>

// Paths
static const char* LOGINUSERS_PATH = "C:\\Program Files (x86)\\Steam\\config\\loginusers.vdf";
static const char* AVATAR_CACHE = "C:\\Program Files (x86)\\Steam\\config\\avatarcache";
static const char* PLACEHOLDER = "placeholder.png";
static const char* STEAM_EXE = "C:\\Program Files (x86)\\Steam\\Steam.exe";

// Variáveis para o GUI
namespace guicoisas {
    inline bool order = true;
    inline POINTS position = {};      // Local da janela para poder mover
    inline bool exit = true;     // Sinalização de saída do programa
    inline float W = 500.f, H = 300.f; // Tamanho do GUI
    inline HWND hwnd = nullptr;  // Janela onde está o menu

    // Ponteiros D3D para carregar texturas
    inline ID3D11Device* pd3dDevice = nullptr;
    inline ID3D11DeviceContext* pd3dDeviceContext = nullptr;

    // Estrutura de conta
    struct Account {
        std::string steamid;
        std::string personaName;
        ImTextureID avatar = nullptr;
    };
    inline std::vector<Account> accounts;
    inline std::vector<Account> originalAccounts;

    // Helpers de string
    static std::string Trim(const std::string& s) {
        auto b = s.find_first_not_of(" \t\r\n");
        auto e = s.find_last_not_of(" \t\r\n");
        return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
    }
    static std::string TrimQuotes(const std::string& s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    }
    static std::string ExtractValue(const std::string& line) {
        size_t p1 = line.find_last_of('"');
        if (p1 == std::string::npos) return "";
        size_t p2 = line.find_last_of('"', p1 - 1);
        if (p2 == std::string::npos) return "";
        return line.substr(p2 + 1, p1 - p2 - 1);
    }

    inline std::string RemoveInvisibleUnicode(const std::string& str) {
        std::string result;
        size_t i = 0;

        while (i < str.size()) {
            unsigned char c = str[i];

            // TAG characters (U+E0000 to U+E007F) in UTF-8 = F3 A0 80 [A0–BF]
            if ((unsigned char)c == 0xF3 && i + 3 < str.size()) {
                unsigned char b2 = str[i + 1];
                unsigned char b3 = str[i + 2];
                unsigned char b4 = str[i + 3];

                if (b2 == 0xA0 && b3 == 0x80 && (b4 >= 0xA0 && b4 <= 0xBF)) {
                    i += 4;
                    continue; // skip TAG character
                }
            }

            // Common 3-byte invisibles
            if ((c & 0xF0) == 0xE0 && i + 2 < str.size()) {
                std::string seq = str.substr(i, 3);
                if (seq == "\xE2\x80\x8B" || // U+200B
                    seq == "\xE2\x80\x8C" || // U+200C
                    seq == "\xE2\x80\x8D" || // U+200D
                    seq == "\xEF\xBB\xBF")   // U+FEFF
                {
                    i += 3;
                    continue;
                }
            }

            result += c;
            ++i;
        }

        return result;
    }

    inline void KillProcessByName(const std::wstring& name) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;

        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);

        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                    if (hProcess) {
                        TerminateProcess(hProcess, 0);
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    // Recarrega contas e avatares
    inline void LoadAccounts() {
        // libera SRVs antigos
        for (auto& a : accounts) {
            if (a.avatar) {
                ((ID3D11ShaderResourceView*)a.avatar)->Release();
            }
        }
        accounts.clear();

        std::ifstream file(LOGINUSERS_PATH);
        if (!file.is_open()) return;

        std::string line;
        Account cur;
        bool inBlock = false;
        while (std::getline(file, line)) {
            std::string s = Trim(line);
            if (s.rfind("\"7656", 0) == 0) {
                cur = Account();
                cur.steamid = TrimQuotes(s);
                inBlock = true;
            }
            else if (inBlock && s.rfind("\"PersonaName\"", 0) == 0) {
                cur.personaName = RemoveInvisibleUnicode(ExtractValue(s));
            }
            else if (inBlock && s == "}") {
                // carrega avatar via D3DX11
                std::string path = std::string(AVATAR_CACHE) + "\\" + cur.steamid + ".png";
                if (!std::filesystem::exists(path)) path = PLACEHOLDER;
                ID3D11ShaderResourceView* srv = nullptr;
                HRESULT hr = D3DX11CreateShaderResourceViewFromFileA(
                    pd3dDevice, path.c_str(), nullptr, nullptr, &srv, nullptr);
                if (SUCCEEDED(hr)) {
                    cur.avatar = (ImTextureID)srv;
                }
                else {
                    cur.avatar = nullptr;
                }
                accounts.push_back(cur);
                inBlock = false;
            }
        }
        file.close();
        originalAccounts = accounts; // Salva ordem original para alternância dinâmica
    }

    inline void RealizarLogin(const std::string& steamid) {
        // Fecha processos da Steam
        KillProcessByName(L"steam.exe");
        KillProcessByName(L"steamservice.exe");
        KillProcessByName(L"steamwebhelper.exe");
        KillProcessByName(L"cs2.exe");

        Sleep(1000);

        std::ifstream in(LOGINUSERS_PATH);
        if (!in.is_open()) return;

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        in.close();

        bool insideBlock = false;
        std::string currentSteamId;
        std::string selectedAccountName;

        for (size_t i = 0; i < lines.size(); ++i) {
            std::string trimmed = guicoisas::Trim(lines[i]);

            if (trimmed.starts_with("\"7656")) {
                currentSteamId = guicoisas::TrimQuotes(trimmed);
                insideBlock = true;
            }
            else if (insideBlock) {
                if (trimmed.starts_with("\"MostRecent\"")) {
                    lines[i] = "        \"MostRecent\"\t\t\"" + std::string(currentSteamId == steamid ? "1" : "0") + "\"";
                }
                else if (trimmed.starts_with("\"AllowAutoLogin\"")) {
                    lines[i] = "        \"AllowAutoLogin\"\t\t\"" + std::string(currentSteamId == steamid ? "1" : "0") + "\"";
                }
                else if (trimmed.starts_with("\"Timestamp\"")) {
                    lines[i] = "        \"Timestamp\"\t\t\"" + (currentSteamId == steamid ? std::to_string(time(nullptr)) : guicoisas::ExtractValue(trimmed)) + "\"";
                }
                else if (trimmed.starts_with("\"AccountName\"") && currentSteamId == steamid) {
                    selectedAccountName = guicoisas::ExtractValue(trimmed);
                }
                else if (trimmed == "}") {
                    insideBlock = false;
                    currentSteamId.clear();
                }
            }
        }

        std::ofstream out(LOGINUSERS_PATH, std::ios::trunc);
        if (out.is_open()) {
            for (const auto& l : lines) {
                out << l << "\n";
            }
            out.close();
        }

        // Registro: AutoLoginUser + RememberPassword
        if (!selectedAccountName.empty()) {
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegSetValueExA(hKey, "AutoLoginUser", 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(selectedAccountName.c_str()),
                    static_cast<DWORD>(selectedAccountName.size() + 1));
                DWORD remember = 1;
                RegSetValueExA(hKey, "RememberPassword", 0, REG_DWORD,
                    reinterpret_cast<const BYTE*>(&remember), sizeof(DWORD));
                RegCloseKey(hKey);
            }
        }

        // Reinicia Steam
        ShellExecuteA(NULL, "open", STEAM_EXE, NULL, NULL, SW_SHOWNORMAL);
    }

    inline void AddNewSteamLogin() {
        // Fecha processos
        KillProcessByName(L"steam.exe");
        KillProcessByName(L"steamservice.exe");
        KillProcessByName(L"steamwebhelper.exe");
        KillProcessByName(L"cs2.exe");
        Sleep(1000);

        // Limpa loginusers.vdf: zera MostRecent e AllowAutoLogin
        std::ifstream in(LOGINUSERS_PATH);
        if (!in.is_open()) return;

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        in.close();

        for (std::string& l : lines) {
            std::string trimmed = guicoisas::Trim(l);
            if (trimmed.starts_with("\"MostRecent\"")) {
                l = "        \"MostRecent\"\t\t\"0\"";
            }
            else if (trimmed.starts_with("\"AllowAutoLogin\"")) {
                l = "        \"AllowAutoLogin\"\t\t\"0\"";
            }
        }

        std::ofstream out(LOGINUSERS_PATH, std::ios::trunc);
        if (out.is_open()) {
            for (const auto& l : lines) {
                out << l << "\n";
            }
            out.close();
        }

        // Limpa AutoLoginUser do registro
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueA(hKey, "AutoLoginUser");
            RegCloseKey(hKey);
        }

        // Abre Steam sem conta logada
        ShellExecuteA(NULL, "open", STEAM_EXE, NULL, NULL, SW_SHOWNORMAL);
    }


    // Função de texto centralizado (mantida do original)
    void text(std::string text) {
        float availableWidth = ImGui::GetContentRegionAvail().x;
        ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        float textWidth = textSize.x;
        float offsetX = (availableWidth - textWidth) / 2.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        ImGui::Text(text.c_str());
    }

} // namespace guicoisas

// Declarar a função ImGui_ImplWin32_WndProcHandler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// WndProc
LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);

class ImGuiDX11 {
public:
    bool Initialize(HINSTANCE hInstance, int) {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        WNDCLASSEX wc = { sizeof(wc), CS_CLASSDC, WndProc, 0,0,
                          GetModuleHandle(NULL), NULL,NULL,NULL,NULL,
                          _T("baston.dev - steam account switcher"), NULL };
        RegisterClassEx(&wc);
        hwnd = CreateWindow(wc.lpszClassName,
            _T("baston.dev - steam account switcher"),
            WS_POPUP,
            (screenW - guicoisas::W) / 2,
            (screenH - guicoisas::H) / 2,
            (int)guicoisas::W, (int)guicoisas::H,
            NULL, NULL, wc.hInstance, NULL);

        if (!CreateDeviceD3D(hwnd)) {
            CleanupDeviceD3D();
            UnregisterClass(wc.lpszClassName, wc.hInstance);
            return false;
        }

        ShowWindow(hwnd, SW_SHOWDEFAULT);
        UpdateWindow(hwnd);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

        // salva ponteiros D3D para o namespace
        guicoisas::pd3dDevice = g_pd3dDevice;
        guicoisas::pd3dDeviceContext = g_pd3dDeviceContext;

        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
        guicoisas::hwnd = hwnd;
        io.IniFilename = nullptr;

        // aplica tema original (cores e estilos)...
                // Theme
        ImVec4* colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
        colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
        colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
        colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.561f, 0.561f, 0.561f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
        colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(8.00f, 8.00f);
        style.FramePadding = ImVec2(5.00f, 2.00f);
        style.CellPadding = ImVec2(6.00f, 6.00f);
        style.ItemSpacing = ImVec2(6.00f, 6.00f);
        style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
        style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
        style.IndentSpacing = 25;
        style.ScrollbarSize = 15;
        style.GrabMinSize = 10;
        style.WindowBorderSize = 1;
        style.ChildBorderSize = 1;
        style.PopupBorderSize = 1;
        style.FrameBorderSize = 1;
        style.TabBorderSize = 1;
        style.ChildRounding = 4;
        style.FrameRounding = 3;
        style.PopupRounding = 4;
        style.ScrollbarRounding = 9;
        style.GrabRounding = 3;
        style.LogSliderDeadzone = 4;
        style.TabRounding = 4;

        style.AntiAliasedLines = true;
        style.AntiAliasedFill = true;
        style.AntiAliasedLinesUseTex = true;

        // carrega contas pela primeira vez
        guicoisas::LoadAccounts();

        return true;
    }

    void Render() {

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({ 0,0 });
        ImGui::SetNextWindowSize({ guicoisas::W, guicoisas::H });

        if (!ImGui::Begin("baston.dev - steam account switcher",
            &guicoisas::exit,
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove))
        {
            ImGui::End();
            return;
        }

        // Botão de refresh
        if (ImGui::Button("Update List")) {
            guicoisas::LoadAccounts();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Account")) {
            guicoisas::AddNewSteamLogin();
        }
        ImGui::SameLine();
        ImGui::Checkbox("alphabetical order", &guicoisas::order);

        ImGui::Separator();

        const float ICON_SZ = 16.0f;
        const float SPACING = 8.0f;
        const float BUTTON_W = 80.0f;

        ImGui::BeginChild("##AccountList", ImVec2(0, 0), false);

        std::vector<guicoisas::Account> displayAccounts = guicoisas::accounts;

        if (guicoisas::order) {
            std::sort(displayAccounts.begin(), displayAccounts.end(),
                [](const guicoisas::Account& a, const guicoisas::Account& b) {
                    return _stricmp(a.personaName.c_str(), b.personaName.c_str()) < 0;
                });
        }
        else {
            displayAccounts = guicoisas::originalAccounts;
        }

        for (auto& a : displayAccounts) {
            ImGui::BeginGroup();

            // 1) botão Conectar
            if (ImGui::Button(("Open Acc##" + a.steamid).c_str(), ImVec2(BUTTON_W, 0))) {
                guicoisas::RealizarLogin(a.steamid);
            }

            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + SPACING);

            // 2) avatar
            if (a.avatar)
                ImGui::Image(a.avatar, ImVec2(ICON_SZ, ICON_SZ));
            else
                ImGui::Dummy(ImVec2(ICON_SZ, ICON_SZ));

            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + SPACING);

            // 3) nome da conta
            ImGui::TextUnformatted(a.personaName.c_str());

            ImGui::EndGroup();
            ImGui::Separator();
        }

        ImGui::End();

        if (!guicoisas::exit)
            PostQuitMessage(0);

        ImGui::Render();
        const float clear_color[4] = { 0.45f,0.55f,0.60f,1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    void Cleanup() {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupDeviceD3D();
        if (hwnd) DestroyWindow(hwnd), hwnd = nullptr;
    }

    bool ShouldClose() { return msg.message == WM_QUIT; }

    void PollEvents() {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

private:
    bool CreateDeviceD3D(HWND hWnd) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT createFlags = 0;
#ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
            createFlags, levels, 2,
            D3D11_SDK_VERSION,
            &sd, &g_pSwapChain,
            &g_pd3dDevice,
            &featureLevel,
            &g_pd3dDeviceContext) != S_OK)
            return false;
        CreateRenderTarget();
        return true;
    }

    void CleanupDeviceD3D() {
        CleanupRenderTarget();
        if (g_pSwapChain) { g_pSwapChain->Release();           g_pSwapChain = nullptr; }
        if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release();    g_pd3dDeviceContext = nullptr; }
        if (g_pd3dDevice) { g_pd3dDevice->Release();           g_pd3dDevice = nullptr; }
    }

    void CreateRenderTarget() {
        ID3D11Texture2D* bb;
        g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
        g_pd3dDevice->CreateRenderTargetView(bb, NULL, &g_mainRenderTargetView);
        bb->Release();
    }

    void CleanupRenderTarget() {
        if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    }

    HWND                    hwnd = nullptr;
    MSG                     msg = {};
    ID3D11Device* g_pd3dDevice = nullptr;
    ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
    IDXGISwapChain* g_pSwapChain = nullptr;
    ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

    friend LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);
};

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    ImGuiDX11* pThis = (ImGuiDX11*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (pThis) {
        switch (msg) {
        case WM_SIZE:
            if (pThis->g_pd3dDevice && wParam != SIZE_MINIMIZED) {
                pThis->CleanupRenderTarget();
                pThis->g_pSwapChain->ResizeBuffers(0,
                    (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                    DXGI_FORMAT_UNKNOWN, 0);
                pThis->CreateRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0); return 0;
        case WM_LBUTTONDOWN:
            guicoisas::position = MAKEPOINTS(lParam);
            return 0;
        case WM_MOUSEMOVE:
            if (wParam == MK_LBUTTON) {
                auto pts = MAKEPOINTS(lParam);
                RECT r; GetWindowRect(guicoisas::hwnd, &r);
                r.left += pts.x - guicoisas::position.x;
                r.top += pts.y - guicoisas::position.y;
                if (guicoisas::position.x >= 0 && guicoisas::position.x <= guicoisas::W
                    && guicoisas::position.y >= 0 && guicoisas::position.y <= 19)
                {
                    SetWindowPos(guicoisas::hwnd, HWND_TOPMOST,
                        r.left, r.top, 0, 0,
                        SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOZORDER);
                }
            }
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
