#pragma once

#include <steam_api.h>

typedef void *(*CreateInterfaceFn)(const char *pName, int *pReturnCode);
ISteamClient *g_pSteamClient;
ISteamFriends *g_pSteamFriends;
ISteamUser *g_pSteamUser;
HSteamPipe g_hSteamPipe;
HSteamUser g_hSteamUser;

bool g_bSteamAPIInitialized = false;