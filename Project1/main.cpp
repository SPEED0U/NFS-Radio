﻿#include "framework.h"
#include "utils.h"
#include "GlobalVariables.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <string>
#include <vector>
#include <fstream>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// DirectX inclusions
#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

// ImGui includes
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// Bass typedefs and defines
typedef DWORD HSTREAM;
#define BASS_SAMPLE_FLOAT 256
#define BASS_STREAM_PRESCAN 0x20000
#define BASS_STREAM_STATUS 0x800000
#define BASS_ATTRIB_VOL 2

// Structure pour les stations radio
struct RadioStation {
    std::string name;
    std::string url;
    std::string currentSong;
    std::string coverUrl;
};

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Variables globales pour la radio
std::vector<RadioStation> g_radioStations = {
    {
        "NFS: Radio",
        "http://radio.nightriderz.world:8000/320/radio.mp3",
        "",
        ""
    },
    {
        "Horizon Radio",
        "http://radio.nightriderz.world:8010/320/radio.mp3",
        "",
        ""
    }
};

RadioStation g_currentStation = g_radioStations[0];
bool g_autoPlay = false;
bool g_needCoverUpdate = false;

// Variables pour la texture de la couverture
LPDIRECT3DTEXTURE9 g_coverTexture = nullptr;
int g_coverTextureWidth = 0;
int g_coverTextureHeight = 0;
std::string g_currentCoverUrl = "";

// Hook de la fenêtre originale
WNDPROC oWndProc = nullptr;

// Variables globales
bool g_showMenu = false;
HSTREAM g_radioStream = 0;
float g_volume = 1.0f;
bool g_isPlaying = false;
char g_statusMessage[256] = "";
float g_messageTimer = 0.0f;
float g_titleDisplayTimer = 0.0f;
std::string g_lastDisplayedSong = "";
bool g_shouldDisplayTitle = false;
float g_titleAlpha = 0.0f;          // Pour l'animation de transition
float g_titleYOffset = -50.0f;      // Position Y pour l'animation
const float FADE_IN_TIME = 0.5f;    // Durée de l'apparition en secondes
const float FADE_OUT_TIME = 1.0f;   // Durée de la disparition en secondes
const float DISPLAY_TIME = 15.0f;   // Temps total d'affichage

// MinHook
#define MH_OK 0
#define MH_ALL_HOOKS (LPVOID)(-1)

