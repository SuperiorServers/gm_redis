#pragma once

#define MODULE_VERSION 1.21

#include <cpp_redis/cpp_redis>
#include <GarrysMod/Lua/Interface.h>
#include "readerwriterqueue.hpp"
#include <cstring>

#define DerivedInterfaceMethod(ret) template <class actionStruct, class redisInterface> ret redis::BaseInterface<actionStruct, redisInterface>
#define wrap(Fn) [](lua_State* L) -> int { GarrysMod::Lua::ILuaBase* LUA = L->luabase; LUA->SetState(L); return Fn(LUA); }

namespace redis
{
	namespace globals
	{
		extern int			iRefErrorNoHalt;
		extern int			iRefDebugTraceBack;

		enum class actionType
		{
			Connection,
			Disconnection,
			Reply,
			Publish,
			Message
		};
	}

	void ErrorNoHalt(GarrysMod::Lua::ILuaBase* LUA, const char* msg);
	bool PushCallback(GarrysMod::Lua::ILuaBase* LUA, int ref, int idx, const char* field);

	template <typename actionData>
	struct action {
		globals::actionType	type;
		actionData	data;
	};

	template <class actionStruct, class redisInterface>
	class BaseInterface {
	public:
		BaseInterface(GarrysMod::Lua::ILuaBase* LUA);

		static int lua__eq(GarrysMod::Lua::ILuaBase* LUA);
		static int lua__tostring(GarrysMod::Lua::ILuaBase* LUA);
		static int lua__index(GarrysMod::Lua::ILuaBase* LUA);
		static int lua__newindex(GarrysMod::Lua::ILuaBase* LUA);
		static int lua__gc(GarrysMod::Lua::ILuaBase* LUA);

		static int lua_IsValid(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_IsConnected(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_IsReconnecting(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Connect(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Disconnect(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Poll(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Commit(GarrysMod::Lua::ILuaBase* LUA);

		bool EnqueueAction(const actionStruct& action) { return m_queue.enqueue(action); }
		bool DequeueAction(actionStruct& action) { return m_queue.try_dequeue(action); }
	protected:
		inline static int	m_metaTableID = 0;
		inline static char* m_metaTableName = 0;
		int					m_refOnConnected = 0;
		int					m_refOnDisconnected = 0;
		int					m_refOnMessage = 0;

		static BaseInterface* Get(GarrysMod::Lua::ILuaBase* LUA, int index, bool throwNullError) { return static_cast<BaseInterface*>(_get(LUA, index, throwNullError)); }
		static void* _get(GarrysMod::Lua::ILuaBase* LUA, int index, bool throwNullError);

		static void InitMetatable(GarrysMod::Lua::ILuaBase* LUA, const char* mtName);
		static void CheckType(GarrysMod::Lua::ILuaBase* LUA, int index);
		virtual void HandleAction(GarrysMod::Lua::ILuaBase* LUA, actionStruct action) { }

		redisInterface m_iface;
		moodycamel::ReaderWriterQueue<actionStruct> m_queue;
	};
};


#pragma region BaseInterface
DerivedInterfaceMethod()::BaseInterface(GarrysMod::Lua::ILuaBase* LUA)
{
	LUA->PushUserType(this, m_metaTableID);
	LUA->PushMetaTable(m_metaTableID);
	LUA->SetMetaTable(-2);

	LUA->CreateTable();
	LUA->SetFEnv(-2);
}

DerivedInterfaceMethod(void)::InitMetatable(GarrysMod::Lua::ILuaBase* LUA, const char* mtName)
{
	m_metaTableName = const_cast<char*>(mtName);
	m_metaTableID = LUA->CreateMetaTable(mtName);

	LUA->PushCFunction(wrap(lua__tostring));
	LUA->SetField(-2, "__tostring");

	LUA->PushCFunction(wrap(lua__eq));
	LUA->SetField(-2, "__eq");

	LUA->PushCFunction(wrap(lua__index));
	LUA->SetField(-2, "__index");

	LUA->PushCFunction(wrap(lua__newindex));
	LUA->SetField(-2, "__newindex");

	LUA->PushCFunction(wrap(lua__gc));
	LUA->SetField(-2, "__gc");

	LUA->PushCFunction(wrap(lua__gc));
	LUA->SetField(-2, "Destroy");

	LUA->PushCFunction(wrap(lua_IsValid));
	LUA->SetField(-2, "IsValid");

	LUA->PushCFunction(wrap(lua_IsConnected));
	LUA->SetField(-2, "IsConnected");

	LUA->PushCFunction(wrap(lua_IsReconnecting));
	LUA->SetField(-2, "IsReconnecting");

	LUA->PushCFunction(wrap(lua_Connect));
	LUA->SetField(-2, "Connect");

	LUA->PushCFunction(wrap(lua_Disconnect));
	LUA->SetField(-2, "Disconnect");

	LUA->PushCFunction(wrap(lua_Poll));
	LUA->SetField(-2, "Poll");

	LUA->PushCFunction(wrap(lua_Commit));
	LUA->SetField(-2, "Commit");
}

DerivedInterfaceMethod(void*)::_get(GarrysMod::Lua::ILuaBase* LUA, int index, bool throwNullError)
{
	void* iface = LUA->GetUserType<void*>(index, m_metaTableID);
	if (iface == nullptr && throwNullError)
	{
		char err[37];
		sprintf(err, "Tried to use a NULL %s", m_metaTableName);
		LUA->ThrowError(err);
	}

	return iface;
}

DerivedInterfaceMethod(void)::CheckType(GarrysMod::Lua::ILuaBase* LUA, int index)
{
	LUA->CheckType(index, m_metaTableID);
}

DerivedInterfaceMethod(int)::lua__eq(GarrysMod::Lua::ILuaBase* LUA)
{
	LUA->PushBool(Get(LUA, 1, false) == Get(LUA, 2, false));
	return 1;
}

DerivedInterfaceMethod(int)::lua__tostring(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, false);

	if (ptr == nullptr)
		LUA->PushFormattedString("[NULL %s]", m_metaTableName);
	else
		LUA->PushFormattedString("[%s: 0x%p]", m_metaTableName, ptr);

	return 1;
}

DerivedInterfaceMethod(int)::lua__index(GarrysMod::Lua::ILuaBase* LUA)
{
	CheckType(LUA, 1);

	LUA->PushMetaTable(m_metaTableID);
	LUA->Push(2);
	LUA->RawGet(-2);
	if (!LUA->IsType(-1, GarrysMod::Lua::Type::NIL))
		return 1;

	LUA->Pop(2);

	LUA->GetFEnv(1);
	LUA->Push(2);
	LUA->RawGet(-2);
	return 1;
}

DerivedInterfaceMethod(int)::lua__newindex(GarrysMod::Lua::ILuaBase* LUA)
{
	CheckType(LUA, 1);
	BaseInterface* ptr = Get(LUA, 1, true);

	if (LUA->GetType(2) == GarrysMod::Lua::Type::String)
	{
		const char* key = LUA->GetString(2);
		int* ref = nullptr;

		if (strcmp(key, "OnConnected") == 0)
			ref = &ptr->m_refOnConnected;
		else if (strcmp(key, "OnDisconnected") == 0)
			ref = &ptr->m_refOnDisconnected;
		else if (strcmp(key, "OnMessage") == 0)
			ref = &ptr->m_refOnMessage;

		if (ref)
		{
			if (*ref > 0)
				LUA->ReferenceFree(*ref);

			LUA->Push(3);
			*ref = LUA->ReferenceCreate();
		}
	}

	LUA->GetFEnv(1);
	LUA->Push(2);
	LUA->Push(3);
	LUA->RawSet(-3);
	return 0;
}

DerivedInterfaceMethod(int)::lua__gc(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, false);

	if (ptr != nullptr)
		delete ptr,
		LUA->SetUserType(1, nullptr);

	return 0;
}

DerivedInterfaceMethod(int)::lua_IsValid(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, false);

	LUA->PushBool(ptr != nullptr);
	return 1;
}

DerivedInterfaceMethod(int)::lua_IsConnected(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, true);

	LUA->PushBool(ptr->m_iface.is_connected());
	return 1;
}

DerivedInterfaceMethod(int)::lua_IsReconnecting(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, true);

