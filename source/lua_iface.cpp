#include "main.hpp"
#include "lua_iface.h"
#include "redis_client.h"
#include "redis_subscriber.h"

GMOD_MODULE_OPEN()
{
	redis::lua::Initialize(LUA);
	redis::client::Initialize(LUA);
	redis::subscriber::Initialize(LUA);

	return 0;
}

GMOD_MODULE_CLOSE()
{
	LUA->ReferenceFree(redis::globals::iRefDebugTraceBack);
	LUA->ReferenceFree(redis::globals::iRefErrorNoHalt);

	return 0;
}

static void redis::lua::Initialize(GarrysMod::Lua::ILuaBase* LUA)
{
	LUA->GetField(GarrysMod::Lua::INDEX_GLOBAL, "ErrorNoHalt");
	redis::globals::iRefErrorNoHalt = LUA->ReferenceCreate();

	LUA->GetField(GarrysMod::Lua::INDEX_GLOBAL, "debug");
	LUA->GetField(-1, "traceback");
	redis::globals::iRefDebugTraceBack = LUA->ReferenceCreate();
	LUA->Pop();

	LUA->CreateTable();

	LUA->PushNumber(MODULE_VERSION);
	LUA->SetField(-2, "Version");

	LUA->PushCFunction(wrap(redis::lua::Create<redis::client>));
	LUA->SetField(-2, "CreateClient");

	LUA->PushCFunction(wrap(redis::lua::Create<redis::subscriber>));
	LUA->SetField(-2, "CreateSubscriber");

	LUA->SetField(GarrysMod::Lua::INDEX_GLOBAL, "redis");
}

template <class T>
static int redis::lua::Create(GarrysMod::Lua::ILuaBase* LUA)
{
	try
	{
		T* iface = new T(LUA);

		return 1;
	}
	catch (std::exception& e)
	{
		LUA->PushNil();
		LUA->PushString(e.what());
	}
	catch (...)
	{
		LUA->PushNil();
		LUA->PushString("Unknown exception occurred");
	}

	return 2;
}