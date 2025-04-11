# CS2 Player Fetcher

Retrieves and displays information about Counter-Strike 2 players you've recently played with.

This program works by asking your Steam client for "coplay" friends, sorts the result by time, and takes up to 9 players to fetch stats for. Unfortunately some players may be missing from time to time, this is a Steam issue.

Then for every steamid it fetches data from [Leetify](https://leetify.com/) public API, which was created with our feedback.

At the end, displays information such as Leetify rating, Premier rank, aim/positioning/utility scores, time to damage, crosshair placement, headshot percentage, win rate, matches, and bans.

If available, it will also try to group players into lobbies and display their recent teammates.

This project is not affiliated with or endorsed by Valve or Leetify. Use at your own risk.

## Download

[Download latest release here.](https://github.com/Poggicek/CS2-Player-Fetcher/releases/latest)

It may be outdated, so you can use [this nightly link](https://nightly.link/Poggicek/CS2-Player-Fetcher/workflows/build/master/artifact.zip) to download the latest build.
