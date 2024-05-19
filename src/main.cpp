#include <iostream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <tabulate/table.hpp>

#ifdef _WIN32
#include <windows.h>
#define GET_PROC_ADDRESS GetProcAddress
#else
#include <dlfcn.h>
#include <signal.h>
#define GET_PROC_ADDRESS dlsym
#endif

#include "main.h"
#include "leetify_provider.h"

using namespace tabulate;

#ifdef _WIN32
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
	RegCloseKey(hKey);

	if (lRes != ERROR_SUCCESS)
	{
		printf("Failed to query registry key\n");
		return "";
	}

	return std::string(value);
}
#endif

void CustomSteamAPIInit()
{
#ifdef _WIN32
	auto steamClientDllPath = GetSteamClientDllPath();

	auto clientModule = LoadLibraryExA(steamClientDllPath.c_str(), 0, 8);
#else
	void* clientModule = dlopen("steamclient.so", RTLD_NOW);
#endif

	if (!clientModule)
	{
		printf("Failed to load steamclient library!\n");
		return;
	}

	auto createInterface = (CreateInterfaceFn)GET_PROC_ADDRESS(clientModule, "CreateInterface");

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

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD signal)
{
	if (signal == CTRL_CLOSE_EVENT || signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT)
	{
		CustomSteamAPIShutdown();
		return true;
	}
	return false;
}
#else
void signalHandler(int signal) {
	if (signal == SIGINT || signal == SIGTERM || signal == SIGQUIT) {
		CustomSteamAPIShutdown();
		exit(EXIT_SUCCESS);
	}
}
#endif

std::string roundTo(float value, int decimalPlaces)
{
	std::stringstream stream;
	stream << std::fixed << std::setprecision(decimalPlaces) << value;
	return stream.str();
}

