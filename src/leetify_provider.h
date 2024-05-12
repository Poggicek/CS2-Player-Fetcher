#pragma once

#include <set>
#include <steam_api.h>

struct LeetifyUser
{
	bool success = false;
	float winRate = 0.0f;
	int skillLevel = -1;
	int matches = -1;
	int lobbyID = 0;
	CSteamID steamID;
	std::set<uint64> teammates{};
	std::string faceitNickname;

	struct RecentGameRatings
	{
		float aim = 0.0f;
		float positioning = 0.0f;
		float utility = 0.0f;
		float leetifyRating = -100.0f;
	} recentGameRatings;
};

LeetifyUser GetLeetifyUser(CSteamID steamid);