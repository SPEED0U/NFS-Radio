#include "framework.h"
#include "utils.h"
#include "GlobalVariables.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

// DirectX inclusions
#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

// ImGui includes
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Hook de la fenêtre originale
WNDPROC oWndProc = nullptr;

// Notre gestionnaire de messages Windows
LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Passer les messages à ImGui d'abord
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    // Si ImGui ne traite pas le message, le passer à la fenêtre originale
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

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

// BASS Audio - Définitions
typedef DWORD HSTREAM;
typedef DWORD HMUSIC;
typedef DWORD BASS_CHANNELINFO;
typedef DWORD DWORD_PTR;
typedef int MH_STATUS;

// Constantes BASS
#define BASS_SAMPLE_FLOAT 256
#define BASS_STREAM_PRESCAN 0x20000
#define BASS_STREAM_STATUS 0x800000
#define BASS_ATTRIB_VOL 2

// Pointeurs de fonctions BASS
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
// Variables de l'application
bool g_showMenu = false;
HSTREAM g_radioStream = 0;
std::string g_currentURL = "http://radio.nightriderz.world:8000/320/radio.mp3";
float g_volume = 1.0f;
bool g_isPlaying = false;
char g_urlBuffer[256] = "http://radio.nightriderz.world:8000/320/radio.mp3";
char g_statusMessage[256] = "";
float g_messageTimer = 0.0f;

// Hook pour EndScene
typedef HRESULT(APIENTRY* EndScene)(LPDIRECT3DDEVICE9);
EndScene oEndScene = nullptr;

// Fonction pour définir un message d'état
void SetStatusMessage(const char* message) {
    strcpy_s(g_statusMessage, message);
    g_messageTimer = 3.0f;
}

// Fonction pour jouer/arrêter le flux
void ToggleRadio() {
    if (!g_isPlaying) {
        SetStatusMessage("Connecting to radio...");

        if (g_radioStream) {
            BASS_StreamFree_Fn(g_radioStream);
            g_radioStream = 0;
        }

        g_radioStream = BASS_StreamCreateURL_Fn(g_currentURL.c_str(), 0, BASS_STREAM_PRESCAN | BASS_STREAM_STATUS, NULL, NULL);
        if (g_radioStream) {
            BASS_ChannelSetAttribute_Fn(g_radioStream, BASS_ATTRIB_VOL, g_volume);
            if (BASS_ChannelPlay_Fn(g_radioStream, FALSE)) {
                g_isPlaying = true;
                SetStatusMessage("Radio started!");
            }
            else {
                int error = BASS_ErrorGetCode_Fn();
                char msg[256];
                //sprintf_s(msg, "Erreur lecture: %d", error);
                SetStatusMessage(msg);
            }
        }
        else {
            int error = BASS_ErrorGetCode_Fn();
            char msg[256];
            //sprintf_s(msg, "Erreur création stream: %d", error);
            SetStatusMessage(msg);
        }
    }
    else {
        if (g_radioStream) {
            BASS_ChannelStop_Fn(g_radioStream);
            BASS_StreamFree_Fn(g_radioStream);
            g_radioStream = 0;
        }
        g_isPlaying = false;
        SetStatusMessage("Radio stopped");
    }
}

// Fonction pour dessiner l'interface
void RenderInterface() {
    if (!g_showMenu) return;

    ImGui::Begin("NFS: Radio", &g_showMenu);

    // URL du flux
    if (ImGui::InputText("Flux URL", g_urlBuffer, sizeof(g_urlBuffer))) {
        g_currentURL = g_urlBuffer;
    }

    // Bouton Play/Stop avec indicateur
    if (ImGui::Button(g_isPlaying ? "Stop" : "Play", ImVec2(100, 30))) {
        ToggleRadio();
    }
    ImGui::SameLine();
    ImGui::TextColored(g_isPlaying ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
        g_isPlaying ? "Playing" : "Stopped");

    // Contrôle du volume
    if (ImGui::SliderFloat("Volume", &g_volume, 0.0f, 1.0f, "%.1f")) {
        if (g_radioStream) {
            BASS_ChannelSetAttribute_Fn(g_radioStream, BASS_ATTRIB_VOL, g_volume);
        }
    }

    // Message d'état
    if (strlen(g_statusMessage) > 0 && g_messageTimer > 0) {
        ImGui::Separator();
        ImGui::TextWrapped(g_statusMessage);
        g_messageTimer -= ImGui::GetIO().DeltaTime;
        if (g_messageTimer <= 0) {
            g_statusMessage[0] = '\0';
        }
    }

    ImGui::End();
}
// Function hook EndScene
HRESULT APIENTRY hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool init = false;
    if (!init) {
        HWND gameWindow = GetForegroundWindow();
        ImGui::CreateContext();
        ImGui_ImplWin32_Init(gameWindow);
        ImGui_ImplDX9_Init(pDevice);

        // Installer le hook de la fenêtre
        oWndProc = (WNDPROC)SetWindowLongPtr(gameWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);

        init = true;
        ////MessageBoxA(nullptr, "ImGui initialisé", "Debug", MB_OK);
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RenderInterface();

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    return oEndScene(pDevice);
}

