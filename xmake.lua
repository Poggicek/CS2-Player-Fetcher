add_rules("mode.debug", "mode.release")
add_requires("nlohmann_json")
add_requires("libcurl")
add_requires("ftxui")

target("PlayerFetch")
	set_kind("binary")
	add_files("src/**.cpp")
	add_headerfiles("src/**.h")
	add_packages("nlohmann_json", "ftxui", "libcurl")

	add_includedirs({
		"vendor/steam/public",
	})

	if is_plat("windows") then
		add_files("icon.rc")
		add_links("shell32")
	end

	set_languages("cxx20")
	set_exceptions("cxx")
