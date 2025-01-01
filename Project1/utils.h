#pragma once
#include <windows.h>
#include <d3d9.h>
#include <tlhelp32.h>

// Game-specific offsets
const uintptr_t _HasFocus = 0x906504;
const uintptr_t _InGarage = 0x907A20;
const uintptr_t _LoadState = 0x907B30;
const uintptr_t _GameState = 0x908540;

// Global variables
extern uintptr_t gBase;
extern bool IsOnFocus;
extern bool InGarage;
extern bool IsLoading;

// Global message variables
extern UINT msg;
extern WPARAM wParam;
extern LPARAM lParam;

// Function prototypes
HWND GetGameWindow();
LPDIRECT3DDEVICE9 GetGameD3DDevice();
void SetStatusMessage(const char* message);
void SaveSettings();
void LoadSettings();
void UpdateRadioInfo();
bool LoadCoverTexture(LPDIRECT3DDEVICE9 device, const std::string& url);
void ToggleRadio();
void RenderInterface();
void RenderSongTitle();
void Update();

// Helper functions
inline HWND GetProcessWindow() {
    return *(HWND*)(gBase + _HasFocus);
}

inline bool IsFocused() {
    return *(bool*)(gBase + _HasFocus);
}

inline bool IsInGarage() {
    return *(bool*)(gBase + _InGarage);
}

inline bool IsGameLoading() {
    return *(bool*)(gBase + _LoadState);
}

inline int GetGameState() {
    return *(int*)(gBase + _GameState);
}