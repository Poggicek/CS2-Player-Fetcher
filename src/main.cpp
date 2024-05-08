#include <iostream>
#include <vector>
#include <algorithm>
#include <windows.h>
#include <filesystem>

#include "main.h"

std::string GetSteamClientDllPath()
{
	HKEY hKey;
	LONG lRes = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\ActiveProcess", 0, KEY_READ, &hKey);

	if (lRes != ERROR_SUCCESS)
	{
		printf("Failed to open registry key\n");
		return "";
	}

	char value[1024];
	DWORD size = sizeof(value);
	lRes = RegQueryValueExA(hKey, "SteamClientDll64", nullptr, nullptr, (LPBYTE)value, &size);

	if (lRes != ERROR_SUCCESS)
	{
		printf("Failed to query registry key\n");
		return "";
	}

	RegCloseKey(hKey);
	return std::string(value);
}

void CustomSteamAPIInit()
{
	auto steamClientDllPath = GetSteamClientDllPath();

	auto clientModule = LoadLibraryExA(steamClientDllPath.c_str(), 0, 8);

	if (!clientModule)
	{
		printf("Failed to load steamclient64.dll\n");
		return;
	}

	auto createInterface = (CreateInterfaceFn)GetProcAddress(clientModule, "CreateInterface");

	g_pSteamClient = (ISteamClient*)createInterface("SteamClient021", nullptr);
	g_hSteamPipe = g_pSteamClient->CreateSteamPipe();
	g_hSteamUser = g_pSteamClient->ConnectToGlobalUser(g_hSteamPipe);
	g_pSteamFriends = g_pSteamClient->GetISteamFriends(g_hSteamUser, g_hSteamPipe, "SteamFriends017");
	g_pSteamUser = g_pSteamClient->GetISteamUser(g_hSteamUser, g_hSteamPipe, "SteamUser019");
}

void CustomSteamAPIShutdown()
{
	g_pSteamClient->ReleaseUser(g_hSteamPipe, g_hSteamUser);
	g_pSteamClient->BReleaseSteamPipe(g_hSteamPipe);
}

int main()
{
	SetConsoleOutputCP(65001);
	CustomSteamAPIInit();

	auto nPlayers = g_pSteamFriends->GetCoplayFriendCount();

	std::vector<Player> players;

	for (int i = 0; i < nPlayers; ++i)
	{
		CSteamID playerSteamID = g_pSteamFriends->GetCoplayFriend(i);
		int iTimeStamp = g_pSteamFriends->GetFriendCoplayTime(playerSteamID);
		AppId_t app = g_pSteamFriends->GetFriendCoplayGame(playerSteamID);

		if (playerSteamID == g_pSteamUser->GetSteamID())
			continue;

		if (app != 730)
			continue;

		static auto now = std::chrono::system_clock::now();
		auto time = std::chrono::system_clock::from_time_t(iTimeStamp);

		if (std::chrono::duration_cast<std::chrono::minutes>(now - time).count() > 30)
			continue;

		players.emplace_back(playerSteamID, iTimeStamp);
	}

	std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) { return a.time > b.time; });

	for (int i = 0; i < min(9, (int)players.size()); ++i)
	{
		auto& player = players[i];
		const char* playerName = g_pSteamFriends->GetFriendPersonaName(player.playerSteamID);

		printf("https://leetify.com/app/profile/%lld | %s\n", player.playerSteamID.ConvertToUint64(), playerName);
	}

	printf("Open links in browser (Y/n) ");

	int input = getchar();
	if (input == 'n' || input == 'N')
	{
		CustomSteamAPIShutdown();
		return 0;
	}

	for (int i = 0; i < min(9, (int)players.size()); ++i)
	{
		auto& player = players[i];
		std::string url = "https://leetify.com/app/profile/" + std::to_string(player.playerSteamID.ConvertToUint64());
		ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	CustomSteamAPIShutdown();

	return 0;
}