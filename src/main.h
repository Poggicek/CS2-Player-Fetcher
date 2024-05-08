
#include "steam_api.h"

struct Player
{
	CSteamID playerSteamID;
	int time;

	Player(CSteamID playerSteamID, int iTimeStamp)
		: playerSteamID(playerSteamID), time(iTimeStamp)
	{}
};

typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);
ISteamClient* g_pSteamClient;
ISteamFriends* g_pSteamFriends;
ISteamUser* g_pSteamUser;
HSteamPipe g_hSteamPipe;
HSteamUser g_hSteamUser;

bool g_bSteamAPIInitialized = false;