// Pointeurs de fonctions MinHook
typedef int (WINAPI* MH_Initialize_PTR)(void);
typedef int (WINAPI* MH_CreateHook_PTR)(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
typedef int (WINAPI* MH_EnableHook_PTR)(LPVOID pTarget);
typedef int (WINAPI* MH_DisableHook_PTR)(LPVOID pTarget);
typedef int (WINAPI* MH_Uninitialize_PTR)(void);

// Variables globales MinHook
MH_Initialize_PTR MH_Initialize_Fn = NULL;
MH_CreateHook_PTR MH_CreateHook_Fn = NULL;
MH_EnableHook_PTR MH_EnableHook_Fn = NULL;
MH_DisableHook_PTR MH_DisableHook_Fn = NULL;
MH_Uninitialize_PTR MH_Uninitialize_Fn = NULL;

// BASS Audio Function Types
typedef BOOL(WINAPI* BASS_Init_PTR)(int device, DWORD freq, DWORD flags, HWND win, const void* dsguid);
typedef void (WINAPI* BASS_Free_PTR)(void);
typedef int (WINAPI* BASS_ErrorGetCode_PTR)(void);
typedef HSTREAM(WINAPI* BASS_StreamCreateURL_PTR)(const char* url, DWORD offset, DWORD flags, void* proc, void* user);
typedef BOOL(WINAPI* BASS_ChannelPlay_PTR)(DWORD handle, BOOL restart);
typedef BOOL(WINAPI* BASS_ChannelStop_PTR)(DWORD handle);
typedef BOOL(WINAPI* BASS_ChannelSetAttribute_PTR)(DWORD handle, DWORD attrib, float value);
typedef BOOL(WINAPI* BASS_StreamFree_PTR)(HSTREAM handle);

// Variables globales BASS
HMODULE g_hBassDll = NULL;
BASS_Init_PTR BASS_Init_Fn = NULL;
BASS_Free_PTR BASS_Free_Fn = NULL;
BASS_ErrorGetCode_PTR BASS_ErrorGetCode_Fn = NULL;
BASS_StreamCreateURL_PTR BASS_StreamCreateURL_Fn = NULL;
BASS_ChannelPlay_PTR BASS_ChannelPlay_Fn = NULL;
BASS_ChannelStop_PTR BASS_ChannelStop_Fn = NULL;
BASS_ChannelSetAttribute_PTR BASS_ChannelSetAttribute_Fn = NULL;
BASS_StreamFree_PTR BASS_StreamFree_Fn = NULL;


// Hook pour EndScene
typedef HRESULT(APIENTRY* EndScene)(LPDIRECT3DDEVICE9);
EndScene oEndScene = nullptr;

// Prototypes de fonctions
void SetStatusMessage(const char* message);
void SaveSettings();
void LoadSettings();
void UpdateRadioInfo();
bool LoadCoverTexture(LPDIRECT3DDEVICE9 device, const std::string& url);
void ToggleRadio();
void RenderSongTitle();

// Implémentation des fonctions
void SetStatusMessage(const char* message) {
    strcpy_s(g_statusMessage, message);
    g_messageTimer = 3.0f;
}

bool LoadCoverTexture(LPDIRECT3DDEVICE9 device, const std::string& url) {
    if (url.empty() || url == g_currentCoverUrl) return false;

    if (g_coverTexture) {
        g_coverTexture->Release();
        g_coverTexture = nullptr;
    }

    HINTERNET hSession = WinHttpOpen(L"NFS Radio/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) return false;

    // Convert URL to wide string for WinHTTP
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, NULL, 0);
    std::vector<wchar_t> wideUrl(wideLength);
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wideUrl.data(), wideLength);

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t whost[256] = { 0 };
    wchar_t wpath[1024] = { 0 };
    urlComp.lpszHostName = whost;
    urlComp.dwHostNameLength = sizeof(whost) / sizeof(whost[0]);
    urlComp.lpszUrlPath = wpath;
    urlComp.dwUrlPathLength = sizeof(wpath) / sizeof(wpath[0]);

    if (!WinHttpCrackUrl(wideUrl.data(), 0, 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, whost, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
        L"GET", wpath,
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, NULL)) {

        std::vector<BYTE> imageData;
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;

        do {
            dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);

            if (dwSize > 0) {
                std::vector<BYTE> buffer(dwSize);

                if (WinHttpReadData(hRequest, buffer.data(),
                    dwSize, &dwDownloaded)) {
                    imageData.insert(imageData.end(),
                        buffer.begin(), buffer.begin() + dwDownloaded);
                }
            }
        } while (dwSize > 0);

        // Créer la texture
        if (!imageData.empty()) {
            D3DFORMAT format = D3DFMT_A8R8G8B8;
            UINT width = 256;
            UINT height = 256;

            HRESULT hr = device->CreateTexture(
                width, height,
                1,
                0,
                format,
                D3DPOOL_MANAGED,
                &g_coverTexture,
                NULL
            );

            if (SUCCEEDED(hr)) {
                D3DLOCKED_RECT lockedRect;
                if (SUCCEEDED(g_coverTexture->LockRect(0, &lockedRect, NULL, 0))) {
                    // Remplir la texture avec une couleur par défaut
                    DWORD* pixels = (DWORD*)lockedRect.pBits;
                    for (UINT i = 0; i < width * height; i++) {
                        pixels[i] = 0xFF808080; // Gris
                    }

                    g_coverTexture->UnlockRect(0);
                    g_coverTextureWidth = width;
                    g_coverTextureHeight = height;
                    g_currentCoverUrl = url;
                    return true;
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
}

void UpdateRadioInfo() {
    HINTERNET hSession = WinHttpOpen(L"NFS Radio/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) return;

    HINTERNET hConnect = WinHttpConnect(hSession,
        L"radio.nightriderz.world", INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect,
            L"GET", L"/api/nowplaying/nfs",
            NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);

        if (hRequest) {
            if (WinHttpSendRequest(hRequest,
                WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, NULL)) {

                std::string response;
                DWORD dwSize = 0;
                DWORD dwDownloaded = 0;
                char* pszOutBuffer;

                do {
                    dwSize = 0;
                    WinHttpQueryDataAvailable(hRequest, &dwSize);

                    if (dwSize > 0) {
                        pszOutBuffer = new char[dwSize + 1];

                        if (WinHttpReadData(hRequest, pszOutBuffer,
                            dwSize, &dwDownloaded)) {
                            pszOutBuffer[dwDownloaded] = '\0';
                            response += pszOutBuffer;
                        }

                        delete[] pszOutBuffer;
                    }
                } while (dwSize > 0);

                // Parser manuellement le JSON
                size_t songPos = response.find("\"text\":\"");
                size_t artPos = response.find("\"art\":\"");

                if (songPos != std::string::npos && artPos != std::string::npos) {
                    songPos += 8;
                    artPos += 7;

                    size_t songEnd = response.find("\"", songPos);
                    size_t artEnd = response.find("\"", artPos);

                    if (songEnd != std::string::npos && artEnd != std::string::npos) {
                        std::string newSong = response.substr(songPos, songEnd - songPos);
                        std::string newCoverUrl = response.substr(artPos, artEnd - artPos);

                        // Vérifier si le titre a changé
                        if (newSong != g_currentStation.currentSong) {
                            g_currentStation.currentSong = newSong;
                            g_shouldDisplayTitle = true;
                            g_titleDisplayTimer = DISPLAY_TIME;  // DISPLAY_TIME au lieu de 15.0f
                            g_titleAlpha = 0.0f;           // Démarrer avec une transparence totale
                            g_titleYOffset = -50.0f;       // Démarrer hors écran
                        }

                        if (newCoverUrl != g_currentStation.coverUrl) {
                            g_currentStation.coverUrl = newCoverUrl;
                            g_needCoverUpdate = true;
                        }
                    }
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
}

void SaveSettings() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    PathRemoveFileSpecA(path);
    strcat_s(path, "\\scripts\\Radio\\settings.ini");

    FILE* file;
    if (fopen_s(&file, path, "w") == 0) {
        fprintf(file, "volume=%.2f\n", g_volume * 100.0f);
        fprintf(file, "autoplay=%d\n", g_isPlaying ? 1 : 0);
        fprintf(file, "station=%s\n", g_currentStation.url.c_str());
        fclose(file);
    }
}

void LoadSettings() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    PathRemoveFileSpecA(path);
    strcat_s(path, "\\scripts\\Radio\\settings.ini");

    FILE* file;
    if (fopen_s(&file, path, "r") == 0) {
        char line[512];
        while (fgets(line, sizeof(line), file)) {
            char* value = strchr(line, '=');
            if (value) {
                *value = '\0';
                value++;

                if (strcmp(line, "volume") == 0) {
                    g_volume = static_cast<float>(atof(value)) / 100.0f; // Si la valeur est stockée en pourcentage
                }
                else if (strcmp(line, "autoplay") == 0) {
                    g_autoPlay = (atoi(value) == 1);
                }
                else if (strcmp(line, "station") == 0) {
                    char* newline = strchr(value, '\n');
                    if (newline) *newline = '\0';
                    // Trouver la station correspondante
                    for (const auto& station : g_radioStations) {
                        if (station.url == value) {
                            g_currentStation = station;
                            break;
                        }
                    }
                }
            }
        }
        fclose(file);

        if (g_autoPlay) {
            ToggleRadio();
        }
    }
}

void ToggleRadio() {
    if (!g_isPlaying) {
        SetStatusMessage("Connecting to radio...");

        if (g_radioStream != 0) {
            BASS_StreamFree_Fn(g_radioStream);
            g_radioStream = 0;
        }

        g_radioStream = BASS_StreamCreateURL_Fn(g_currentStation.url.c_str(), 0,
            static_cast<DWORD>(BASS_STREAM_PRESCAN | BASS_STREAM_STATUS), NULL, NULL);

        if (g_radioStream != 0) {
            BASS_ChannelSetAttribute_Fn(g_radioStream, BASS_ATTRIB_VOL, g_volume);
            if (BASS_ChannelPlay_Fn(g_radioStream, FALSE)) {
                g_isPlaying = true;
                SetStatusMessage("Radio started!");
                SaveSettings();
            }
            else {
                char errorMsg[256];
                sprintf_s(errorMsg, "Play error: %d", BASS_ErrorGetCode_Fn());
                SetStatusMessage(errorMsg);
            }
        }
        else {
            char errorMsg[256];
            sprintf_s(errorMsg, "Stream creation error: %d", BASS_ErrorGetCode_Fn());
            SetStatusMessage(errorMsg);
        }
    }
    else {
        if (g_radioStream != 0) {
            BASS_ChannelStop_Fn(g_radioStream);
            BASS_StreamFree_Fn(g_radioStream);
            g_radioStream = 0;
        }
        g_isPlaying = false;
        SaveSettings();
        SetStatusMessage("Radio stopped");
    }
}

// Notre gestionnaire de messages Windows
LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

// Fonction pour dessiner l'interface
void RenderInterface() {
    if (!g_showMenu) return;

    // Style du menu
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15, 15));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));

    // Couleurs du menu
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.09f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.4f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.15f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.27f, 0.27f, 0.33f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.5f, 0.5f, 0.5f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.6f, 0.6f, 0.6f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.13f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.15f, 0.15f, 0.18f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.19f, 0.19f, 0.22f, 0.94f));

    // Fenêtre principale
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("NightRiderz Radio", &g_showMenu,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);

    // En-tête avec les contrôles
    ImGui::BeginChild("Header", ImVec2(0, 30), true);
    ImGui::Text("Controls:"); ImGui::SameLine();
    ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "F7 - Toggle Menu  |  F8 - Play/Pause");
    ImGui::EndChild();
    ImGui::Spacing();

    // Section de la station
    ImGui::BeginChild("Station", ImVec2(0, 120), true);

    if (ImGui::BeginCombo("##StationSelector", g_currentStation.name.c_str())) {
        for (size_t i = 0; i < g_radioStations.size(); i++) {
            const bool is_selected = (g_currentStation.name == g_radioStations[i].name);
            if (ImGui::Selectable(g_radioStations[i].name.c_str(), is_selected)) {
                bool wasPlaying = g_isPlaying;
                if (wasPlaying) {
                    ToggleRadio(); // Stop current
                }
                g_currentStation = g_radioStations[i];
                if (wasPlaying) {
                    ToggleRadio(); // Start new
                }
                SaveSettings();
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Layout horizontal pour la couverture et les infos
    if (g_coverTexture) {
        float displayHeight = 90.0f;
        float aspectRatio = static_cast<float>(g_coverTextureWidth) / g_coverTextureHeight;
        float displayWidth = displayHeight * aspectRatio;

        ImGui::Image((ImTextureID)(uintptr_t)g_coverTexture, ImVec2(displayWidth, displayHeight));
        ImGui::SameLine();
    }

    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Current Station:");
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "%s", g_currentStation.name.c_str());

    if (!g_currentStation.currentSong.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Now Playing:");
        ImGui::TextWrapped("%s", g_currentStation.currentSong.c_str());
    }
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::Spacing();

    // Section des contrôles
    ImGui::BeginChild("Controls", ImVec2(0, 100), true);

    // Bouton Play/Stop stylisé
    ImVec2 buttonSize(80, 30);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonSize.x) * 0.5f);
    if (ImGui::Button(g_isPlaying ? "Stop" : "Play", buttonSize)) {
        ToggleRadio();
    }

    // Status stylisé
    ImVec4 statusColor = g_isPlaying ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(g_isPlaying ? "Playing" : "Stopped").x) * 0.5f);
    ImGui::TextColored(statusColor, g_isPlaying ? "Playing" : "Stopped");

    // Slider de volume avec label
    ImGui::Spacing();
    ImGui::PushItemWidth(ImGui::GetWindowWidth() - 30);

    // Convertir le volume de 0-1 en 0-100 pour l'affichage
    float displayVolume = g_volume * 100.0f;
    if (ImGui::SliderFloat("##Volume", &displayVolume, 0.0f, 100.0f, "Volume: %.0f%%")) {
        // Reconvertir de 0-100 en 0-1 pour BASS
        g_volume = displayVolume / 100.0f;
        if (g_radioStream != 0) {
            BASS_ChannelSetAttribute_Fn(g_radioStream, BASS_ATTRIB_VOL, g_volume);
            SaveSettings();
        }
    }
    ImGui::PopItemWidth();

    ImGui::EndChild();

    // Messages d'état
    if (strlen(g_statusMessage) > 0 && g_messageTimer > 0) {
        ImGui::Spacing();
        ImGui::BeginChild("Status", ImVec2(0, 30), true);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, std::min(g_messageTimer / 1.0f, 1.0f)), g_statusMessage);
        g_messageTimer -= ImGui::GetIO().DeltaTime;
        ImGui::EndChild();
    }

    ImGui::End();

    // Pop des styles
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(10);
}

