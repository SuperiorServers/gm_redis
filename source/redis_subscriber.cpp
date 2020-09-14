#include "main.hpp"
#include "redis_subscriber.h"

void redis::subscriber::Initialize(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface::InitMetatable(LUA, "redis_subscriber");

	LUA->PushCFunction(wrap(lua_Subscribe));
	LUA->SetField(-2, "Subscribe");

	LUA->PushCFunction(wrap(lua_Unsubscribe));
	LUA->SetField(-2, "Unsubscribe");

	LUA->PushCFunction(wrap(lua_PSubscribe));
	LUA->SetField(-2, "PSubscribe");

	LUA->PushCFunction(wrap(lua_PUnsubscribe));
	LUA->SetField(-2, "PUnsubscribe");

	LUA->Pop();
}

void redis::subscriber::HandleAction(GarrysMod::Lua::ILuaBase* LUA, subAction action)
{
	if (action.type == redis::globals::actionType::Message)
	{
		LUA->ReferencePush(redis::globals::iRefDebugTraceBack);
		if (redis::PushCallback(LUA, m_refOnMessage, 1, "OnMessage"))
		{
			LUA->Push(1);
				LUA->PushString(action.data.channel.c_str());
				LUA->PushString(action.data.message.c_str());

				if (LUA->PCall(3, 0, -5) != 0)
					redis::ErrorNoHalt(LUA, "[redis OnMessage callback error] ");
		}
		else
			LUA->Pop();
	}
}


int redis::subscriber::lua_Subscribe(GarrysMod::Lua::ILuaBase* LUA)
{
	subscriber* ptr = GetSubscriber(LUA, 1, true);
	const char* channel = LUA->CheckString(2);

	try
	{
		ptr->m_iface.subscribe(channel, [ptr](const std::string& channel, const std::string& message)
			{
				ptr->EnqueueAction({ redis::globals::actionType::Message, channel, message });
			});
	}
	catch (const cpp_redis::redis_error& e)
	{
		LUA->PushNil();
		LUA->PushString(e.what());
		return 2;
	}

	LUA->PushBool(true);
	return 1;
}

int redis::subscriber::lua_PSubscribe(GarrysMod::Lua::ILuaBase* LUA)
{
	subscriber* ptr = GetSubscriber(LUA, 1, true);
	const char* channel = LUA->CheckString(2);

	try
	{
		ptr->m_iface.psubscribe(channel, [ptr](const std::string& channel, const std::string& message)
			{
				ptr->EnqueueAction({ redis::globals::actionType::Message, channel, message });
			});
	}
	catch (const cpp_redis::redis_error& e)
	{
		LUA->PushNil();
		LUA->PushString(e.what());
		return 2;
	}

	LUA->PushBool(true);
	return 1;
}

int redis::subscriber::lua_Unsubscribe(GarrysMod::Lua::ILuaBase* LUA)
{
	subscriber* ptr = GetSubscriber(LUA, 1, true);
	const char* channel = LUA->CheckString(2);

	try
	{
		ptr->m_iface.unsubscribe(channel);
	}
	catch (const cpp_redis::redis_error& e)
	{
		LUA->PushNil();
		LUA->PushString(e.what());
		return 2;
	}

	LUA->PushBool(true);
	return 1;
}

int redis::subscriber::lua_PUnsubscribe(GarrysMod::Lua::ILuaBase* LUA)
{
	subscriber* ptr = GetSubscriber(LUA, 1, true);
	const char* channel = LUA->CheckString(2);

	try
	{
		ptr->m_iface.punsubscribe(channel);
	}
	catch (const cpp_redis::redis_error& e)
	{
		LUA->PushNil();
		LUA->PushString(e.what());
		return 2;
	}

	LUA->PushBool(true);
	return 1;
}