// Thread pour gérer les touches
DWORD WINAPI KeyboardThread(LPVOID lpParam) {
    //MessageBoxA(nullptr, "Thread clavier démarré", "Debug", MB_OK);

    while (true) {
        // Vérifier F7
        SHORT f7State = GetAsyncKeyState(VK_F7);
        if (f7State & 0x8000) {  // Si la touche est pressée
            g_showMenu = !g_showMenu;
            char msg[256];
            //sprintf_s(msg, "F7 pressé - Menu: %s", g_showMenu ? "activé" : "désactivé");
            //MessageBoxA(nullptr, msg, "Debug", MB_OK);
        }

        // Vérifier F8
        if (GetAsyncKeyState(VK_F8) & 0x8000) {
            //MessageBoxA(nullptr, "F8 pressé", "Debug", MB_OK);
            ToggleRadio();
        }

        Sleep(100);
    }
    return 0;
}
// Thread d'initialisation
DWORD WINAPI InitBassThread(LPVOID lpParam) {
    while (TheGameFlowManager != 3 || InGarage != true)
    {
        Sleep(1000);
    }

    // Obtenir le chemin de notre DLL
    char asiPath[MAX_PATH];
    GetModuleFileNameA((HMODULE)lpParam, asiPath, MAX_PATH);
    PathRemoveFileSpecA(asiPath);

    // Charger MinHook
    char minhookPath[MAX_PATH];
    sprintf_s(minhookPath, "%s\\MinHook.dll", asiPath);
    HMODULE hMinHook = LoadLibraryA(minhookPath);
    if (!hMinHook) {
        //MessageBoxA(nullptr, "Erreur chargement MinHook.DLL", "Error", MB_OK);
        return FALSE;
    }
    //MessageBoxA(nullptr, "MinHook.DLL chargé", "Debug", MB_OK);

    // Charger les fonctions MinHook
    MH_Initialize_Fn = (MH_Initialize_PTR)GetProcAddress(hMinHook, "MH_Initialize");
    MH_CreateHook_Fn = (MH_CreateHook_PTR)GetProcAddress(hMinHook, "MH_CreateHook");
    MH_EnableHook_Fn = (MH_EnableHook_PTR)GetProcAddress(hMinHook, "MH_EnableHook");
    MH_DisableHook_Fn = (MH_DisableHook_PTR)GetProcAddress(hMinHook, "MH_DisableHook");
    MH_Uninitialize_Fn = (MH_Uninitialize_PTR)GetProcAddress(hMinHook, "MH_Uninitialize");

    if (!MH_Initialize_Fn || !MH_CreateHook_Fn || !MH_EnableHook_Fn || !MH_DisableHook_Fn || !MH_Uninitialize_Fn) {
        //MessageBoxA(nullptr, "Erreur chargement fonctions MinHook", "Error", MB_OK);
        return FALSE;
    }

    // Initialiser MinHook avant toute autre chose
    if (MH_Initialize_Fn() != MH_OK) {
        //MessageBoxA(nullptr, "Erreur MH_Initialize", "Error", MB_OK);
        return FALSE;
    }
    //MessageBoxA(nullptr, "MinHook initialisé", "Debug", MB_OK);

    // Hook D3D9 EndScene
    void* d3d9Device[119];
    memset(d3d9Device, 0, sizeof(d3d9Device));

    //MessageBoxA(nullptr, "Attente avant récupération du D3D9Device...", "Debug", MB_OK);
    Sleep(5000); // Attendre 5 secondes
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!GetD3D9Device(d3d9Device, sizeof(d3d9Device))) {
            char msg[256];
            //sprintf_s(msg, "Tentative %d : Échec de GetD3D9Device", attempt + 1);
            //MessageBoxA(nullptr, msg, "Debug", MB_OK);
            Sleep(2000); // Attendre 2 secondes entre les tentatives
            continue;
        }

        void* endSceneAddr = d3d9Device[42];
        char addressMsg[256];
        sprintf_s(addressMsg, "Tentative %d : Adresse EndScene: %p", attempt + 1, endSceneAddr);
        //MessageBoxA(nullptr, addressMsg, "Debug", MB_OK);

        if (endSceneAddr == NULL) {
            //MessageBoxA(nullptr, "Adresse EndScene est NULL", "Error", MB_OK);
            continue;
        }

        DWORD oldProtect;
        if (!VirtualProtect(endSceneAddr, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Erreur VirtualProtect: %d", GetLastError());
            //MessageBoxA(nullptr, errorMsg, "Error", MB_OK);
            continue;
        }

        Sleep(1000); // Attendre 1 seconde avant de créer le hook

        MH_STATUS status = MH_CreateHook_Fn(endSceneAddr, &hkEndScene, reinterpret_cast<LPVOID*>(&oEndScene));
        if (status != MH_OK) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Tentative %d : Erreur création hook: %d", attempt + 1, status);
            //MessageBoxA(nullptr, errorMsg, "Error", MB_OK);
            VirtualProtect(endSceneAddr, sizeof(void*), oldProtect, &oldProtect);
            continue;
        }

        status = MH_EnableHook_Fn(endSceneAddr);
        if (status != MH_OK) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Tentative %d : Erreur EnableHook: %d", attempt + 1, status);
            //MessageBoxA(nullptr, errorMsg, "Error", MB_OK);
            VirtualProtect(endSceneAddr, sizeof(void*), oldProtect, &oldProtect);
            continue;
        }

        VirtualProtect(endSceneAddr, sizeof(void*), oldProtect, &oldProtect);
        //MessageBoxA(nullptr, "Hook activé avec succès!", "Success", MB_OK);

        // Charger BASS après que les hooks sont en place
        char bassPath[MAX_PATH];
        sprintf_s(bassPath, "%s\\bass.dll", asiPath);

        g_hBassDll = LoadLibraryA(bassPath);
        if (!g_hBassDll) {
            //MessageBoxA(nullptr, "Erreur chargement BASS.DLL", "Error", MB_OK);
            return FALSE;
        }
        //MessageBoxA(nullptr, "BASS.DLL chargé", "Debug", MB_OK);
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
            //MessageBoxA(nullptr, "Erreur chargement fonctions BASS", "Error", MB_OK);
            return FALSE;
        }
        //MessageBoxA(nullptr, "Fonctions BASS chargées", "Debug", MB_OK);

        // Initialiser BASS
        if (!BASS_Init_Fn(-1, 44100, 0, GetForegroundWindow(), NULL)) {
            int error = BASS_ErrorGetCode_Fn();
            char msg[256];
            //sprintf_s(msg, "Erreur BASS_Init: %d", error);
            //MessageBoxA(nullptr, msg, "Error", MB_OK);
            return FALSE;
        }
        //MessageBoxA(nullptr, "BASS initialisé", "Debug", MB_OK);

        // Créer le thread du clavier
        CreateThread(NULL, 0, KeyboardThread, NULL, 0, NULL);
        //MessageBoxA(nullptr, "Thread clavier créé\nF7: Menu\nF8: Play/Stop", "Info", MB_OK);

        return TRUE;
    }

    //MessageBoxA(nullptr, "Échec après 3 tentatives", "Error", MB_OK);
    MH_Uninitialize_Fn();
    return FALSE;
}

