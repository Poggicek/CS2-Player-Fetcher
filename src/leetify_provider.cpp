#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "leetify_provider.h"

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp)
{
	userp->append((char *)contents, size * nmemb);
	return size * nmemb;
}

struct CurlHandle
{
	LeetifyUser *user;
	CURL *handle;
	std::string response;
};

template <typename T> T getValue(const nlohmann::json &j, const std::string &key, const T &defaultValue = T())
{
	return j.contains(key) && !j[key].is_null() ? j.value(key, defaultValue) : defaultValue;
}

std::vector<LeetifyUser> GetLeetifyUsers(const std::vector<Player> &players)
{
	std::vector<LeetifyUser> users(players.size());
	std::vector<std::unique_ptr<CurlHandle>> handles(players.size());

	CURLM *multiHandle = curl_multi_init();
	if (!multiHandle)
	{
		printf("Failed to initialize CURL multi handle\n");
		return users;
	}

	for (auto i = 0; i < players.size(); i++)
	{
		auto handle = std::make_unique<CurlHandle>();
		handle->user = &users[i];
		handle->handle = curl_easy_init();

		auto &player = players[i];
		users[i].steamID = player.steamID;
		users[i].playedTime = player.time;

		if (handle->handle)
		{
			auto url = "https://api-public.cs-prod.leetify.com/v2/profiles/" +
			           std::to_string(player.steamID.ConvertToUint64());

			curl_easy_setopt(handle->handle, CURLOPT_USERAGENT,
			                 "CS2 Player Fetcher (+https://github.com/Poggicek/CS2-Player-Fetcher)");
			curl_easy_setopt(handle->handle, CURLOPT_TIMEOUT, 60L);
			curl_easy_setopt(handle->handle, CURLOPT_URL, url.c_str());
			curl_easy_setopt(handle->handle, CURLOPT_WRITEFUNCTION, WriteCallback);
			curl_easy_setopt(handle->handle, CURLOPT_WRITEDATA, &handle->response);
			curl_easy_setopt(handle->handle, CURLOPT_PRIVATE, handle.get());

			curl_multi_add_handle(multiHandle, handle->handle);

			handles[i] = std::move(handle);
		}
		else
		{
			printf("fail: %llu - failed to initialize cURL handle\n", player.steamID.ConvertToUint64());
		}
	}

	int stillRunning = 0;
	do
	{
		CURLMcode mc = curl_multi_perform(multiHandle, &stillRunning);
		if (mc == CURLM_OK)
		{
			mc = curl_multi_poll(multiHandle, nullptr, 0, 1000, nullptr);
		}

		if (mc != CURLM_OK)
		{
			break;
		}

		CURLMsg *msg;
		int msgsLeft;
		while ((msg = curl_multi_info_read(multiHandle, &msgsLeft)))
		{
			if (msg->msg != CURLMSG_DONE)
			{
				continue;
			}

			CurlHandle *handle;
			curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &handle);

			auto user = handle->user;

			if (msg->data.result != CURLE_OK)
			{
				printf("fail: %llu - cURL error %d\n", user->steamID.ConvertToUint64(), msg->data.result);
				continue;
			}

			int response_code;
			curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &response_code);

			if (response_code != 200)
			{
				if (response_code != 404)
				{
					printf("fail: %llu - Leetify error HTTP %d\n", user->steamID.ConvertToUint64(), response_code);
				}
				continue;
			}

			try
			{
				auto json = nlohmann::json::parse(handle->response);

				user->name = getValue(json, "name", std::string(""));
				user->winRate = getValue(json, "winrate", 0.0f) * 100.0f;
				user->totalMatches = getValue(json, "total_matches", 0);

				if (json.contains("first_match_date") && json["first_match_date"].is_string())
				{
					std::istringstream iss(json["first_match_date"].get<std::string>());
					std::chrono::from_stream(iss, "%Y-%m-%dT%H:%M:%S%Z", user->firstMatchDate);
				}

				if (json.contains("rating") && json["rating"].is_object())
				{
					auto &rating = json["rating"];
					user->rating.aim = getValue(rating, "aim", 0.0f);
					user->rating.positioning = getValue(rating, "positioning", 0.0f);
					user->rating.utility = getValue(rating, "utility", 0.0f);
					user->rating.clutch = getValue(rating, "clutch", 0.0f);
					user->rating.opening = getValue(rating, "opening", 0.0f);
					user->rating.ct_leetify = getValue(rating, "ct_leetify", 0.0f);
					user->rating.t_leetify = getValue(rating, "t_leetify", 0.0f);
				}

				if (json.contains("ranks") && json["ranks"].is_object())
				{
					auto &ranks = json["ranks"];
					user->ranks.leetify = getValue(ranks, "leetify", 0.0f);
					user->ranks.premier = getValue(ranks, "premier", 0);
					user->ranks.faceit = getValue(ranks, "faceit_elo", 0);
				}

				if (json.contains("stats") && json["stats"].is_object())
				{
					auto &skills = json["stats"];
					user->skills.accuracy_enemy_spotted = getValue(skills, "accuracy_enemy_spotted", 0.0f);
					user->skills.accuracy_head = getValue(skills, "accuracy_head", 0.0f);
					user->skills.counter_strafing_good_shots_ratio =
					    getValue(skills, "counter_strafing_good_shots_ratio", 0.0f);
					user->skills.ct_opening_aggression_success_rate =
					    getValue(skills, "ct_opening_aggression_success_rate", 0.0f);
					user->skills.ct_opening_duel_success_percentage =
					    getValue(skills, "ct_opening_duel_success_percentage", 0.0f);
					user->skills.flashbang_hit_foe_avg_duration =
					    getValue(skills, "flashbang_hit_foe_avg_duration", 0.0f);
					user->skills.flashbang_hit_foe_per_flashbang =
					    getValue(skills, "flashbang_hit_foe_per_flashbang", 0.0f);
					user->skills.flashbang_hit_friend_per_flashbang =
					    getValue(skills, "flashbang_hit_friend_per_flashbang", 0.0f);
					user->skills.flashbang_leading_to_kill = getValue(skills, "flashbang_leading_to_kill", 0.0f);
					user->skills.flashbang_thrown = getValue(skills, "flashbang_thrown", 0.0f);
					user->skills.he_foes_damage_avg = getValue(skills, "he_foes_damage_avg", 0.0f);
					user->skills.he_friends_damage_avg = getValue(skills, "he_friends_damage_avg", 0.0f);
					user->skills.preaim = getValue(skills, "preaim", 0.0f);
					user->skills.reaction_time = getValue(skills, "reaction_time_ms", 0.0f);
					user->skills.spray_accuracy = getValue(skills, "spray_accuracy", 0.0f);
					user->skills.t_opening_aggression_success_rate =
					    getValue(skills, "t_opening_aggression_success_rate", 0.0f);
					user->skills.t_opening_duel_success_percentage =
					    getValue(skills, "t_opening_duel_success_percentage", 0.0f);
					user->skills.traded_deaths_success_percentage =
					    getValue(skills, "traded_deaths_success_percentage", 0.0f);
					user->skills.trade_kill_opportunities_per_round =
					    getValue(skills, "trade_kill_opportunities_per_round", 0.0f);
					user->skills.trade_kills_success_percentage =
					    getValue(skills, "trade_kills_success_percentage", 0.0f);
					user->skills.utility_on_death_avg = getValue(skills, "utility_on_death_avg", 0.0f);
				}

				if (json.contains("bans") && json["bans"].is_array())
				{
					for (const auto &ban : json["bans"])
					{
						user->bans.push_back(getValue(ban, "platform", std::string("")));
					}
				}

				if (json.contains("recent_teammates") && json["recent_teammates"].is_array())
				{
					for (const auto &teammate : json["recent_teammates"])
					{
						auto steam64_id = std::stoull(getValue(teammate, "steam64_id", std::string("0")));
						auto matchCount = getValue(teammate, "recent_matches_count", 0);

						user->recentTeammates.emplace_back(CSteamID(steam64_id), matchCount);
					}
				}

				user->success = true;
			}
			catch (const std::exception &e)
			{
				printf("fail: %llu - error %s\n", user->steamID.ConvertToUint64(), e.what());
			}

			curl_multi_remove_handle(multiHandle, msg->easy_handle);
		}
	} while (stillRunning);

	for (auto &handle : handles)
	{
		curl_easy_cleanup(handle->handle);
	}

	curl_multi_cleanup(multiHandle);

	return users;
}