void RenderSongTitle() {
    if (!g_isPlaying || g_currentStation.currentSong.empty()) return;

    // Mettre à jour les animations
    float deltaTime = ImGui::GetIO().DeltaTime;

    if (g_shouldDisplayTitle) {
        // Animation d'apparition
        if (g_titleAlpha < 1.0f) {
            g_titleAlpha += deltaTime / FADE_IN_TIME;
            if (g_titleAlpha > 1.0f) g_titleAlpha = 1.0f;

            g_titleYOffset += deltaTime * (0.0f - g_titleYOffset) * 5.0f;
        }

        // Mise à jour du timer
        g_titleDisplayTimer -= deltaTime;
        if (g_titleDisplayTimer <= 0.0f) {
            g_shouldDisplayTitle = false;
        }
    }
    else {
        // Animation de disparition
        g_titleAlpha -= deltaTime / FADE_OUT_TIME;
        if (g_titleAlpha <= 0.0f) {
            g_titleAlpha = 0.0f;
            g_titleYOffset = -50.0f;
            return;
        }

        g_titleYOffset -= deltaTime * 50.0f;
    }

    if (g_titleAlpha <= 0.0f) return;

    // Window avec le titre complet
    ImGui::SetNextWindowPos(ImVec2(10, 10 + g_titleYOffset));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.8f * g_titleAlpha));

    ImGui::Begin("NowPlaying", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_AlwaysAutoResize);

    // "Now Playing" text
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextColored(ImVec4(1, 1, 1, g_titleAlpha), "NOW PLAYING");
    ImGui::SetWindowFontScale(1.0f);

    // Song title with shadow
    ImVec2 pos = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(pos.x + 1, pos.y + 1));
    ImGui::TextColored(ImVec4(0, 0, 0, g_titleAlpha), "%s", g_currentStation.currentSong.c_str());
    ImGui::SetCursorPos(pos);
    ImGui::TextColored(ImVec4(1, 1, 1, g_titleAlpha), "%s", g_currentStation.currentSong.c_str());

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// Thread pour la mise à jour des informations radio
DWORD WINAPI RadioUpdateThread(LPVOID lpParam) {
    while (true) {
        if (g_isPlaying) {
            UpdateRadioInfo();
        }
        Sleep(5000);
    }
    return 0;
}