	LUA->PushBool(ptr->m_iface.is_reconnecting());
	return 1;
}

DerivedInterfaceMethod(int)::lua_Connect(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, true);

	const char* host = LUA->CheckString(2);
	size_t port = static_cast<size_t>(LUA->CheckNumber(3));

	int timeoutMs = 250;
	if (LUA->IsType(4, GarrysMod::Lua::Type::Number))
		timeoutMs = LUA->GetNumber(4);

	int maxReconnects = -1;
	if (LUA->IsType(5, GarrysMod::Lua::Type::Number))
		maxReconnects = LUA->GetNumber(5);

	int reconnectIntervalMs = 250;
	if (LUA->IsType(6, GarrysMod::Lua::Type::Number))
		reconnectIntervalMs = LUA->GetNumber(6);

	try
	{
		ptr->m_iface.connect(host, port, [ptr](auto, auto, auto status)
			{
				using state = cpp_redis::connect_state;

				if (
					status == state::dropped ||
					status == state::failed ||
					status == state::lookup_failed ||
					status == state::stopped
					)
					ptr->EnqueueAction({ globals::actionType::Disconnection });
				else if (status == state::ok)
					ptr->EnqueueAction({ globals::actionType::Connection });

			}, timeoutMs, maxReconnects, reconnectIntervalMs);
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

DerivedInterfaceMethod(int)::lua_Disconnect(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, true);

	ptr->m_iface.disconnect();
	return 0;
}

DerivedInterfaceMethod(int)::lua_Commit(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, true);

	try {
		ptr->m_iface.commit();
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

DerivedInterfaceMethod(int)::lua_Poll(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, true);

	bool hadResponses = false;
	actionStruct action;
	while (ptr->DequeueAction(action))
	{
		hadResponses = true;

		switch (action.type)
		{
		case globals::actionType::Disconnection:
			LUA->ReferencePush(redis::globals::iRefDebugTraceBack);
			if (redis::PushCallback(LUA, ptr->m_refOnDisconnected, 1, "OnDisconnected"))
			{
				LUA->Push(1);
				if (LUA->PCall(1, 0, -3) != 0)
					redis::ErrorNoHalt(LUA, "[redis OnDisconnected callback error] ");
			}
			else
				LUA->Pop();
			break;
		case globals::actionType::Connection:
			LUA->ReferencePush(redis::globals::iRefDebugTraceBack);
			if (redis::PushCallback(LUA, ptr->m_refOnDisconnected, 1, "OnConnected"))
			{
				LUA->Push(1);
				if (LUA->PCall(1, 0, -3) != 0)
					redis::ErrorNoHalt(LUA, "[redis OnConnected callback error] ");
			}
			else
				LUA->Pop();
			break;
		default:
			ptr->HandleAction(LUA, action);
			break;
		}
	}

	LUA->PushBool(hadResponses);
	return 1;
}
#pragma endregion