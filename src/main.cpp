#include <iostream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <thread>
#include <windows.h>
#include <tabulate/table.hpp>

#include "main.h"
#include "leetify_provider.h"

using namespace tabulate;

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
	g_bSteamAPIInitialized = true;
}

void CustomSteamAPIShutdown()
{
	if(!g_bSteamAPIInitialized)
		return;

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

std::string roundTo(float value, int decimalPlaces)
{
	std::stringstream stream;
	stream << std::fixed << std::setprecision(decimalPlaces) << value;
	return stream.str();
}

int main()
{
	SetConsoleOutputCP(65001);
	SetConsoleCtrlHandler(consoleHandler, true);
	CustomSteamAPIInit();

	auto iPlayers = g_pSteamFriends->GetCoplayFriendCount();

	std::vector<Player> players;
	int iHighestTimeStamp = 0;

	for (int i = 0; i < iPlayers; ++i)
	{
		CSteamID playerSteamID = g_pSteamFriends->GetCoplayFriend(i);
		int iTimeStamp = g_pSteamFriends->GetFriendCoplayTime(playerSteamID);
		AppId_t app = g_pSteamFriends->GetFriendCoplayGame(playerSteamID);

		if (playerSteamID == g_pSteamUser->GetSteamID())
			continue;

		if (app != 730)
			continue;

		if (iTimeStamp > iHighestTimeStamp)
			iHighestTimeStamp = iTimeStamp;

		players.emplace_back(playerSteamID, iTimeStamp);
	}

	std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) { return a.time > b.time; });

	std::erase_if(players, [iHighestTimeStamp](const Player& player) {
		static auto highestTime = std::chrono::system_clock::from_time_t(iHighestTimeStamp);
		auto time = std::chrono::system_clock::from_time_t(player.time);

		return std::chrono::duration_cast<std::chrono::minutes>(highestTime - time).count() > 2;
	});

	if (players.size() > 9)
		players.erase(players.begin() + 9, players.end());

	std::vector<std::thread> threads;
	std::vector<LeetifyUser> leetifyUsers;
	std::mutex mtx;
	
	for (const auto& player : players)
	{
		threads.emplace_back([&player, &mtx, &leetifyUsers]() {
			auto leetifyUser = GetLeetifyUser(player.playerSteamID.ConvertToUint64());
			std::lock_guard<std::mutex> lock(mtx);
			leetifyUsers.push_back(leetifyUser);
		});
	}

	for (auto& thread : threads)
		thread.join();

	Table tblPlayers;
	tblPlayers.add_row({ "Name", "Aim", "Positioning", "Utility", "Win Rate", "Leetify Rating", "Premier", "Matches"});

	for (const auto& user : leetifyUsers)
	{
		const char* playerName = g_pSteamFriends->GetFriendPersonaName(user.steamID);

		if (!user.success)
		{
			tblPlayers.add_row({ playerName, "N/A", "N/A", "N/A", "N/A", "N/A", "N/A", "N/A" });
			continue;
		}

		tblPlayers.add_row({ playerName, std::to_string((int)user.recentGameRatings.aim), std::to_string((int)user.recentGameRatings.positioning), std::to_string((int)user.recentGameRatings.utility), std::to_string((int)user.winRate) + "%", roundTo(user.recentGameRatings.leetifyRating * 100, 2), std::to_string(user.skillLevel), std::to_string(user.matches)});
	}

	tblPlayers.format()
		.multi_byte_characters(true)
		.locale("en_US.UTF-8");

	for(size_t i = 0; i < 8; ++i)
		tblPlayers[0][i].format().font_color(Color::yellow).font_style({ FontStyle::bold });

	std::cout << tblPlayers << std::endl;

	printf("Open links in browser (Y/n) ");

	int input = getchar();
	if (input == 'n' || input == 'N')
	{
		CustomSteamAPIShutdown();
		return 0;
	}

	for (const auto& player : players)
	{
		std::string url = "https://leetify.com/app/profile/" + std::to_string(player.playerSteamID.ConvertToUint64());
		ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	CustomSteamAPIShutdown();

	return 0;
}