// Thread pour gérer les touches
DWORD WINAPI KeyboardThread(LPVOID lpParam) {
    while (true) {
        SHORT f7State = GetAsyncKeyState(VK_F7);
        if (f7State & 0x8000) {
            g_showMenu = !g_showMenu;
            Sleep(200);
        }

        if (GetAsyncKeyState(VK_F8) & 0x8000) {
            ToggleRadio();
            Sleep(200);
        }

        Sleep(10);
    }
    return 0;
}

// Function hook EndScene
HRESULT APIENTRY hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool init = false;
    if (!init) {
        HWND gameWindow = GetForegroundWindow();
        ImGui::CreateContext();
        ImGui_ImplWin32_Init(gameWindow);
        ImGui_ImplDX9_Init(pDevice);

        oWndProc = (WNDPROC)SetWindowLongPtr(gameWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);

        init = true;
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Dessiner l'interface principale si le menu est ouvert
    if (g_showMenu) {
        RenderInterface();
    }

    // Toujours dessiner le titre de la chanson si la radio est active
    RenderSongTitle();

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    return oEndScene(pDevice);
}

// Thread d'initialisation
DWORD WINAPI InitBassThread(LPVOID lpParam) {
    Sleep(15000);
    while (TheGameFlowManager != 6) {
        Sleep(1000);
    }

    char asiPath[MAX_PATH];
    GetModuleFileNameA((HMODULE)lpParam, asiPath, MAX_PATH);
    PathRemoveFileSpecA(asiPath);

    // Charger MinHook
    char minhookPath[MAX_PATH];
    sprintf_s(minhookPath, "%s\\Radio\\MinHook.dll", asiPath);
    HMODULE hMinHook = LoadLibraryA(minhookPath);
    if (!hMinHook) return FALSE;

    // Charger les fonctions MinHook
    MH_Initialize_Fn = (MH_Initialize_PTR)GetProcAddress(hMinHook, "MH_Initialize");
    MH_CreateHook_Fn = (MH_CreateHook_PTR)GetProcAddress(hMinHook, "MH_CreateHook");
    MH_EnableHook_Fn = (MH_EnableHook_PTR)GetProcAddress(hMinHook, "MH_EnableHook");
    MH_DisableHook_Fn = (MH_DisableHook_PTR)GetProcAddress(hMinHook, "MH_DisableHook");
    MH_Uninitialize_Fn = (MH_Uninitialize_PTR)GetProcAddress(hMinHook, "MH_Uninitialize");

    if (!MH_Initialize_Fn || !MH_CreateHook_Fn || !MH_EnableHook_Fn ||
        !MH_DisableHook_Fn || !MH_Uninitialize_Fn) return FALSE;

    if (MH_Initialize_Fn() != MH_OK) return FALSE;

    // Hook D3D9 EndScene
    void* d3d9Device[119];
    memset(d3d9Device, 0, sizeof(d3d9Device));

    Sleep(5000);
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!GetD3D9Device(d3d9Device, sizeof(d3d9Device))) {
            Sleep(2000);
            continue;
        }

        void* endSceneAddr = d3d9Device[42];
        if (endSceneAddr == NULL) continue;

        DWORD oldProtect;
        if (!VirtualProtect(endSceneAddr, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            continue;
        }

        Sleep(1000);

        if (MH_CreateHook_Fn(endSceneAddr, &hkEndScene, reinterpret_cast<LPVOID*>(&oEndScene)) != MH_OK) {
            VirtualProtect(endSceneAddr, sizeof(void*), oldProtect, &oldProtect);
            continue;
        }

        if (MH_EnableHook_Fn(endSceneAddr) != MH_OK) {
            VirtualProtect(endSceneAddr, sizeof(void*), oldProtect, &oldProtect);
            continue;
        }

        VirtualProtect(endSceneAddr, sizeof(void*), oldProtect, &oldProtect);

        // Charger BASS
        char bassPath[MAX_PATH];
        sprintf_s(bassPath, "%s\\Radio\\bass.dll", asiPath);

        g_hBassDll = LoadLibraryA(bassPath);
        if (!g_hBassDll) return FALSE;

        // Charger les fonctions BASS
        BASS_Init_Fn = (BASS_Init_PTR)GetProcAddress(g_hBassDll, "BASS_Init");
        BASS_Free_Fn = (BASS_Free_PTR)GetProcAddress(g_hBassDll, "BASS_Free");
        BASS_ErrorGetCode_Fn = (BASS_ErrorGetCode_PTR)GetProcAddress(g_hBassDll, "BASS_ErrorGetCode");
        BASS_StreamCreateURL_Fn = (BASS_StreamCreateURL_PTR)GetProcAddress(g_hBassDll, "BASS_StreamCreateURL");
        BASS_ChannelPlay_Fn = (BASS_ChannelPlay_PTR)GetProcAddress(g_hBassDll, "BASS_ChannelPlay");
        BASS_ChannelStop_Fn = (BASS_ChannelStop_PTR)GetProcAddress(g_hBassDll, "BASS_ChannelStop");
        BASS_ChannelSetAttribute_Fn = (BASS_ChannelSetAttribute_PTR)GetProcAddress(g_hBassDll, "BASS_ChannelSetAttribute");
        BASS_StreamFree_Fn = (BASS_StreamFree_PTR)GetProcAddress(g_hBassDll, "BASS_StreamFree");

        if (!BASS_Init_Fn || !BASS_Free_Fn || !BASS_ErrorGetCode_Fn || !BASS_StreamCreateURL_Fn ||
            !BASS_ChannelPlay_Fn || !BASS_ChannelStop_Fn || !BASS_ChannelSetAttribute_Fn || !BASS_StreamFree_Fn) {
            return FALSE;
        }

        // Initialiser BASS
        if (!BASS_Init_Fn(-1, 44100, 0, GetForegroundWindow(), NULL)) {
            return FALSE;
        }

        // Charger les paramètres
        LoadSettings();

        // Créer les threads
        CreateThread(NULL, 0, KeyboardThread, NULL, 0, NULL);
        CreateThread(NULL, 0, RadioUpdateThread, NULL, 0, NULL);

        return TRUE;
    }

    MH_Uninitialize_Fn();
    return FALSE;
}

