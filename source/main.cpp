#include "main.hpp"
#include "redis_client.h"
#include "redis_subscriber.h"

namespace redis
{
	namespace globals
	{
		extern int				iRefErrorNoHalt = 0;
		extern int				iRefDebugTraceBack = 0;
	}

	bool PushCallback(GarrysMod::Lua::ILuaBase* LUA, int ref, int idx, const char* field)
	{
		if (ref > 0)
		{
			LUA->ReferencePush(ref);
			return true;
		}

		// Get metatable entry
		LUA->GetMetaTable(idx);
		LUA->PushString(field);
		LUA->RawGet(-2);
		if (LUA->IsType(-1, GarrysMod::Lua::Type::FUNCTION))
		{
			LUA->Remove(-2);
			return true;
		}


		LUA->Pop(2);
		// Should be cached, but still try
		LUA->GetFEnv(idx);
		LUA->PushString(field);
		LUA->RawGet(-2);
		if (LUA->IsType(-1, GarrysMod::Lua::Type::FUNCTION))
		{
			LUA->Remove(-2);
			return true;
		}

		LUA->Pop(2);
		return false;
	}

	void ErrorNoHalt(GarrysMod::Lua::ILuaBase* LUA, const char* msg)
	{
		const char* err = LUA->GetString(-1);
		LUA->ReferencePush(redis::globals::iRefErrorNoHalt);
		LUA->PushString(msg);
		LUA->PushString(err);
		LUA->PushString("\n");
		LUA->Call(3, 0);
		LUA->Pop();
	};
};

