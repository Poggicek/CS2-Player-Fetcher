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

void renderTable(std::vector<LeetifyUser> leetifyUsers)
{
	using namespace ftxui;

	auto now = std::chrono::system_clock::now();

	std::vector<std::vector<Element>> table_data;

	table_data.push_back({text(" Name ") | bold | color(Color::Yellow), text(" Leetify ") | bold | color(Color::Yellow),
	                      text(" Premier ") | bold | color(Color::Yellow), text(" Aim ") | bold | color(Color::Yellow),
	                      text(" Pos ") | bold | color(Color::Yellow), text(" Reaction ") | bold | color(Color::Yellow),
	                      text(" Preaim ") | bold | color(Color::Yellow), text(" HS% ") | bold | color(Color::Yellow),
	                      text(" Win% ") | bold | color(Color::Yellow), text(" Wins ") | bold | color(Color::Yellow),
	                      text(" FACEIT ") | bold | color(Color::Yellow), text(" Time ") | bold | color(Color::Yellow),
	                      text(" Bans ") | bold | color(Color::Yellow)});

	for (const auto &user : leetifyUsers)
	{
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

		row.push_back(hbox({
		    text(" "),
		    text(playerName + " ") | hyperlink(profileUrl),
		}));

		if (!user.success)
		{
			row.push_back(text(" N/A ") | color(Color::Magenta));
			row.push_back(text(""));
			row.push_back(text(""));
			row.push_back(text(""));
			row.push_back(text(""));
			row.push_back(text(""));
			row.push_back(text(""));
			row.push_back(text(""));
			row.push_back(text(""));
			row.push_back(text(""));
			row.push_back(text(""));
			row.push_back(text(" " + std::to_string(playedAgoMinutes) + "m ago "));
			row.push_back(text(""));
			table_data.push_back(row);
			continue;
		}

		float leetifyRating = user.ranks.leetify * 100;

		Color leetifyColor = leetifyRating >= 5    ? Color::Yellow
		                     : leetifyRating >= 1  ? Color::Green
		                     : leetifyRating <= -1 ? Color::Red
		                                           : Color::White;

		Color premierColor = user.ranks.premier >= 30000   ? Color::Yellow
		                     : user.ranks.premier >= 25000 ? Color::Red
		                     : user.ranks.premier >= 20000 ? Color::Magenta
		                     : user.ranks.premier >= 15000 ? Color::Blue
		                     : user.ranks.premier >= 10000 ? Color::Cyan
		                                                   : Color::White;

		Color aimColor = user.rating.aim >= 85 ? Color::Red : user.rating.aim >= 60 ? Color::Green : Color::White;

		Color posColor = user.rating.positioning >= 60 ? Color::Green : Color::White;
		Color winsColor = user.winRate >= 55 ? Color::Green : user.winRate <= 45 ? Color::Red : Color::White;

		Color faceitColor = user.ranks.faceit >= 2001   ? Color::Red
		                    : user.ranks.faceit >= 1701 ? Color::Magenta
		                                                : Color::White;

		Color reactionColor = user.skills.reaction_time < 300   ? Color::Red
		                      : user.skills.reaction_time < 450 ? Color::Green
		                      : user.skills.reaction_time > 650 ? Color::Yellow
		                                                        : Color::White;

		Color preaimColor = user.skills.preaim < 3    ? Color::Red
		                    : user.skills.preaim < 10 ? Color::Green
		                                              : Color::White;

		Color hsColor = user.skills.accuracy_head >= 20 ? Color::Green : Color::White;

		row.push_back(text((user.ranks.leetify >= 0.0 ? "+" : "") + roundTo(leetifyRating, 2) + " ") |
		              color(leetifyColor) | bold);
		row.push_back(text(user.ranks.premier <= 0 ? "" : std::to_string(user.ranks.premier) + " ") |
		              color(premierColor));
		row.push_back(text(" " + std::to_string((int)user.rating.aim) + " ") | color(aimColor));
		row.push_back(text(" " + std::to_string((int)user.rating.positioning) + " ") | color(posColor));

		row.push_back(text(" " + std::to_string((int)user.skills.reaction_time) + "ms") | color(reactionColor));
		row.push_back(text(" " + roundTo(user.skills.preaim, 2) + "° ") | color(preaimColor));
		row.push_back(text(" " + std::to_string((int)user.skills.accuracy_head) + " ") | color(hsColor));

		row.push_back(text(" " + std::to_string((int)user.winRate) + "% ") | color(winsColor));
		row.push_back(text(std::to_string(user.matchmakingWins) + " "));

		if (user.ranks.faceit > 0)
		{
			row.push_back(text(std::to_string(user.ranks.faceit) + " ") | color(faceitColor));
		}
		else
		{
			row.push_back(text(""));
		}

		row.push_back(text(" " + std::to_string(playedAgoMinutes) + "m ago "));

		if (!user.bans.empty())
		{
			std::string bansStr;
			for (size_t i = 0; i < user.bans.size(); ++i)
			{
				bansStr += user.bans[i];
				if (i < user.bans.size() - 1)
				{
					bansStr += ", ";
				}
			}
			row.push_back(text(" " + bansStr) | color(Color::Red));
		}
		else
		{
			row.push_back(text(""));
		}

		table_data.push_back(row);
	}

	auto table = Table(table_data);

	table.SelectAll().SeparatorVertical(LIGHT);
	table.SelectAll().Border(HEAVY);

	table.SelectColumn(1).DecorateCells(align_right);
	table.SelectColumn(2).DecorateCells(align_right);
	table.SelectColumn(3).DecorateCells(align_right);
	table.SelectColumn(4).DecorateCells(align_right);
	table.SelectColumn(6).DecorateCells(align_right);
	table.SelectColumn(7).DecorateCells(align_right);
	table.SelectColumn(8).DecorateCells(align_right);
	table.SelectColumn(9).DecorateCells(align_right);
	table.SelectColumn(10).DecorateCells(align_right);

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

	std::sort(leetifyUsers.begin(), leetifyUsers.end(), [](const LeetifyUser &a, const LeetifyUser &b) {
		if (b.ranks.leetify != a.ranks.leetify)
		{
			return a.ranks.leetify > b.ranks.leetify;
		}

		return a.steamID > b.steamID;
	});

	renderTable(leetifyUsers);

	CustomSteamAPIShutdown();

	printf("\n\nReaction is time to damage. Wins is Premier wins in current season.");
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
