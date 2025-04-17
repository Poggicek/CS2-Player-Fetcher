#include "main.h"
#include "leetify_provider.h"
#include "ui.h"
#include <algorithm>
#include <vector>
#include <windows.h>

typedef void *(*CreateInterfaceFn)(const char *pName, int *pReturnCode);
ISteamClient *g_pSteamClient;
ISteamFriends *g_pSteamFriends;
ISteamUser *g_pSteamUser;
HSteamPipe g_hSteamPipe;
HSteamUser g_hSteamUser;

bool g_bSteamAPIInitialized = false;

std::string GetSteamClientDllPath()
{
	HKEY hKey;
	LONG lRes = RegOpenKeyExA(HKEY_CURRENT_USER, R"(Software\Valve\Steam\ActiveProcess)", 0, KEY_READ, &hKey);

	if (lRes != ERROR_SUCCESS)
	{
		printf("Failed to open registry key\n");
		return "";
	}

	char value[1024];
	DWORD size = sizeof(value);
	lRes = RegQueryValueExA(hKey, "SteamClientDll64", nullptr, nullptr, (LPBYTE)value, &size);
	RegCloseKey(hKey);

	if (lRes != ERROR_SUCCESS)
	{
		printf("Failed to query registry key\n");
		return "";
	}

	return {value};
}

void CustomSteamAPIInit()
{
	auto steamClientDllPath = GetSteamClientDllPath();

	auto clientModule = LoadLibraryExA(steamClientDllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

	if (!clientModule)
	{
		printf("Failed to load steamclient64.dll\n");
		return;
	}

	auto createInterface = (CreateInterfaceFn)GetProcAddress(clientModule, "CreateInterface");

	g_pSteamClient = (ISteamClient *)createInterface("SteamClient021", nullptr);
	g_hSteamPipe = g_pSteamClient->CreateSteamPipe();
	g_hSteamUser = g_pSteamClient->ConnectToGlobalUser(g_hSteamPipe);
	g_pSteamFriends = g_pSteamClient->GetISteamFriends(g_hSteamUser, g_hSteamPipe, "SteamFriends017");
	g_pSteamUser = g_pSteamClient->GetISteamUser(g_hSteamUser, g_hSteamPipe, "SteamUser019");
	g_bSteamAPIInitialized = true;
}

void CustomSteamAPIShutdown()
{
	if (!g_bSteamAPIInitialized)
	{
		return;
	}

	g_pSteamClient->ReleaseUser(g_hSteamPipe, g_hSteamUser);
	g_pSteamClient->BReleaseSteamPipe(g_hSteamPipe);
	g_bSteamAPIInitialized = false;
}

BOOL WINAPI consoleHandler(DWORD signal)
{
	if (signal == CTRL_CLOSE_EVENT || signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT)
	{
		CustomSteamAPIShutdown();
		return true;
	}
	return false;
}

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(65001);
	SetConsoleCtrlHandler(consoleHandler, true);
	SetConsoleTitle("Leetify Stats");

	auto demoMode = false;

	for (int i = 1; i < argc; i++) 
	{
		if (strcmp(argv[i], "-demo") == 0) 
		{
			demoMode = true;
		}
	}

	CustomSteamAPIInit();

	auto mySteamID = g_pSteamUser->GetSteamID();
	auto iPlayers = g_pSteamFriends->GetCoplayFriendCount();

	std::vector<Player> players;

	for (int i = 0; i < iPlayers; ++i)
	{
		CSteamID playerSteamID = g_pSteamFriends->GetCoplayFriend(i);

		if (playerSteamID == mySteamID)
		{
			continue;
		}

		AppId_t app = g_pSteamFriends->GetFriendCoplayGame(playerSteamID);

		if (app != 730)
		{
			continue;
		}

		int iTimeStamp = g_pSteamFriends->GetFriendCoplayTime(playerSteamID);

		players.emplace_back(playerSteamID, iTimeStamp);
	}

	std::sort(players.begin(), players.end(), [](const Player &a, const Player &b) {
		if (a.time == b.time)
		{
			return a.steamID > b.steamID;
		}

		return a.time > b.time;
	});

	int iHighestTimeStamp;

	if (players.size() > 0)
	{
		iHighestTimeStamp = players[(std::min)(5, static_cast<int>(players.size()) - 1)].time;
		iHighestTimeStamp -= 300; // allow slack of 5 minutes because Steam is a bit inconsistent
	}

	std::erase_if(players, [iHighestTimeStamp](const Player &player) { return iHighestTimeStamp > player.time; });

	if (players.size() > 9)
	{
		players.erase(players.begin() + 9, players.end());
	}

	players.emplace_back(mySteamID, 0);

	if (demoMode)
	{
		auto now = time(NULL);

		// Just a list of some pro players to get a pretty screenshot
		players.clear();
		players.emplace_back(CSteamID(76561198074762801ul), now);
		players.emplace_back(CSteamID(76561198034202275ul), now);
		players.emplace_back(CSteamID(76561198134401925ul), now);
		players.emplace_back(CSteamID(76561198012872053ul), now);
		players.emplace_back(CSteamID(76561197982141573ul), now);
		players.emplace_back(CSteamID(76561198068002993ul), now);
		players.emplace_back(CSteamID(76561197991272318ul), now);
		players.emplace_back(CSteamID(76561197989744167ul), now);
		players.emplace_back(CSteamID(76561198113666193ul), now);
	}

	auto leetifyUsers = GetLeetifyUsers(players);

	Render(mySteamID, leetifyUsers);

	CustomSteamAPIShutdown();

	(void)(getchar());

	return 0;
}

std::string GetPersonaName(LeetifyUser user)
{
	auto playerName = std::string(g_pSteamFriends->GetFriendPersonaName(user.steamID));

	if (playerName.empty() || playerName == "[unknown]")
	{
		playerName = user.name;
	}

	if (playerName.empty())
	{
		playerName = std::to_string(user.steamID.ConvertToUint64());
	}

	return playerName;
}