void Update(LPVOID)
{
    while (true)
    {
        IsOnFocus = (*(bool*)(gBase + _HasFocus));
        InGarage = (*(bool*)(gBase + _InGarage));
        IsLoading = (*(bool*)(gBase + _LoadState));
        TheGameFlowManager = (*(int*)(gBase + _GameState));
        Sleep(16.6666);
    }
}

// Point d'entrée DLL
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        gBase = (uintptr_t)GetModuleHandleA(NULL);
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)(gBase);
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(gBase + dos->e_lfanew);
        if ((gBase + nt->OptionalHeader.AddressOfEntryPoint + (0x400000 - gBase)) == 0x97CEFC) // Check if .exe file is compatible
        {
            CreateThread(NULL, 0, InitBassThread, hModule, 0, NULL);
            CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&Update, NULL, 0, NULL);
            break;
        }
        else
        {
            //MessageBoxA(NULL, "This .exe is not supported.\nPlease use v1.9.3 nfsw.exe (10,9 MB (11.452.160 bytes)).", "WorldWhineGen", MB_ICONERROR);
            return FALSE;
        }
    }
    case DLL_PROCESS_DETACH: {
        if (g_radioStream && BASS_ChannelStop_Fn) {
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
        break;
    }
    }
    return TRUE;
}