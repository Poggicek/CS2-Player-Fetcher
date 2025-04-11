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

	for (size_t i = 0; i < players.size(); i++)
	{
		auto handle = std::make_unique<CurlHandle>();
		handle->user = &users[i];
		handle->handle = curl_easy_init();

		auto player = players[i];
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
				printf("fail: %llu - Leetify error HTTP %d\n", user->steamID.ConvertToUint64(), response_code);
				continue;
			}

			try
			{
				auto json = nlohmann::json::parse(handle->response);

				user->name = json["name"].is_null() ? "" : json["name"].get<std::string>();
				user->winRate = json["winrate"].get<float>() * 100.0f;
				user->matchmakingWins = json["matchmaking_wins"].get<int>();

				if (json.contains("rating") && json["rating"].is_object())
				{
					auto &rating = json["rating"];
					user->rating.aim = rating["aim"].get<float>();
					user->rating.positioning = rating["positioning"].get<float>();
					user->rating.utility = rating["utility"].get<float>();
					user->rating.clutch = rating["clutch"].get<float>();
					user->rating.opening = rating["opening"].get<float>();
					user->rating.ct_leetify = rating["ct_leetify"].get<float>();
					user->rating.t_leetify = rating["t_leetify"].get<float>();
				}

				if (json.contains("ranks") && json["ranks"].is_object())
				{
					if (!json["ranks"]["leetify"].is_null())
					{
						user->ranks.leetify = json["ranks"]["leetify"].get<float>();
					}

					if (!json["ranks"]["premier"].is_null())
					{
						user->ranks.premier = json["ranks"]["premier"].get<int>();
					}

					if (!json["ranks"]["faceit"].is_null())
					{
						user->ranks.faceit = json["ranks"]["faceit"].get<int>();
					}
				}

				if (json.contains("skills") && json["skills"].is_object())
				{
					auto &skills = json["skills"];
					user->skills.accuracy_enemy_spotted = skills["accuracy_enemy_spotted"].get<float>();
					user->skills.accuracy_head = skills["accuracy_head"].get<float>();
					user->skills.counter_strafing_good_shots_ratio =
					    skills["counter_strafing_good_shots_ratio"].get<float>();
					user->skills.ct_opening_aggression_success_rate =
					    skills["ct_opening_aggression_success_rate"].get<float>();
					user->skills.ct_opening_duel_success_percentage =
					    skills["ct_opening_duel_success_percentage"].get<float>();
					user->skills.flashbang_hit_foe_avg_duration = skills["flashbang_hit_foe_avg_duration"].get<float>();
					user->skills.flashbang_hit_foe_per_flashbang =
					    skills["flashbang_hit_foe_per_flashbang"].get<float>();
					user->skills.flashbang_hit_friend_per_flashbang =
					    skills["flashbang_hit_friend_per_flashbang"].get<float>();
					user->skills.flashbang_leading_to_kill = skills["flashbang_leading_to_kill"].get<float>();
					user->skills.flashbang_thrown = skills["flashbang_thrown"].get<float>();
					user->skills.he_foes_damage_avg = skills["he_foes_damage_avg"].get<float>();
					user->skills.he_friends_damage_avg = skills["he_friends_damage_avg"].get<float>();
					user->skills.preaim = skills["preaim"].get<float>();
					user->skills.reaction_time = skills["reaction_time"].get<float>();
					user->skills.spray_accuracy = skills["spray_accuracy"].get<float>();
					user->skills.t_opening_aggression_success_rate =
					    skills["t_opening_aggression_success_rate"].get<float>();
					user->skills.t_opening_duel_success_percentage =
					    skills["t_opening_duel_success_percentage"].get<float>();
					user->skills.traded_deaths_success_percentage =
					    skills["traded_deaths_success_percentage"].get<float>();
					user->skills.trade_kill_opportunities_per_round =
					    skills["trade_kill_opportunities_per_round"].get<float>();
					user->skills.trade_kills_success_percentage = skills["trade_kills_success_percentage"].get<float>();
					user->skills.utility_on_death_avg = skills["utility_on_death_avg"].get<float>();
				}

				if (json.contains("bans") && json["bans"].is_array())
				{
					user->bans = json["bans"].get<std::vector<std::string>>();
				}

				if (json.contains("recent_teammates") && json["recent_teammates"].is_array())
				{
					for (const auto &teammate : json["recent_teammates"])
					{
						auto steam64_id = std::stoull(teammate["steam64_id"].get<std::string>());
						auto matchCount = teammate["recent_matches_count"].get<int>();

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
