add_rules("mode.debug", "mode.release")
add_requires("nlohmann_json", "tabulate")
add_requires("libcurl", {configs = {openssl = true}})

target("PlayerFetch")
	set_kind("binary")
	add_files("src/**.cpp")
	add_headerfiles("src/**.h")
	add_packages("nlohmann_json", "tabulate", "libcurl")

	add_includedirs({
		"vendor/steam/public",
	})

	if is_plat("windows") then
		add_links("advapi32", "shell32")
	end

	if is_plat("linux") then
		add_rpathdirs("~/.steam/steam/linux64/")
	end

	set_languages("cxx20")
	set_exceptions("cxx")
