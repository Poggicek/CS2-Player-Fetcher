#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"
#include "ftxui/screen/screen.hpp"
#include <algorithm>
#include <curl/curl.h>
#include <filesystem>
#include <windows.h>

#include "leetify_provider.h"
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
	RegCloseKey(hKey);

	if (lRes != ERROR_SUCCESS)
	{
		printf("Failed to query registry key\n");
		return "";
	}

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

std::string roundTo(float value, int decimalPlaces)
{
	std::stringstream stream;
	stream << std::fixed << std::setprecision(decimalPlaces) << value;
	return stream.str();
}

void processAndSortUsers(std::vector<LeetifyUser> &leetifyUsers)
{
	int nextLobbyID = 1;

	// Helper function to find a user by Steam ID
	auto findUserBySteamID = [&leetifyUsers](uint64 steamID) {
		return std::find_if(leetifyUsers.begin(), leetifyUsers.end(),
		                    [steamID](const LeetifyUser &u) { return u.steamID.ConvertToUint64() == steamID; });
	};

	// Assign lobby IDs and handle single-player lobbies in a single pass
	for (auto &user : leetifyUsers)
	{
		if (user.lobbyID == 0)
		{
			user.lobbyID = nextLobbyID++;
			bool hasTeammate = false;

			for (const auto &teammateSteamID : user.teammates)
			{
				auto teammateIt = findUserBySteamID(teammateSteamID);
				if (teammateIt != leetifyUsers.end())
				{
					hasTeammate = true;
					if (teammateIt->lobbyID != 0 && teammateIt->lobbyID != user.lobbyID)
					{
						// Merge lobbies
						int oldLobbyID = teammateIt->lobbyID;
						for (auto &u : leetifyUsers)
						{
							if (u.lobbyID == oldLobbyID)
							{
								u.lobbyID = user.lobbyID;
							}
						}
					}
					else
					{
						teammateIt->lobbyID = user.lobbyID;
					}
				}
			}

			// Reset lobby ID if it's a single-player lobby
			if (!hasTeammate)
			{
				user.lobbyID = 0;
			}
		}
	}

	// Sort users by lobby ID and then by Leetify rating
	std::sort(leetifyUsers.begin(), leetifyUsers.end(), [](const LeetifyUser &a, const LeetifyUser &b) {
		if (b.lobbyID != a.lobbyID)
		{
			return a.lobbyID > b.lobbyID;
		}

		if (b.recentGameRatings.leetifyRating != a.recentGameRatings.leetifyRating)
		{
			return a.recentGameRatings.leetifyRating > b.recentGameRatings.leetifyRating;
		}

		return a.steamID > b.steamID;
	});
}

