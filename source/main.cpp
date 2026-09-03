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

	void PushReply(GarrysMod::Lua::ILuaBase* LUA, const cpp_redis::reply& reply)
	{
		switch (reply.get_type())
		{
		case cpp_redis::reply::type::error:
		case cpp_redis::reply::type::bulk_string:
		case cpp_redis::reply::type::simple_string:
			LUA->PushString(reply.as_string().c_str());
			break;

		case cpp_redis::reply::type::integer:
			LUA->PushNumber(static_cast<double>(reply.as_integer()));
			break;

		case cpp_redis::reply::type::array:
		{
			LUA->CreateTable();

			int i = 1;
			for (const auto& row : reply.as_array())
			{
				LUA->PushNumber(i++);
				PushReply(LUA, row);
				LUA->SetTable(-3);
			}
			break;
		}

		case cpp_redis::reply::type::null:
		default:
			LUA->PushNil();
			break;
		}
	}

	int Exception(GarrysMod::Lua::ILuaBase* LUA, int callbackRef, const cpp_redis::redis_error& e)
	{
		LUA->ReferenceFree(callbackRef);
		LUA->PushNil();
		LUA->PushString(e.what());
		return 2;
	}

	int GetCallback(GarrysMod::Lua::ILuaBase* LUA, int stackPos)
	{
		LUA->CheckType(stackPos, GarrysMod::Lua::Type::FUNCTION);
		LUA->Push(stackPos);

		return LUA->ReferenceCreate();
	}

	int GetCallbackOptional(GarrysMod::Lua::ILuaBase* LUA, int stackPos)
	{
		if (LUA->Top() >= stackPos && !LUA->IsType(stackPos, GarrysMod::Lua::Type::NIL))
		{
			LUA->CheckType(stackPos, GarrysMod::Lua::Type::FUNCTION);
			LUA->Push(stackPos);

			return LUA->ReferenceCreate();
		}

		return GarrysMod::Lua::Type::NONE;
	}
};

