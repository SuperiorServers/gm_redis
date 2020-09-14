#pragma once

namespace redis
{
	namespace lua
	{
		static void Initialize(GarrysMod::Lua::ILuaBase* LUA);

		template <class T>
		static int Create(GarrysMod::Lua::ILuaBase* LUA);
	};
};