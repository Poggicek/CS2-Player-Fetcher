workspace "PlayerFetch"
   configurations { "Debug", "Release" }
	 platforms { "x64" }

project "PlayerFetch"
	kind "ConsoleApp"
	language "C++"
	location "build"
	targetdir "bin/%{cfg.buildcfg}"
	cppdialect "C++20"

	files { "src/**.h", "src/**.cpp" }

	vpaths {
		["Headers/*"] = "src/**.h",
		["Sources/*"] = "src/**.cpp"
	}

	filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "On"

	filter "configurations:Release"
		defines { "NDEBUG" }
		optimize "On"

	filter {}

	includedirs {
		"vendor/steam/public"
	}