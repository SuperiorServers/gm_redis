#pragma once

#include <cstdint>

namespace GarrysMod
{
	namespace Lua
	{
		class ILuaBase;
	}
}

namespace redis_client
{

	void Initialize(GarrysMod::Lua::ILuaBase* LUA);
	void Deinitialize(GarrysMod::Lua::ILuaBase* LUA);
	LUA_FUNCTION_DECLARE(Create);

}
