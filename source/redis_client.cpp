#include "main.hpp"
#include "redis_client.h"

void redis::client::Initialize(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface::InitMetatable(LUA, "redis_client");

	LUA->PushCFunction(wrap(lua_Send));
	LUA->SetField(-2, "Send");

	LUA->PushCFunction(wrap(lua_Ping));
	LUA->SetField(-2, "Ping");
	LUA->PushCFunction(wrap(lua_Auth));
	LUA->SetField(-2, "Auth");
	LUA->PushCFunction(wrap(lua_Select));
	LUA->SetField(-2, "Select");
	LUA->PushCFunction(wrap(lua_Publish));
	LUA->SetField(-2, "Publish");

	LUA->PushCFunction(wrap(lua_Exists));
	LUA->SetField(-2, "Exists");
	LUA->PushCFunction(wrap(lua_Delete));
	LUA->SetField(-2, "Delete");

	LUA->PushCFunction(wrap(lua_Get));
	LUA->SetField(-2, "Get");
	LUA->PushCFunction(wrap(lua_Set));
	LUA->SetField(-2, "Set");
	LUA->PushCFunction(wrap(lua_SetEx));
	LUA->SetField(-2, "SetEx");
	LUA->PushCFunction(wrap(lua_TTL));
	LUA->SetField(-2, "TTL");

	LUA->Pop();
}

const char* toString(GarrysMod::Lua::ILuaBase* LUA, int32_t idx, unsigned int* len = nullptr)
{
	if (LUA->CallMeta(idx, "__tostring") == 0)
		switch (LUA->GetType(idx))
		{
		case GarrysMod::Lua::Type::NUMBER:
			LUA->PushFormattedString("%f", LUA->GetNumber(idx));
			break;

		case GarrysMod::Lua::Type::STRING:
			LUA->Push(idx);
			break;

		case GarrysMod::Lua::Type::BOOL:
			LUA->PushString(LUA->GetBool(idx) ? "true" : "false");
			break;

		case GarrysMod::Lua::Type::NIL:
			LUA->PushString("nil");
			break;

		default:
			LUA->PushFormattedString("%s: %p", LUA->GetTypeName(LUA->GetType(idx)), LUA->GetPointer(idx));
			break;
		}

	return LUA->GetString(-1, len);
}

void redis::client::HandleAction(GarrysMod::Lua::ILuaBase* LUA, clientAction action)
{
	if (action.type == redis::globals::actionType::Reply)
	{
		if (action.data.reference > 0)
		{
			LUA->ReferencePush(redis::globals::iRefDebugTraceBack);
			LUA->ReferencePush(action.data.reference);
			LUA->Push(1);

			redis::PushReply(LUA, action.data.reply);

			if (LUA->PCall(2, 0, -4) != 0)
				redis::ErrorNoHalt(LUA, "[redis Send callback error] ");

			LUA->ReferenceFree(action.data.reference);
		}
	}
}

std::vector<std::string> redis::client::GetKeys(GarrysMod::Lua::ILuaBase* LUA, int stackPos)
{
	std::vector<std::string> keys;
	if (LUA->IsType(stackPos, GarrysMod::Lua::Type::TABLE))
	{
		int32_t k = 1;
		do
		{
			LUA->PushNumber(k);
			LUA->GetTable(stackPos);
			if (LUA->IsType(-1, GarrysMod::Lua::Type::NIL))
			{
				LUA->Pop();
				break;
			}

			keys.push_back(toString(LUA, -1));
			LUA->Pop(stackPos);
			++k;
		} while (true);
	}
	else
		keys.push_back(toString(LUA, stackPos));

	return keys;
}


