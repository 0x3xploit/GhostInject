// Main.cpp - Compact DLL Injector UI (About modal, no Viewport API needed)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <commdlg.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include <deque>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include <d3d11.h>
#include "core/header/Injector.h"

#pragma comment(lib, "d3d11.lib")

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include "resource.h"


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// ---------- Logger ----------
struct LogEntry { std::string msg; ImVec4 col; };
class Logger {
public:
    std::deque<LogEntry> list;
    void Add(const char* m, float r = 1, float g = 1, float b = 1) {
        list.push_back({ m,ImVec4(r,g,b,1) });
        if (list.size() > 80) list.pop_front();
    }
} g_logger;

// ---------- DX ----------
bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL lvl;
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &lvl, &g_pd3dDeviceContext) != S_OK)
        return false;

    ID3D11Texture2D* back;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_pd3dDevice->CreateRenderTargetView(back, nullptr, &g_mainRenderTargetView);
    back->Release();
    return true;
}
void CleanupDeviceD3D() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------- Process List ----------
struct ProcessInfo { char name[256]; DWORD pid; };
void GetProcessList(std::vector<ProcessInfo>& out) {
    out.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                ProcessInfo pi{};
                WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1, pi.name, 256, 0, 0);
                pi.pid = pe.th32ProcessID;
                out.push_back(pi);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
}