int main()
{
#ifdef _WIN32
	SetConsoleOutputCP(65001);
	SetConsoleCtrlHandler(consoleHandler, true);
#else
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGQUIT, signalHandler);
#endif
	CustomSteamAPIInit();

	auto iPlayers = g_pSteamFriends->GetCoplayFriendCount();

	std::vector<Player> players;

	for (int i = 0; i < iPlayers; ++i)
	{
		CSteamID playerSteamID = g_pSteamFriends->GetCoplayFriend(i);
		static auto mySteamID = g_pSteamUser->GetSteamID();

		if (playerSteamID == mySteamID)
			continue;

		AppId_t app = g_pSteamFriends->GetFriendCoplayGame(playerSteamID);

		if (app != 730)
			continue;

		int iTimeStamp = g_pSteamFriends->GetFriendCoplayTime(playerSteamID);

		players.emplace_back(playerSteamID, iTimeStamp);
	}

	std::sort(players.begin(), players.end(), [](const Player& a, const Player& b)
	{
		if (a.time == b.time) {
			return a.playerSteamID > b.playerSteamID;
		}

		return a.time > b.time;
	});

	int iHighestTimeStamp;

	if (players.size() > 0) {
		iHighestTimeStamp = players[std::min(5, static_cast<int>(players.size()) - 1)].time;
	}

	std::erase_if(players, [iHighestTimeStamp](const Player& player) {
		return iHighestTimeStamp > player.time;
	});

	if (players.size() > 9)
		players.erase(players.begin() + 9, players.end());

	std::vector<std::thread> threads;
	std::vector<LeetifyUser> leetifyUsers;
	std::mutex mtx;
	int lobbyID = 1;

	for (const auto& player : players)
	{
		threads.emplace_back([&lobbyID, &player, &mtx, &leetifyUsers]() {
			auto leetifyUser = GetLeetifyUser(player.playerSteamID.ConvertToUint64());
			std::lock_guard<std::mutex> lock(mtx);
			leetifyUser.lobbyID = lobbyID++;
			leetifyUsers.push_back(leetifyUser);
		});
	}

	for (auto& thread : threads)
		thread.join();

	for (int i = 0; i < 4; i++) { // figure out a better algo to sort people into lobbies
		for (auto& user : leetifyUsers) {
			auto steamID = user.steamID.ConvertToUint64();
			auto found = false;

			for (auto& otherUser : leetifyUsers) {
				if (otherUser.teammates.contains(steamID)) {
					found = true;

					if (user.lobbyID < otherUser.lobbyID) {
						user.lobbyID = otherUser.lobbyID;
					}
				}
			}

			if (!found) {
				user.lobbyID = 0;
			}
		}
	}

	std::sort(leetifyUsers.begin(), leetifyUsers.end(), [](const LeetifyUser& a, const LeetifyUser& b) {
		if (b.lobbyID != a.lobbyID) {
			return a.lobbyID > b.lobbyID;
		}

		if (b.recentGameRatings.leetifyRating != a.recentGameRatings.leetifyRating) {
			return a.recentGameRatings.leetifyRating > b.recentGameRatings.leetifyRating;
		}

		return a.steamID > b.steamID;
	});

	int lastSeenLobbyID = -1;
	Table tblPlayers;

	tblPlayers.format()
		.multi_byte_characters(true)
		.locale("en_US.UTF-8");

	tblPlayers.add_row({ "Name", "Leetify", "Premier", "Aim", "Pos", "Util", "Wins", "Matches", "FACEIT", "Teammates" });

	for (const auto& user : leetifyUsers)
	{
		if (lastSeenLobbyID != user.lobbyID)
		{
			if (lastSeenLobbyID != -1)
			{
				tblPlayers.add_row({ "" });
			}

			lastSeenLobbyID = user.lobbyID;
		}

		const char* playerName = g_pSteamFriends->GetFriendPersonaName(user.steamID);
		auto steamID = user.steamID.ConvertToUint64();
		auto row = tblPlayers.size();
		std::vector<std::string> teammates{};

		for (const auto& otherUser : leetifyUsers)
		{
			if (otherUser.teammates.contains(steamID))
			{
				const char* teammateName = g_pSteamFriends->GetFriendPersonaName(otherUser.steamID);
				teammates.push_back(teammateName);
			}
		}

		std::ostringstream teammatesStr;
		for (size_t i = 0; i < teammates.size(); ++i) {
			if (i != 0) {
				teammatesStr << ", ";
			}
			teammatesStr << teammates[i];
		}

		if (!user.success)
		{
			tblPlayers.add_row({ playerName, "N/A", "N/A", "N/A", "N/A", "N/A", "N/A", "N/A", "", teammatesStr.str() });
			tblPlayers[row].format().font_color(Color::magenta).font_style({FontStyle::italic});
			continue;
		}

		auto leetifyRating = user.recentGameRatings.leetifyRating * 100;

		tblPlayers.add_row({
			playerName,
			(user.recentGameRatings.leetifyRating >= 0.0 ? "+" : "") + roundTo(leetifyRating, 2),
			user.skillLevel <= 0 ? "N/A" : std::to_string(user.skillLevel),
			std::to_string((int)user.recentGameRatings.aim),
			std::to_string((int)user.recentGameRatings.positioning),
			std::to_string((int)user.recentGameRatings.utility),
			std::to_string((int)user.winRate) + "%",
			std::to_string(user.matches),
			user.faceitNickname,
			teammatesStr.str()
		});

		auto rowFormat = tblPlayers[row];

		if (leetifyRating >= 1) {
			rowFormat[1].format().font_color(Color::green);
		} else if (leetifyRating <= -1) {
			rowFormat[1].format().font_color(Color::red);
		}

		if (user.recentGameRatings.aim >= 60) {
			rowFormat[3].format().font_color(Color::green);
		}

		if (user.recentGameRatings.positioning >= 60) {
			rowFormat[4].format().font_color(Color::green);
		}

		if (user.recentGameRatings.utility >= 60) {
			rowFormat[5].format().font_color(Color::green);
		}

		if (user.winRate >= 55) {
			rowFormat[6].format().font_color(Color::green);
		} else if (user.winRate <= 45) {
			rowFormat[6].format().font_color(Color::red);
		}
	}

	for(size_t i = 0; i < tblPlayers[0].size(); ++i)
		tblPlayers[0][i].format().font_color(Color::yellow).font_style({ FontStyle::bold });

	std::cout << tblPlayers << std::endl;

	printf("Open links in browser (Y/n) ");

	int input = getchar();
	if (input == EOF || input == 'n' || input == 'N')
	{
		CustomSteamAPIShutdown();
		return 0;
	}

	for (const auto& player : players)
	{
		std::string url = "https://leetify.com/app/profile/" + std::to_string(player.playerSteamID.ConvertToUint64());
#ifdef _WIN32
		ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
		system(("xdg-open " + url).c_str());
#endif
	}

	CustomSteamAPIShutdown();

	return 0;
}