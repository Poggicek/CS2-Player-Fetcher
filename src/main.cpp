#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <algorithm>
#include <curl/curl.h>
#include <d3d11.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <windows.h>

#include "leetify_provider.h"
#include "main.h"

// Forward declare message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// DirectX 11 Variables
ID3D11Device *g_pd3dDevice = nullptr;
ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
IDXGISwapChain *g_pSwapChain = nullptr;
ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;

// Forward declarations for helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Colors for ImGui
namespace Colors
{
const ImVec4 Yellow = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
const ImVec4 Green = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
const ImVec4 Red = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
const ImVec4 Magenta = ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
const ImVec4 Blue = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
const ImVec4 Cyan = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
const ImVec4 White = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
} // namespace Colors

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

std::string roundTo(float value, int decimalPlaces)
{
	std::stringstream stream;
	stream << std::fixed << std::setprecision(decimalPlaces) << value;
	return stream.str();
}

void OpenPlayerProfilesInBrowser(const std::vector<Player> &players)
{
	for (const auto &player : players)
	{
		std::string url = "https://leetify.com/app/profile/" + std::to_string(player.playerSteamID.ConvertToUint64());
		ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
}

std::string GetConsolasFontPath()
{
	HKEY hKey;
	const char *subKey = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
	{
		return "";
	}

	char fontPath[MAX_PATH];
	DWORD size = sizeof(fontPath);
	DWORD type;

	// Query the registry for the Consolas font
	if (RegQueryValueExA(hKey, "Consolas (TrueType)", nullptr, &type, (LPBYTE)fontPath, &size) == ERROR_SUCCESS)
	{
		RegCloseKey(hKey);

		// The registry only provides the filename, so we prepend the Windows Fonts directory
		std::string fullPath = std::string(getenv("WINDIR")) + "\\Fonts\\" + fontPath;
		return fullPath;
	}

	RegCloseKey(hKey);
	return "";
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
	// Create application window
	WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL,
	                 "Leetify Stats",    NULL};
	RegisterClassEx(&wc);
	HWND hwnd = CreateWindow(wc.lpszClassName, "Leetify Stats", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL,
	                         wc.hInstance, NULL);

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// Show the window
	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

	// Setup ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

	// Setup ImGui style
	ImGui::StyleColorsDark();

	std::string fontPath = GetConsolasFontPath();
	if (!fontPath.empty())
	{
		ImGuiIO &io = ImGui::GetIO();
		io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f);
	}

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	// Steam API initialization
	CustomSteamAPIInit();

	// Get player data
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
			return a.playerSteamID > b.playerSteamID;
		}

		return a.time > b.time;
	});

	int iHighestTimeStamp;

	if (players.size() > 0)
	{
		iHighestTimeStamp = players[min(5, static_cast<int>(players.size()) - 1)].time;
	}

	std::erase_if(players, [iHighestTimeStamp](const Player &player) { return iHighestTimeStamp > player.time; });

	if (players.size() > 9)
	{
		players.erase(players.begin() + 9, players.end());
	}

	// Get Leetify data
	std::vector<std::thread> threads;
	std::vector<LeetifyUser> leetifyUsers;
	std::mutex mtx;

	curl_global_init(CURL_GLOBAL_DEFAULT);

	for (const auto &player : players)
	{
		threads.emplace_back([&player, &mtx, &leetifyUsers]() {
			auto leetifyUser = GetLeetifyUser(player.playerSteamID.ConvertToUint64());
			std::lock_guard<std::mutex> lock(mtx);
			leetifyUsers.push_back(leetifyUser);
		});
	}

	for (auto &thread : threads)
	{
		thread.join();
	}

	curl_global_cleanup();

	// Helper function to find a user by Steam ID
	auto findUserBySteamID = [&leetifyUsers](uint64 steamID) {
		return std::find_if(leetifyUsers.begin(), leetifyUsers.end(),
		                    [steamID](const LeetifyUser &u) { return u.steamID.ConvertToUint64() == steamID; });
	};

	// Sort users by lobby ID and then by Leetify rating
	std::sort(leetifyUsers.begin(), leetifyUsers.end(), [](const LeetifyUser &a, const LeetifyUser &b) {
		if (b.recentGameRatings.leetifyRating != a.recentGameRatings.leetifyRating)
		{
			return a.recentGameRatings.leetifyRating > b.recentGameRatings.leetifyRating;
		}

		return a.steamID > b.steamID;
	});

	// Main loop
	bool done = false;
	bool openProfiles = false;

	while (!done)
	{
		// Poll and handle messages
		MSG msg;
		while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
			{
				done = true;
			}
		}
		if (done)
		{
			break;
		}

		// Start the ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// Create main window
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		ImGui::Begin("Leetify Stats", NULL,
		             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		                 ImGuiWindowFlags_NoTitleBar);

		// Table for player data
		const float TEXT_BASE_WIDTH = ImGui::GetFontSize();
		const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();

		ImGui::Text("Leetify Stats for Recent Players");
		ImGui::Separator();

		if (ImGui::Button("Open All Profiles in Browser"))
		{
			openProfiles = true;
		}

		// Table with fixed headers
		static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
		                               ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable |
		                               ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;

		if (ImGui::BeginTable("LeetifyTable", 10, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
		{
			// Declare columns
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Leetify", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 8.0f);
			ImGui::TableSetupColumn("Premier", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 7.0f);
			ImGui::TableSetupColumn("Aim", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 6.0f);
			ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 6.0f);
			ImGui::TableSetupColumn("Util", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 6.0f);
			ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 6.0f);
			ImGui::TableSetupColumn("Matches", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 8.0f);
			ImGui::TableSetupColumn("FACEIT", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Teammates", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableHeadersRow();

			for (const auto &user : leetifyUsers)
			{
				ImGui::TableNextRow();

				// Get player name
				auto playerName = std::string(g_pSteamFriends->GetFriendPersonaName(user.steamID));
				auto steamID = user.steamID.ConvertToUint64();

				if (playerName == "" || playerName == "[unknown]")
				{
					playerName = user.name;
				}

				// Collect teammates
				std::vector<std::string> teammates{};
				for (const auto &otherUser : leetifyUsers)
				{
					if (otherUser.teammates.contains(steamID))
					{
						auto teammateName = std::string(g_pSteamFriends->GetFriendPersonaName(otherUser.steamID));

						if (teammateName == "" || teammateName == "[unknown]")
						{
							teammateName = otherUser.name;
						}

						teammates.push_back(teammateName);
					}
				}

				std::ostringstream teammatesStr;
				for (size_t i = 0; i < teammates.size(); ++i)
				{
					if (i != 0)
					{
						teammatesStr << ", ";
					}
					teammatesStr << teammates[i];
				}

				// Name column with hyperlink behavior
				ImGui::TableSetColumnIndex(0);
				ImGui::PushID(static_cast<int>(steamID & 0xFFFFFFFF));
				if (ImGui::Selectable(playerName.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
				{
					std::string url = "https://leetify.com/app/profile/" + std::to_string(steamID);
					ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				ImGui::PopID();

				if (!user.success)
				{
					ImGui::TableSetColumnIndex(1);
					ImGui::TextColored(Colors::Magenta, "N/A");
					ImGui::TableSetColumnIndex(2);
					ImGui::TextColored(Colors::Magenta, "N/A");
					ImGui::TableSetColumnIndex(3);
					ImGui::TextColored(Colors::Magenta, "N/A");
					ImGui::TableSetColumnIndex(4);
					ImGui::TextColored(Colors::Magenta, "N/A");
					ImGui::TableSetColumnIndex(5);
					ImGui::TextColored(Colors::Magenta, "N/A");
					ImGui::TableSetColumnIndex(6);
					ImGui::TextColored(Colors::Magenta, "N/A");
					ImGui::TableSetColumnIndex(7);
					ImGui::TextColored(Colors::Magenta, "N/A");
					ImGui::TableSetColumnIndex(8);
					ImGui::Text("");
					ImGui::TableSetColumnIndex(9);
					ImGui::Text("%s", teammatesStr.str().c_str());
					continue;
				}

				// Leetify rating with coloring
				auto leetifyRating = user.recentGameRatings.leetifyRating * 100;
				ImGui::TableSetColumnIndex(1);
				std::string ratingText =
				    (user.recentGameRatings.leetifyRating >= 0.0 ? "+" : "") + roundTo(leetifyRating, 2);

				if (leetifyRating >= 5)
				{
					ImGui::TextColored(Colors::Yellow, "%s", ratingText.c_str());
				}
				else if (leetifyRating >= 1)
				{
					ImGui::TextColored(Colors::Green, "%s", ratingText.c_str());
				}
				else if (leetifyRating <= -1)
				{
					ImGui::TextColored(Colors::Red, "%s", ratingText.c_str());
				}
				else
				{
					ImGui::Text("%s", ratingText.c_str());
				}

				// Premier level with coloring
				ImGui::TableSetColumnIndex(2);
				if (user.skillLevel <= 0)
				{
					ImGui::Text("N/A");
				}
				else
				{
					if (user.skillLevel >= 30000)
					{
						ImGui::TextColored(Colors::Yellow, "%d", user.skillLevel);
					}
					else if (user.skillLevel >= 25000)
					{
						ImGui::TextColored(Colors::Red, "%d", user.skillLevel);
					}
					else if (user.skillLevel >= 20000)
					{
						ImGui::TextColored(Colors::Magenta, "%d", user.skillLevel);
					}
					else if (user.skillLevel >= 15000)
					{
						ImGui::TextColored(Colors::Blue, "%d", user.skillLevel);
					}
					else if (user.skillLevel >= 10000)
					{
						ImGui::TextColored(Colors::Cyan, "%d", user.skillLevel);
					}
					else
					{
						ImGui::Text("%d", user.skillLevel);
					}
				}

				// Aim rating with coloring
				ImGui::TableSetColumnIndex(3);
				if (user.recentGameRatings.aim >= 85)
				{
					ImGui::TextColored(Colors::Red, "%d", (int)user.recentGameRatings.aim);
				}
				else if (user.recentGameRatings.aim >= 60)
				{
					ImGui::TextColored(Colors::Green, "%d", (int)user.recentGameRatings.aim);
				}
				else
				{
					ImGui::Text("%d", (int)user.recentGameRatings.aim);
				}

				// Positioning rating with coloring
				ImGui::TableSetColumnIndex(4);
				if (user.recentGameRatings.positioning >= 60)
				{
					ImGui::TextColored(Colors::Green, "%d", (int)user.recentGameRatings.positioning);
				}
				else
				{
					ImGui::Text("%d", (int)user.recentGameRatings.positioning);
				}

				// Utility rating with coloring
				ImGui::TableSetColumnIndex(5);
				if (user.recentGameRatings.utility >= 60)
				{
					ImGui::TextColored(Colors::Green, "%d", (int)user.recentGameRatings.utility);
				}
				else
				{
					ImGui::Text("%d", (int)user.recentGameRatings.utility);
				}

				// Win rate with coloring
				ImGui::TableSetColumnIndex(6);
				if (user.winRate >= 55)
				{
					ImGui::TextColored(Colors::Green, "%d%%", (int)user.winRate);
				}
				else if (user.winRate <= 45)
				{
					ImGui::TextColored(Colors::Red, "%d%%", (int)user.winRate);
				}
				else
				{
					ImGui::Text("%d%%", (int)user.winRate);
				}

				// Matches
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%d", user.matches);

				// FACEIT info with coloring
				ImGui::TableSetColumnIndex(8);
				std::string faceitInfo =
				    (user.faceitElo > 0 ? "[" + std::to_string(user.faceitElo) + "] " : "") + user.faceitNickname;
				if (user.faceitElo >= 2001)
				{
					ImGui::TextColored(Colors::Red, "%s", faceitInfo.c_str());
				}
				else if (user.faceitElo >= 1701)
				{
					ImGui::TextColored(Colors::Magenta, "%s", faceitInfo.c_str());
				}
				else
				{
					ImGui::Text("%s", faceitInfo.c_str());
				}

				// Teammates
				ImGui::TableSetColumnIndex(9);
				ImGui::Text("%s", teammatesStr.str().c_str());
			}

			ImGui::EndTable();
		}

		ImGui::End();

		// Rendering
		ImGui::Render();
		const float clear_color_with_alpha[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		g_pSwapChain->Present(1, 0);

		// Handle opening profiles
		if (openProfiles)
		{
			OpenPlayerProfilesInBrowser(players);
			openProfiles = false;
		}
	}

	// Cleanup
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	DestroyWindow(hwnd);
	UnregisterClass(wc.lpszClassName, wc.hInstance);

	CustomSteamAPIShutdown();
	return 0;
}

// Helper functions for D3D and ImGui

bool CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	// createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

	if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2,
	                                  D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel,
	                                  &g_pd3dDeviceContext) != S_OK)
	{
		return false;
	}

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain)
	{
		g_pSwapChain->Release();
		g_pSwapChain = NULL;
	}
	if (g_pd3dDeviceContext)
	{
		g_pd3dDeviceContext->Release();
		g_pd3dDeviceContext = NULL;
	}
	if (g_pd3dDevice)
	{
		g_pd3dDevice->Release();
		g_pd3dDevice = NULL;
	}
}

void CreateRenderTarget()
{
	ID3D11Texture2D *pBackBuffer;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
	pBackBuffer->Release();
}

void CleanupRenderTarget()
{
	if (g_mainRenderTargetView)
	{
		g_mainRenderTargetView->Release();
		g_mainRenderTargetView = NULL;
	}
}

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
	{
		return true;
	}

	switch (msg)
	{
	case WM_SIZE:
		if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
		{
			CleanupRenderTarget();
			g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
			CreateRenderTarget();
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
		{
			return 0;
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}
