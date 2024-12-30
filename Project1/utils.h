#pragma once
#include <windows.h>
#include <d3d9.h>
#include <tlhelp32.h>

// Définitions
#define START_OFFSET_DEFAULT 0x400000

bool GetD3D9Device(void** pTable, size_t size) {
    MessageBoxA(NULL, "Début GetD3D9Device", "Debug", MB_OK);

    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        MessageBoxA(NULL, "Direct3DCreate9 a échoué", "Debug", MB_OK);
        return false;
    }

    // Obtention de la fenêtre du jeu
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        MessageBoxA(NULL, "GetForegroundWindow a échoué", "Debug", MB_OK);
        pD3D->Release();
        return false;
    }

    // Obtention des informations d'affichage
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    if (!GetMonitorInfo(monitor, &info)) {
        MessageBoxA(NULL, "GetMonitorInfo a échoué", "Debug", MB_OK);
        pD3D->Release();
        return false;
    }

    int width = info.rcMonitor.right - info.rcMonitor.left;
    int height = info.rcMonitor.bottom - info.rcMonitor.top;

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.BackBufferWidth = width;
    d3dpp.BackBufferHeight = height;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferCount = 1;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.MultiSampleQuality = 0;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hwnd;
    d3dpp.Windowed = TRUE;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3dpp.Flags = 0;
    d3dpp.FullScreen_RefreshRateInHz = 0;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* pDevice = nullptr;
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
        &d3dpp, &pDevice);

    if (FAILED(hr)) {
        // Si le hardware processing échoue, essayons le software
        hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
            &d3dpp, &pDevice);

        if (FAILED(hr)) {
            char msg[256];
            sprintf_s(msg, "CreateDevice a échoué avec l'erreur: 0x%X", hr);
            MessageBoxA(NULL, msg, "Debug", MB_OK);
            pD3D->Release();
            return false;
        }
    }

    MessageBoxA(NULL, "Device créé avec succès", "Debug", MB_OK);

    void** vtable = *reinterpret_cast<void***>(pDevice);
    if (vtable) {
        memcpy(pTable, vtable, size);
        char msg[256];
        sprintf_s(msg, "EndScene offset: 0x%p", vtable[42]);
        MessageBoxA(NULL, msg, "Debug", MB_OK);
    }

    pDevice->Release();
    pD3D->Release();

    MessageBoxA(NULL, "GetD3D9Device terminé avec succès", "Debug", MB_OK);
    return true;
}

// Fonction pour obtenir la fenêtre du processus
inline HWND GetProcessWindow() {
    HWND hwnd = FindWindowA("NFSWindowClassName", NULL);  // Essaie de trouver la fenêtre NFS d'abord
    if (hwnd) return hwnd;

    // Si on ne trouve pas la fenêtre NFS, cherche une fenêtre générique du processus
    for (HWND hwnd = GetTopWindow(NULL); hwnd != NULL; hwnd = GetNextWindow(hwnd, GW_HWNDNEXT)) {
        DWORD procId;
        GetWindowThreadProcessId(hwnd, &procId);
        if (GetCurrentProcessId() == procId) {
            CHAR windowTitle[256];
            GetWindowTextA(hwnd, windowTitle, 256);
            RECT rect;
            GetWindowRect(hwnd, &rect);
            if (rect.right - rect.left > 100 && rect.bottom - rect.top > 100) {
                return hwnd;
            }
        }
    }
    return NULL;
}