void renderTable(std::vector<LeetifyUser> leetifyUsers)
{
	using namespace ftxui;

	auto now = std::chrono::system_clock::now();
	int lastSeenLobbyID = -1;

	// Build table data as a 2D vector of Elements (allowing per-cell color).
	std::vector<std::vector<Element>> table_data;

	// Header row.
	table_data.push_back({text("Name") | bold | color(Color::Yellow), text("Leetify") | bold | color(Color::Yellow),
	                      text("Premier") | bold | color(Color::Yellow), text("Aim") | bold | color(Color::Yellow),
	                      text("Pos") | bold | color(Color::Yellow), text("Util") | bold | color(Color::Yellow),
	                      text("Wins") | bold | color(Color::Yellow), text("Matches") | bold | color(Color::Yellow),
	                      text("FACEIT") | bold | color(Color::Yellow), text("Time") | bold | color(Color::Yellow)});

	// For each Leetify user, build a row with colored cells.
	for (const auto &user : leetifyUsers)
	{
		if (lastSeenLobbyID != user.lobbyID)
		{
			table_data.push_back({text(user.lobbyID == 0 ? "Solos" : "Lobby") | bold | color(Color::Cyan)});

			lastSeenLobbyID = user.lobbyID;
		}

		std::vector<Element> row;
		std::string playerName = std::string(g_pSteamFriends->GetFriendPersonaName(user.steamID));
		if (playerName.empty() || playerName == "[unknown]")
		{
			playerName = user.name;
		}

		auto playedAgoMinutes = std::chrono::duration_cast<std::chrono::minutes>(
		                            now - std::chrono::system_clock::from_time_t(user.playedTime))
		                            .count();

		std::string profileUrl = "https://leetify.com/app/profile/" + std::to_string(user.steamID.ConvertToUint64());

		row.push_back(text(playerName) | hyperlink(profileUrl));

		if (!user.success)
		{
			row.push_back(text("N/A") | bold | color(Color::Magenta));
			row.push_back(text("N/A") | bold | color(Color::Magenta));
			row.push_back(text("N/A") | bold | color(Color::Magenta));
			row.push_back(text("N/A") | bold | color(Color::Magenta));
			row.push_back(text("N/A") | bold | color(Color::Magenta));
			row.push_back(text("N/A") | bold | color(Color::Magenta));
			row.push_back(text("N/A") | bold | color(Color::Magenta));
			row.push_back(text(""));
			row.push_back(text(std::to_string(playedAgoMinutes) + "m ago"));
			table_data.push_back(row);
			continue;
		}

		float leetifyRating = user.recentGameRatings.leetifyRating * 100;

		Color leetifyColor = leetifyRating >= 5    ? Color::Yellow
		                     : leetifyRating >= 1  ? Color::Green
		                     : leetifyRating <= -1 ? Color::Red
		                                           : Color::White;

		Color premierColor = user.skillLevel >= 30000   ? Color::Yellow
		                     : user.skillLevel >= 25000 ? Color::Red
		                     : user.skillLevel >= 20000 ? Color::Magenta
		                     : user.skillLevel >= 15000 ? Color::Blue
		                     : user.skillLevel >= 10000 ? Color::Cyan
		                                                : Color::White;

		Color aimColor = user.recentGameRatings.aim >= 85   ? Color::Red
		                 : user.recentGameRatings.aim >= 60 ? Color::Green
		                                                    : Color::White;

		Color posColor = user.recentGameRatings.positioning >= 60 ? Color::Green : Color::White;
		Color utilColor = user.recentGameRatings.utility >= 60 ? Color::Green : Color::White;
		Color winsColor = user.winRate >= 55 ? Color::Green : user.winRate <= 45 ? Color::Red : Color::White;

		Color faceitColor = user.faceitElo >= 2001   ? Color::Red
		                    : user.faceitElo >= 1701 ? Color::Magenta
		                                             : Color::White;

		row.push_back(text((user.recentGameRatings.leetifyRating >= 0.0 ? "+" : "") + roundTo(leetifyRating, 2)) |
		              color(leetifyColor));
		row.push_back(text(user.skillLevel <= 0 ? "N/A" : std::to_string(user.skillLevel)) | color(premierColor));
		row.push_back(text(std::to_string((int)user.recentGameRatings.aim)) | color(aimColor));
		row.push_back(text(std::to_string((int)user.recentGameRatings.positioning)) | color(posColor));
		row.push_back(text(std::to_string((int)user.recentGameRatings.utility)) | color(utilColor));
		row.push_back(text(std::to_string((int)user.winRate) + "%") | color(winsColor));
		row.push_back(text(std::to_string(user.matches)));

		if (user.faceitNickname.length() > 0)
		{
			row.push_back(
			    text((user.faceitElo > 0 ? ("[" + std::to_string(user.faceitElo) + "] ") : "") + user.faceitNickname) |
			    color(faceitColor) | hyperlink("https://www.faceit.com/en/players/" + user.faceitNickname));
		}
		else
		{
			row.push_back(text(""));
		}

		row.push_back(text(std::to_string(playedAgoMinutes) + "m ago"));

		table_data.push_back(row);
	}

	auto table = Table(table_data);

	table.SelectAll().SeparatorVertical(LIGHT);
	table.SelectAll().Border(HEAVY);

	table.SelectColumn(1).DecorateCells(align_right);
	table.SelectColumn(2).DecorateCells(align_right);
	table.SelectColumn(3).DecorateCells(align_right);
	table.SelectColumn(4).DecorateCells(align_right);
	table.SelectColumn(5).DecorateCells(align_right);
	table.SelectColumn(6).DecorateCells(align_right);
	table.SelectColumn(7).DecorateCells(align_right);

	auto document = table.Render();
	auto screen = Screen::Create(Dimension::Fit(document));
	Render(screen, document);
	screen.Print();
}

int main()
{
	SetConsoleOutputCP(65001);
	SetConsoleCtrlHandler(consoleHandler, true);
	SetConsoleTitle("Leetify Stats");

	CustomSteamAPIInit();

	auto iPlayers = g_pSteamFriends->GetCoplayFriendCount();

	std::vector<Player> players;

	for (int i = 0; i < iPlayers; ++i)
	{
		CSteamID playerSteamID = g_pSteamFriends->GetCoplayFriend(i);
		static auto mySteamID = g_pSteamUser->GetSteamID();

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

	curl_global_init(CURL_GLOBAL_DEFAULT);
	std::vector<LeetifyUser> leetifyUsers = GetLeetifyUsers(players);
	curl_global_cleanup();

	processAndSortUsers(leetifyUsers);
	renderTable(leetifyUsers);

	CustomSteamAPIShutdown();

	printf("\n\nCtrl+Click on player name to open on Leetify. Open links in browser (Y/n) ");

	int input = getchar();
	if (input == EOF || input == 'n' || input == 'N')
	{
		return 0;
	}

	for (const auto &player : players)
	{
		std::string url = "https://leetify.com/app/profile/" + std::to_string(player.steamID.ConvertToUint64());
		ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	return 0;
}
