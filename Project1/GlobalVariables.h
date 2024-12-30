#pragma once

#include "stdio.h"
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <future>
#include <chrono>

// Global variables
#define _InGarage 0x08E3614
#define _HasFocus 0x0882E41
#define _IsPaused 0x00874E58
#define _EngineVolSlider 0x904D80
#define _MasterVolSlider 0x904D98
#define _LoadState 0x08ECDC4
#define _GameState 0x08FC730

uintptr_t gBase;
bool IsPaused = false, IsOnFocus = true, InGarage = true, IsLoading = false;
DWORD TheGameFlowManager = 0;