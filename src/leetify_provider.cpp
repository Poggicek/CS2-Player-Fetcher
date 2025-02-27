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
			auto url =
			    "https://api.cs-prod.leetify.com/api/profile/id/" + std::to_string(player.steamID.ConvertToUint64());

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
			printf("Failed to initialize CURL handle for %llu\n", i);
		}
	}

	auto now = std::chrono::system_clock::now();
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

			if (msg->data.result != CURLE_OK)
			{
				printf("failed to perform curl: error %d\n", msg->data.result);
				continue;
			}

			long response_code;
			curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &response_code);

			if (response_code != 200)
			{
				printf("failed to perform leetify request: HTTP %d\n", response_code);
				continue;
			}

			auto user = handle->user;

			try
			{
				auto json = nlohmann::json::parse(handle->response);

				user->name = json["meta"]["name"].is_null() ? "" : json["meta"]["name"].get<std::string>();
				user->recentGameRatings.aim = json["recentGameRatings"]["aim"].get<float>();
				user->recentGameRatings.positioning = json["recentGameRatings"]["positioning"].get<float>();
				user->recentGameRatings.utility = json["recentGameRatings"]["utility"].get<float>();
				user->recentGameRatings.leetifyRating = json["recentGameRatings"]["leetify"].get<float>();

				if (json["meta"].contains("faceitNickname"))
				{
					user->faceitNickname = json["meta"]["faceitNickname"].get<std::string>();
				}

				auto games = json["games"].get<std::vector<nlohmann::json>>();

				auto twoMonthsAgo = now - std::chrono::hours(24 * 60);

				auto latestFaceitGame =
				    std::find_if(games.begin(), games.end(), [&twoMonthsAgo](const nlohmann::json &game) {
					    if (game["dataSource"].get<std::string>() != "faceit")
					    {
						    return false;
					    }

					    auto gameFinishedAt = game["gameFinishedAt"].get<std::string>();
					    std::tm tm = {};
					    std::istringstream ss(gameFinishedAt);
					    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
					    auto gameTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));

					    return gameTime > twoMonthsAgo;
				    });

				if (latestFaceitGame != games.end() && !latestFaceitGame->at("elo").is_null())
				{
					user->faceitElo = latestFaceitGame->value("elo", -1);
				}

				user->matches = games.size();

				auto mmGames = std::find_if(games.begin(), games.end(), [&](const nlohmann::json &game) {
					return game["dataSource"].get<std::string>() == "matchmaking" && !game["rankType"].is_null() &&
					       game.value("rankType", -1) == 11;
				});

				if (mmGames != games.end())
				{
					user->skillLevel = mmGames[0].at("skillLevel").get<int>();
				}

				if (games.size() > 30)
				{
					games.erase(games.begin() + 30, games.end());
				}

				int wins = 0;
				int ties = 0;

				for (const auto &game : games)
				{
					auto matchResult = game["matchResult"].get<std::string>();
					if (matchResult == "win")
					{
						wins++;
					}
					else if (matchResult == "tie")
					{
						ties++;
					}

					auto teammatesGame = game["ownTeamSteam64Ids"].get<std::vector<std::string>>();

					for (const auto &teammateGame : teammatesGame)
					{
						auto teammateSteamID = std::stoull(teammateGame);

						if (teammateSteamID != user->steamID.ConvertToUint64())
						{
							user->teammates.insert(teammateSteamID);
						}
					}
				}

				user->winRate = (float)wins / (games.size() - ties) * 100.0f;

				if (json["teammates"].is_array())
				{
					auto teammates = json["teammates"].get<std::vector<nlohmann::json>>();

					for (const auto &teammate : teammates)
					{
						user->teammates.insert(std::stoull(teammate["steam64Id"].get<std::string>()));
					}
				}

				user->success = true;
			}
			catch (const std::exception &e)
			{
				printf("fail: %llu ", user->steamID.ConvertToUint64());
				printf("error %s\n", e.what());
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