// ---------- APP ----------
int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    WNDCLASSEXW wc{ sizeof(wc),CS_CLASSDC,WndProc,0,0,inst };
    wc.lpszClassName = L"InjUI";
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_ICON1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"GhostInject",
        WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
        100, 100, 550, 450, nullptr, nullptr, wc.hInstance, nullptr);


    BOOL value = TRUE;

    // Enable dark title bar
    DwmSetWindowAttribute(
        hwnd,
        20,                // DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 1809+)
        &value,
        sizeof(value)
    );


    if (!CreateDeviceD3D(hwnd)) return 1;
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    Injector::SetLogCallback([](const char* m, float r, float g, float b) {
        g_logger.Add(m, r, g, b);
        });

    char dllPath[512] = "";
    char procName[256] = "";
    DWORD pid = 0;
    int method = 1;
    int selected = -1;
    static char procFilter[64] = "";
    std::vector<ProcessInfo> list;
    bool showAbout = false;

    MSG msg{};
    ImGuiIO& io = ImGui::GetIO();
    

    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg); continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RECT rc; GetClientRect(hwnd, &rc);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)(rc.right - rc.left), (float)(rc.bottom - rc.top)));

        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* c = style.Colors;

        // --- DARK TITLE BAR ---
        c[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.0f);   // Normal title bg
        c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);   // Focused title bar
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.03f, 0.03f, 0.03f, 1.0f);   // Collapsed title bg


        ImGui::Begin("Injector", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse);


        // LEFT PANEL
        ImGui::BeginChild("Left", ImVec2(250, -50), true);

        ImGui::Spacing();

        auto DrawHeader = [&](const char* txt) {
            ImVec2 sz = ImGui::CalcTextSize(txt);
            float avail = ImGui::GetContentRegionAvail().x;
            float center = (avail - sz.x) * 0.5f;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, ImVec2(pos.x + avail, pos.y + sz.y + 6), IM_COL32(55, 55, 55, 220), 3);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
            ImGui::SetCursorPosX(center);
            ImGui::TextUnformatted(txt);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);
            };

        DrawHeader("Select Process");
        ImGui::Spacing(); ImGui::Spacing();

        ImGui::PushItemWidth(-1);
        const char* prev = selected >= 0 ? list[selected].name : "Choose";
        if (ImGui::BeginCombo("##Proc", prev)) {
            ImGui::InputText("##Filter", procFilter, sizeof(procFilter));
            GetProcessList(list);
            for (int i = 0; i < list.size(); i++) {
                if (procFilter[0]) {
                    size_t L = strlen(procFilter);
                    if (_strnicmp(list[i].name, procFilter, L) != 0) continue;
                }
                bool sel = selected == i;
                if (ImGui::Selectable(list[i].name, sel)) {
                    selected = i;
                    pid = list[i].pid;
                    strcpy_s(procName, list[i].name);
                    g_logger.Add("Process Selected", .4f, 1.f, .4f);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        if (pid) ImGui::Text("PID %u", pid);

        ImGui::Spacing(); ImGui::Spacing();
        DrawHeader("Select DLL");
        ImGui::Spacing(); ImGui::Spacing();

        if (ImGui::Button("Pick", ImVec2(-1, 22))) {
            OPENFILENAMEA ofn{ sizeof(ofn) };
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = dllPath;
            ofn.nMaxFile = 512;
            ofn.lpstrFilter = "DLL\0*.dll\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) g_logger.Add("DLL Selected", .6f, .8f, 1.f);
        }
        if (*dllPath && strrchr(dllPath, '\\'))
            ImGui::TextWrapped("%s", strrchr(dllPath, '\\') + 1);

        ImGui::Spacing(); ImGui::Spacing();
        DrawHeader("Select Injection Technique");
        ImGui::Spacing(); ImGui::Spacing();

        ImGui::RadioButton("Standard Injection", &method, 0);
        ImGui::RadioButton("Manual Injection", &method, 1);
        ImGui::RadioButton("Manual Encrypted Injection", &method, 2);
        ImGui::EndChild();

        // RIGHT PANEL LOG

       


        ImGui::SameLine();
        ImGui::BeginChild("Right", ImVec2(0, -50), true);

        ImGui::Spacing(); 
        DrawHeader("Logs");
        ImGui::Spacing(); ImGui::Spacing();

        ImGui::Separator();
        int displayed = 0;
        for (auto it = g_logger.list.rbegin(); it != g_logger.list.rend(); ++it) {
            ImGui::TextColored(it->col, "%s", it->msg.c_str());
            if (++displayed >= 15) break;
        }
        ImGui::EndChild();

        // Bottom Buttons
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 45);
        bool ready = pid && *dllPath;
        float w = ImGui::GetWindowWidth();

        if (!ready) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);

        if (ImGui::Button("INJECT", ImVec2(w * 0.50f - 10, 28)) && ready) {
            bool ok = false;
            if (method == 0) ok = Injector::StandardInject(dllPath, pid);
            else if (method == 1) ok = Injector::ManualMapInject(dllPath, pid, false);
            else ok = Injector::ManualMapInject(dllPath, pid, true);
            g_logger.Add(ok ? "SUCCESS" : "FAILED",
                ok ? .4f : 1.f, ok ? 1.f : .4f, .4f);
        }
        if (!ready) ImGui::PopStyleVar();

        ImGui::SameLine();
        if (ImGui::Button("ABOUT", ImVec2(w * 0.25f - 5, 28))) {
            showAbout = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("CLEAR", ImVec2(w * 0.25f - 5, 28))) {
            g_logger.list.clear();
            g_logger.Add("Log cleared", .8f, .8f, .2f);
        }

        ImGui::End();

        // ----- ABOUT MODAL -----
        if (showAbout) {
            ImVec2 disp = ImGui::GetIO().DisplaySize;
            ImVec2 center(disp.x * 0.5f, disp.y * 0.5f);

            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(350, 300));

            if (ImGui::BeginPopupModal("About Injector", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove))
            {
                {
                    const char* txt = "GhostInject";
                    ImVec2 ts = ImGui::CalcTextSize(txt);
                    float cx = (ImGui::GetContentRegionAvail().x - ts.x) * 0.5f;
                    ImGui::SetCursorPosX(cx);
                    ImGui::TextColored(ImVec4(0.6f, 1.f, 0.6f, 1.f), "%s", txt);
                }
                ImGui::Spacing();
                ImGui::Spacing();
                {
                    const char* txt = "A powerfull and efficient DLL injector";
                    ImVec2 ts = ImGui::CalcTextSize(txt);
                    float cx = (ImGui::GetContentRegionAvail().x - ts.x) * 0.5f;
                    ImGui::SetCursorPosX(cx);
                    ImGui::TextColored(ImVec4(0.6f, 1.f, 0.6f, 1.f), "%s", txt);
                }

                ImGui::Spacing();
                ImGui::Spacing();
                
                ImGui::TextWrapped(
                    " . Process auto-filtering\n"
                    " . Three injection strategies\n"
                    " . Built-in log panel\n"
                    " . Manual map with encryption\n"
                    " . 100%% UI responsiveness\n"
                    );

                ImGui::Spacing();
                ImGui::Spacing();

                {
                    const char* txt = "Created By Pratham Naik @2026";
                    ImVec2 ts = ImGui::CalcTextSize(txt);
                    float cx = (ImGui::GetContentRegionAvail().x - ts.x) * 0.5f;
                    ImGui::SetCursorPosX(cx);
                    ImGui::TextColored(ImVec4(0.6f, 1.f, 0.6f, 1.f), "%s", txt);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("Close", ImVec2(-1, 30))) {
                    showAbout = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
            else {
                ImGui::OpenPopup("About Injector");
            }
        }

        ImGui::Render();
        const float clr[4] = { .12f,.12f,.12f,1 };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
