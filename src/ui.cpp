#include "ui.h"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/dom/table.hpp"
#include "ftxui/screen/color.hpp"
#include "ftxui/screen/screen.hpp"
#include "leetify_provider.h"
#include "main.h"
#include "steam_api.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

static std::string roundTo(float value, int decimalPlaces)
{
	std::stringstream stream;
	stream << std::fixed << std::setprecision(decimalPlaces) << value;
	return stream.str();
}

void processAndSortUsers(std::vector<LeetifyUser> &leetifyUsers)
{
	int nextLobbyID = 1;

	auto findUserBySteamID = [&leetifyUsers](CSteamID steamID) {
		return std::find_if(leetifyUsers.begin(), leetifyUsers.end(),
		                    [steamID](const LeetifyUser &u) { return u.steamID == steamID; });
	};

	// First, create a bidirectional relationship map
	// If A has B as a teammate, both should be in the same lobby
	std::map<CSteamID, std::set<CSteamID>> teammateConnections;

	// Build connections in both directions
	for (const auto &user : leetifyUsers)
	{
		// Add connections from this user's teammates list
		for (const auto &teammateInfo : user.recentTeammates)
		{
			// Only add connections for teammates that exist in our list
			if (findUserBySteamID(teammateInfo.steamID) != leetifyUsers.end())
			{
				teammateConnections[user.steamID].insert(teammateInfo.steamID);
				teammateConnections[teammateInfo.steamID].insert(user.steamID);
			}
		}
	}

	// Now assign lobby IDs using the bidirectional connections
	std::map<CSteamID, int> steamIDToLobbyID;

	for (auto &user : leetifyUsers)
	{
		auto userID = user.steamID;

		// If this user already has a lobby ID, skip
		if (steamIDToLobbyID.find(userID) != steamIDToLobbyID.end())
		{
			user.lobbyID = steamIDToLobbyID[userID];
			continue;
		}

		// No lobby ID yet, check if this user has any teammates
		if (teammateConnections[userID].empty())
		{
			// No teammates, assign single-player lobby (0)
			user.lobbyID = 0;
			steamIDToLobbyID[userID] = 0;
			continue;
		}

		// Create a new lobby with teammates
		auto lobbyID = nextLobbyID++;
		user.lobbyID = lobbyID;
		steamIDToLobbyID[userID] = lobbyID;

		// Use a queue to find all connected teammates (breadth-first search)
		std::queue<CSteamID> toProcess;
		std::set<CSteamID> processed;
		toProcess.push(userID);
		processed.insert(userID);

		while (!toProcess.empty())
		{
			auto &currentID = toProcess.front();
			toProcess.pop();

			// Process all teammates of the current user
			for (auto &teammateID : teammateConnections[currentID])
			{
				// Skip already processed teammates
				if (processed.find(teammateID) != processed.end())
				{
					continue;
				}

				// Mark as processed
				processed.insert(teammateID);

				// Add to process queue
				toProcess.push(teammateID);

				// Assign same lobby ID
				auto teammateIt = findUserBySteamID(teammateID);
				if (teammateIt != leetifyUsers.end())
				{
					teammateIt->lobbyID = lobbyID;
					steamIDToLobbyID[teammateID] = lobbyID;
				}
			}
		}
	}

	// Sort users by lobby ID and then by Leetify rating
	std::sort(leetifyUsers.begin(), leetifyUsers.end(), [](const LeetifyUser &a, const LeetifyUser &b) {
		if (b.lobbyID != a.lobbyID)
		{
			return a.lobbyID > b.lobbyID;
		}

		if (b.success != a.success)
		{
			return a.success;
		}

		if (b.ranks.leetify != a.ranks.leetify)
		{
			return a.ranks.leetify > b.ranks.leetify;
		}

		return a.steamID > b.steamID;
	});
}