void Update(LPVOID) {
    static bool lastFocusState = false;

    while (true) {
        IsOnFocus = (*(bool*)(gBase + _HasFocus));
        InGarage = (*(bool*)(gBase + _InGarage));
        IsLoading = (*(bool*)(gBase + _LoadState));
        TheGameFlowManager = (*(int*)(gBase + _GameState));

        // Gérer la lecture de la radio en fonction du focus
        if (g_radioStream != 0 && g_isPlaying) {
            if (IsOnFocus && !lastFocusState) {
                // La fenêtre vient d'obtenir le focus, reprendre la lecture
                BASS_ChannelPlay_Fn(g_radioStream, FALSE);
            }
            else if (!IsOnFocus && lastFocusState) {
                // La fenêtre vient de perdre le focus, mettre en pause
                BASS_ChannelStop_Fn(g_radioStream);
            }
        }
        lastFocusState = IsOnFocus;

        Sleep(17);
    }
}

// Point d'entrée DLL
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        gBase = (uintptr_t)GetModuleHandleA(NULL);
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)(gBase);
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(gBase + dos->e_lfanew);
        if ((gBase + nt->OptionalHeader.AddressOfEntryPoint + (0x400000 - gBase)) == 0x97CEFC) {
            CreateThread(NULL, 0, InitBassThread, hModule, 0, NULL);
            CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&Update, NULL, 0, NULL);
            break;
        }
        else {
            return FALSE;
        }
    }
    case DLL_PROCESS_DETACH: {
        if (g_radioStream != 0 && BASS_ChannelStop_Fn) {
            BASS_ChannelStop_Fn(g_radioStream);
            if (BASS_StreamFree_Fn) {
                BASS_StreamFree_Fn(g_radioStream);
            }
        }
        if (BASS_Free_Fn) {
            BASS_Free_Fn();
        }
        if (g_hBassDll) {
            FreeLibrary(g_hBassDll);
        }
        if (MH_DisableHook_Fn) {
            MH_DisableHook_Fn(MH_ALL_HOOKS);
        }
        if (MH_Uninitialize_Fn) {
            MH_Uninitialize_Fn();
        }
        if (g_coverTexture) {
            g_coverTexture->Release();
            g_coverTexture = nullptr;
        }
        break;
    }
    }
    return TRUE;
}