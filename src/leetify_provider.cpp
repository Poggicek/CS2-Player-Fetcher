#include <format>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <algorithm>

#include "leetify_provider.h"


static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp)
{
	userp->append((char*)contents, size * nmemb);
	return size * nmemb;
}

LeetifyUser GetLeetifyUser(CSteamID steamID)
{
	LeetifyUser user;
	user.steamID = steamID;

	CURL* curl = curl_easy_init();
	if (curl)
	{
		std::string response;
		auto url = std::format("https://api.leetify.com/api/profile/{}", steamID.ConvertToUint64());

		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

		CURLcode res = curl_easy_perform(curl);;

		if (res == CURLE_OK)
		{
			long response_code;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

			if (response_code == 200)
			{
				try {
					auto json = nlohmann::json::parse(response.c_str());

					user.recentGameRatings.aim = json["recentGameRatings"]["aim"].get<float>();
					user.recentGameRatings.positioning = json["recentGameRatings"]["positioning"].get<float>();
					user.recentGameRatings.utility = json["recentGameRatings"]["utility"].get<float>();
					user.recentGameRatings.leetifyRating = json["recentGameRatings"]["leetify"].get<float>();

					if (json["meta"].contains("faceitNickname"))
					{
						user.faceitNickname = json["meta"]["faceitNickname"].get<std::string>();
					}

					auto games = json["games"].get<std::vector<nlohmann::json>>();

					user.matches = games.size();

					auto mmGames = std::find_if(games.begin(), games.end(), [&](const nlohmann::json& game) {
						return game["dataSource"].get<std::string>() == "matchmaking" && !game["rankType"].is_null() && game.value("rankType", -1) == 11;
					});

					if (mmGames != games.end())
					{
						user.skillLevel = mmGames[0].at("skillLevel").get<int>();
					}

					if (games.size() > 30)
						games.erase(games.begin() + 30, games.end());

					int wins = 0;
					int ties = 0;

					for (const auto& game : games)
					{
						auto matchResult = game["matchResult"].get<std::string>();
						if (matchResult == "win")
							wins++;
						else if (matchResult == "tie")
							ties++;

						auto teammatesGame = game["ownTeamSteam64Ids"].get<std::vector<std::string>>();

						for (const auto& teammateGame : teammatesGame)
						{
							auto teammateSteamID = std::stoull(teammateGame);

							if (CSteamID(teammateSteamID) != steamID)
							{
								user.teammates.insert(teammateSteamID);
							}
						}
					}

					user.winRate = (float)wins / (games.size() - ties) * 100.0f;

					auto teammates = json["teammates"].get<std::vector<nlohmann::json>>();

					for (const auto& teammate : teammates)
					{
						user.teammates.insert(std::stoull(teammate["steam64Id"].get<std::string>()));
					}

					user.success = true;
				}
				catch (const std::exception& e)
				{
					printf("error %s\n", e.what());
				}
			}
		}

		curl_easy_cleanup(curl);
	}

	return user;
}