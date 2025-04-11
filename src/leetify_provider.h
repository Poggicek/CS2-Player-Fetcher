#pragma once

#include <set>
#include <steam_api.h>

struct Player
{
	CSteamID steamID;
	int time;

	Player(CSteamID playerSteamID, int iTimeStamp) : steamID(playerSteamID), time(iTimeStamp)
	{
	}
};

struct LeetifyUser
{
	bool success = false;
	float winRate = 0.0f;
	int lobbyID = 0;
	int matchmakingWins = -1;
	int playedTime = 0;
	CSteamID steamID;
	std::string name;
	std::vector<std::string> bans;

	struct TeammateInfo
	{
		CSteamID steamID;
		int matchCount;

		TeammateInfo(CSteamID id, int count) : steamID(id), matchCount(count)
		{
		}
	};

	std::vector<TeammateInfo> recentTeammates;

	struct Ranks
	{
		float leetify;
		int premier;
		int faceit;
	} ranks;

	struct Rating
	{
		float aim;
		float positioning;
		float utility;
		float leetifyRating;
		float clutch;
		float opening;
		float ct_leetify;
		float t_leetify;
	} rating;

	struct Skills
	{
		float accuracy_enemy_spotted;
		float accuracy_head;
		float counter_strafing_good_shots_ratio;
		float ct_opening_aggression_success_rate;
		float ct_opening_duel_success_percentage;
		float flashbang_hit_foe_avg_duration;
		float flashbang_hit_foe_per_flashbang;
		float flashbang_hit_friend_per_flashbang;
		float flashbang_leading_to_kill;
		float flashbang_thrown;
		float he_foes_damage_avg;
		float he_friends_damage_avg;
		float preaim;
		float reaction_time;
		float spray_accuracy;
		float t_opening_aggression_success_rate;
		float t_opening_duel_success_percentage;
		float traded_deaths_success_percentage;
		float trade_kill_opportunities_per_round;
		float trade_kills_success_percentage;
		float utility_on_death_avg;
	} skills;
};

std::vector<LeetifyUser> GetLeetifyUsers(const std::vector<Player> &steamIDs);
