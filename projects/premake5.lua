newoption({
	trigger = "gmcommon",
	description = "Sets the path to the garrysmod_common (https://github.com/danielga/garrysmod_common) directory",
	value = "path to garrysmod_common directory"
})

local gmcommon = _OPTIONS.gmcommon or os.getenv("GARRYSMOD_COMMON")
if gmcommon == nil then
	error("you didn't provide a path to your garrysmod_common (https://github.com/danielga/garrysmod_common) directory")
end

include(gmcommon)

local REDIS_FOLDER = "../cpp_redis"

CreateWorkspace({name = "redis.core"})
	CreateProject({serverside = true})
		links("cpp_redis")
		includedirs(REDIS_FOLDER .. "/includes")
		IncludeLuaShared()

		filter("system:windows")
			links("ws2_32")

	CreateProject({serverside = false})
		links("cpp_redis")
		includedirs(REDIS_FOLDER .. "/includes")
		IncludeLuaShared()

		filter("system:windows")
			links("ws2_32")

	project("cpp_redis")
		kind("StaticLib")
		includedirs(REDIS_FOLDER .. "/includes")
		files({
			REDIS_FOLDER .. "/sources/*.cpp",
			REDIS_FOLDER .. "/sources/builders/*.cpp",
			REDIS_FOLDER .. "/sources/network/*.cpp",
			REDIS_FOLDER .. "/includes/cpp_redis/**.hpp"
		})
		vpaths({
			["Source files/*"] = REDIS_FOLDER .. "/sources/**.cpp",
			["Header files/*"] = REDIS_FOLDER .. "/includes/cpp_redis/**.hpp"
		})

		filter("system:windows")
			files(REDIS_FOLDER .. "/sources/network/windows_impl/*.cpp")

		filter("system:not windows")
			files(REDIS_FOLDER .. "/sources/network/unix_impl/*.cpp")