// Send commands directly
int redis::client::lua_Send(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);
	std::vector<std::string> keys = GetKeys(LUA, 2);
	int callbackRef = GetCallbackOptional(LUA, 3);

	try
	{
		if (callbackRef == GarrysMod::Lua::Type::NONE)
			ptr->m_iface.send(keys);
		else
			ptr->m_iface.send(keys, [ptr, callbackRef](cpp_redis::reply& reply)
			{
					ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
			});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/ping/
int redis::client::lua_Ping(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	int callbackRef = GetCallbackOptional(LUA, 2);

	try
	{
		if (callbackRef == GarrysMod::Lua::Type::NONE)
			ptr->m_iface.ping();
		else
			ptr->m_iface.ping([ptr, callbackRef](cpp_redis::reply& reply)
				{
					ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
				});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/auth/
int redis::client::lua_Auth(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	const char* password = LUA->CheckString(2);
	int callbackRef = GetCallbackOptional(LUA, 3);

	try
	{
		if (callbackRef == GarrysMod::Lua::Type::NONE)
			ptr->m_iface.auth(password);
		else
			ptr->m_iface.auth(password, [ptr, callbackRef](cpp_redis::reply& reply)
				{
					ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
				});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/select/
int redis::client::lua_Select(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	int database = LUA->CheckNumber(2);
	int callbackRef = GetCallbackOptional(LUA, 3);

	try
	{
		if (callbackRef == GarrysMod::Lua::Type::NONE)
			ptr->m_iface.select(database);
		else
			ptr->m_iface.select(database, [ptr, callbackRef](cpp_redis::reply& reply)
				{
					ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
				});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/publish/
int redis::client::lua_Publish(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	const char* channel = LUA->CheckString(2);
	const char* message = LUA->CheckString(3);
	int callbackRef = GetCallbackOptional(LUA, 4);

	try
	{
		if (callbackRef == GarrysMod::Lua::Type::NONE)
			ptr->m_iface.publish(channel, message);
		else
			ptr->m_iface.publish(channel, message, [ptr, callbackRef](cpp_redis::reply& reply)
				{
					ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
				});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/exists/
int redis::client::lua_Exists(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	std::vector<std::string> keys = GetKeys(LUA, 2);
	int callbackRef = GetCallback(LUA, 3);

	try
	{
		ptr->m_iface.exists(keys, [ptr, callbackRef](cpp_redis::reply& reply)
			{
				ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
			});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/delete/
int redis::client::lua_Delete(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	std::vector<std::string> keys = GetKeys(LUA, 2);
	int callbackRef = GetCallbackOptional(LUA, 3);

	try
	{
		if (callbackRef == GarrysMod::Lua::Type::NONE)
			ptr->m_iface.del(keys);
		else
			ptr->m_iface.del(keys, [ptr, callbackRef](cpp_redis::reply& reply)
				{
					ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
				});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/get/
int redis::client::lua_Get(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	const char* key = LUA->CheckString(2);
	int callbackRef = GetCallback(LUA, 3);

	try
	{
		ptr->m_iface.get(key, [ptr, callbackRef](cpp_redis::reply& reply)
			{
				ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
			});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/set/
int redis::client::lua_Set(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	const char* key = LUA->CheckString(2);
	const char* value = LUA->CheckString(3);
	int callbackRef = GetCallbackOptional(LUA, 4);

	try
	{
		if (callbackRef == GarrysMod::Lua::Type::NONE)
			ptr->m_iface.set(key, value);
		else
			ptr->m_iface.set(key, value, [ptr, callbackRef](cpp_redis::reply& reply)
				{
					ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
				});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/setex/
int redis::client::lua_SetEx(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	const char* key = LUA->CheckString(2);
	int secondsTtl = LUA->CheckNumber(3);
	const char* value = LUA->CheckString(4);
	int callbackRef = GetCallbackOptional(LUA, 5);

	try
	{
		if (callbackRef == GarrysMod::Lua::Type::NONE)
			ptr->m_iface.setex(key, secondsTtl, value);
		else
			ptr->m_iface.setex(key, secondsTtl, value, [ptr, callbackRef](cpp_redis::reply& reply)
				{
					ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
				});
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}

// https://redis.io/commands/ttl/
int redis::client::lua_TTL(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);

	const char* key = LUA->CheckString(2);
	int callbackRef = GetCallback(LUA, 3);

	try
	{
		ptr->m_iface.ttl(key, [ptr, callbackRef](cpp_redis::reply& reply)
			{
				ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, callbackRef} });
			});	
	}
	catch (const cpp_redis::redis_error& e)
	{
		return Exception(LUA, callbackRef, e);
	}

	LUA->PushBool(true);
	return 1;
}