void renderTable(CSteamID mySteamID, std::vector<LeetifyUser> leetifyUsers)
{
	using namespace ftxui;

	auto findUserBySteamID = [&leetifyUsers](CSteamID steamID) {
		return std::find_if(leetifyUsers.begin(), leetifyUsers.end(),
		                    [steamID](const LeetifyUser &u) { return u.steamID == steamID; });
	};

	auto now = std::chrono::system_clock::now();
	auto lastSeenLobbyID = -1;
	std::vector<std::vector<Element>> table_data;

	table_data.push_back({text(" Name ") | bold | color(Color::Yellow), text(" Leetify ") | bold | color(Color::Yellow),
	                      text(" Premier ") | bold | color(Color::Yellow), text(" Aim ") | bold | color(Color::Yellow),
	                      text(" Pos ") | bold | color(Color::Yellow), text(" Reaction ") | bold | color(Color::Yellow),
	                      text(" Preaim ") | bold | color(Color::Yellow), text(" HS% ") | bold | color(Color::Yellow),
	                      text(" Win% ") | bold | color(Color::Yellow), text(" Wins ") | bold | color(Color::Yellow),
	                      text(" FACEIT ") | bold | color(Color::Yellow), text(" Time ") | bold | color(Color::Yellow),
	                      text(" Bans ") | bold | color(Color::Yellow),
	                      text(" Teammates ") | bold | color(Color::Yellow)});

	for (const auto &user : leetifyUsers)
	{
		if (lastSeenLobbyID != user.lobbyID)
		{
			if (lastSeenLobbyID != -1)
			{
				table_data.push_back({text("")});
			}

			lastSeenLobbyID = user.lobbyID;
		}

		std::vector<Element> row;
		auto playedAgoMinutes = std::chrono::duration_cast<std::chrono::minutes>(
		                            now - std::chrono::system_clock::from_time_t(user.playedTime))
		                            .count();

		auto profileUrl = "https://leetify.com/app/profile/" + std::to_string(user.steamID.ConvertToUint64());

		row.push_back(hbox({
		    text(" "),
		    text(GetPersonaName(user) + " ") | hyperlink(profileUrl) |
		        color(user.steamID == mySteamID ? Color::Yellow : Color::White),
		}));

		if (!user.success)
		{
			row.push_back(text(" N/A ") | color(Color::Magenta));
			row.push_back(text(" ? "));
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
			row.push_back(text(""));
			table_data.push_back(row);
			continue;
		}

		auto leetifyRating = user.ranks.leetify * 100.0;

		auto leetifyColor = leetifyRating >= 5    ? Color::Yellow
		                    : leetifyRating >= 1  ? Color::Green
		                    : leetifyRating <= -1 ? Color::Red
		                                          : Color::White;

		auto premierColor = user.ranks.premier >= 30000   ? Color::Yellow
		                    : user.ranks.premier >= 25000 ? Color::Red
		                    : user.ranks.premier >= 20000 ? Color::Magenta
		                    : user.ranks.premier >= 15000 ? Color::Blue
		                    : user.ranks.premier >= 10000 ? Color::Cyan
		                                                  : Color::White;

		auto aimColor = user.rating.aim >= 85 ? Color::Red : user.rating.aim >= 60 ? Color::Green : Color::White;

		auto posColor = user.rating.positioning >= 60 ? Color::Green : Color::White;
		auto winsColor = user.winRate >= 55 ? Color::Green : user.winRate <= 45 ? Color::Red : Color::White;

		auto faceitColor = user.ranks.faceit >= 10  ? Color::Red
		                   : user.ranks.faceit >= 8 ? Color::Magenta
		                                            : Color::White;

		auto reactionColor = user.skills.reaction_time < 300   ? Color::Red
		                     : user.skills.reaction_time < 450 ? Color::Green
		                     : user.skills.reaction_time > 650 ? Color::Yellow
		                                                       : Color::White;

		auto preaimColor = user.skills.preaim < 3 ? Color::Red : user.skills.preaim < 10 ? Color::Green : Color::White;

		auto hsColor = user.skills.accuracy_head >= 20 ? Color::Green : Color::White;

		row.push_back(text((user.ranks.leetify >= 0.0 ? " +" : " ") + roundTo(leetifyRating, 2) + " ") |
		              color(leetifyColor) | bold);
		row.push_back(text(" " + (user.ranks.premier <= 0 ? "?" : std::to_string(user.ranks.premier)) + " ") |
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

		if (user.steamID == mySteamID)
		{
			row.push_back(text(" you ") | color(Color::GrayDark));
		}
		else if (playedAgoMinutes >= 1440)
		{
			row.push_back(text(" >" + std::to_string(playedAgoMinutes / 1440) + "d ago ") | color(Color::GrayDark));
		}
		else if (playedAgoMinutes >= 180)
		{
			row.push_back(text(" >" + std::to_string(playedAgoMinutes / 60) + "h ago ") | color(Color::GrayDark));
		}
		else
		{
			row.push_back(text(" " + std::to_string(playedAgoMinutes) + "m ago "));
		}

		if (!user.bans.empty())
		{
			std::string bansStr;
			for (auto i = 0; i < user.bans.size(); i++)
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

		std::string teammatesStr;
		auto firstTeammate = true;

		for (const auto &teammate : user.recentTeammates)
		{
			auto teammateIt = findUserBySteamID(teammate.steamID);
			if (teammateIt != leetifyUsers.end())
			{
				if (!firstTeammate)
				{
					teammatesStr += ", ";
				}
				firstTeammate = false;

				auto teammateName = GetPersonaName(*teammateIt);

				if (teammate.matchCount > 1)
				{
					teammatesStr += teammateName + " (" + std::to_string(teammate.matchCount) + ")";
				}
				else
				{
					teammatesStr += teammateName;
				}
			}
		}

		row.push_back(text(" " + teammatesStr + " "));

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

void Render(CSteamID mySteamID, std::vector<LeetifyUser> leetifyUsers)
{
	processAndSortUsers(leetifyUsers);
	renderTable(mySteamID, leetifyUsers);

	printf("\n\nReaction is time to damage. Wins is Premier wins in current season.");
	printf("\nCtrl+Click on player name to open on Leetify.");
}
