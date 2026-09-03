#pragma once

#define MODULE_VERSION 1.3

#include <cpp_redis/cpp_redis>
#include <GarrysMod/Lua/Interface.h>
#include "readerwriterqueue.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

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
			ConnectionFailed,
			Disconnection,
			ReconnectFailed,
			ReconnectStopped,
			Reply,
			Publish,
			Message
		};
	}

	void ErrorNoHalt(GarrysMod::Lua::ILuaBase* LUA, const char* msg);
	bool PushCallback(GarrysMod::Lua::ILuaBase* LUA, int ref, int idx, const char* field);

	void PushReply(GarrysMod::Lua::ILuaBase* LUA, const cpp_redis::reply& reply);

	int Exception(GarrysMod::Lua::ILuaBase* LUA, int callbackRef, const cpp_redis::redis_error& e);
	int GetCallback(GarrysMod::Lua::ILuaBase* LUA, int stackPos);
	int GetCallbackOptional(GarrysMod::Lua::ILuaBase* LUA, int stackPos);

	template <typename actionData>
	struct action {
		globals::actionType	type;
		actionData	data;
	};

	template <class actionStruct, class redisInterface>
	class BaseInterface {
	public:
		BaseInterface(GarrysMod::Lua::ILuaBase* LUA);
		virtual ~BaseInterface();

		static int lua__eq(GarrysMod::Lua::ILuaBase* LUA);
		static int lua__tostring(GarrysMod::Lua::ILuaBase* LUA);
		static int lua__index(GarrysMod::Lua::ILuaBase* LUA);
		static int lua__newindex(GarrysMod::Lua::ILuaBase* LUA);
		static int lua__gc(GarrysMod::Lua::ILuaBase* LUA);

		static int lua_IsValid(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_IsConnected(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_IsConnecting(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_IsReconnecting(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Connect(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Disconnect(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Poll(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Commit(GarrysMod::Lua::ILuaBase* LUA);

		/*
		 * The queue is single producer / single consumer. The consumer is always the Lua thread
		 * (Poll), but producers are the cpp_redis io thread and the connect thread, so enqueues
		 * are serialised with a mutex.
		 */
		bool EnqueueAction(const actionStruct& action)
		{
			std::lock_guard<std::mutex> lock(m_queueMutex);
			return m_queue.enqueue(action);
		}
		bool DequeueAction(actionStruct& action) { return m_queue.try_dequeue(action); }
	protected:
		inline static int	m_metaTableID = 0;
		inline static char* m_metaTableName = 0;
		int					m_refOnConnected = 0;
		int					m_refOnConnectFailed = 0;
		int					m_refOnDisconnected = 0;
		int					m_refOnReconnectFailed = 0;
		int					m_refOnReconnectStopped = 0;
		int					m_refOnMessage = 0;

		static BaseInterface* Get(GarrysMod::Lua::ILuaBase* LUA, int index, bool throwNullError) { return static_cast<BaseInterface*>(_get(LUA, index, throwNullError)); }
		static void* _get(GarrysMod::Lua::ILuaBase* LUA, int index, bool throwNullError);

		static void InitMetatable(GarrysMod::Lua::ILuaBase* LUA, const char* mtName);
		static void CheckType(GarrysMod::Lua::ILuaBase* LUA, int index);
		virtual void HandleAction(GarrysMod::Lua::ILuaBase* LUA, actionStruct action) { }

		redisInterface m_iface;
		moodycamel::ReaderWriterQueue<actionStruct> m_queue;
		std::mutex m_queueMutex;

		/*
		 * The initial connect blocks for up to the connect timeout, so it runs on its own thread.
		 * One attempt runs at a time; m_connecting covers the window between Connect returning and
		 * the outcome being queued, and m_connectError carries the failure reason to Poll.
		 */
		std::thread m_connectThread;
		std::atomic<bool> m_connecting = false;
		std::string m_connectError;
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

DerivedInterfaceMethod()::~BaseInterface()
{
	if (m_connectThread.joinable())
		m_connectThread.join();
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

	LUA->PushCFunction(wrap(lua_IsConnecting));
	LUA->SetField(-2, "IsConnecting");

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
		else if (strcmp(key, "OnConnectFailed") == 0)
			ref = &ptr->m_refOnConnectFailed;
		else if (strcmp(key, "OnDisconnected") == 0)
			ref = &ptr->m_refOnDisconnected;
		else if (strcmp(key, "OnReconnectFailed") == 0)
			ref = &ptr->m_refOnReconnectFailed;
		else if (strcmp(key, "OnReconnectStopped") == 0)
			ref = &ptr->m_refOnReconnectStopped;
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

DerivedInterfaceMethod(int)::lua_IsConnecting(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, true);

	LUA->PushBool(ptr->m_connecting || ptr->m_iface.is_reconnecting());
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

	std::string host = LUA->CheckString(2);
	size_t port = static_cast<size_t>(LUA->CheckNumber(3));

	int timeoutMs = 5000;
	if (LUA->IsType(4, GarrysMod::Lua::Type::Number))
		timeoutMs = LUA->GetNumber(4);

	int maxReconnects = -1;
	if (LUA->IsType(5, GarrysMod::Lua::Type::Number))
		maxReconnects = LUA->GetNumber(5);

	int reconnectIntervalMs = 500;
	if (LUA->IsType(6, GarrysMod::Lua::Type::Number))
		reconnectIntervalMs = LUA->GetNumber(6);

	if (ptr->m_connecting)
	{
		LUA->PushNil();
		LUA->PushString("already connecting");
		return 2;
	}

	if (ptr->m_iface.is_connected())
	{
		LUA->PushNil();
		LUA->PushString("already connected");
		return 2;
	}

	/*
	 * Only one attempt runs at a time, so a joinable thread here has already finished and
	 * joining it is immediate. The outcome of the new attempt reaches Lua through OnConnected
	 * or OnConnectFailed on a later Poll.
	 */
	if (ptr->m_connectThread.joinable())
		ptr->m_connectThread.join();

	ptr->m_connecting = true;
	ptr->m_connectThread = std::thread([ptr, host, port, timeoutMs, maxReconnects, reconnectIntervalMs]()
		{
			try
			{
				ptr->m_iface.connect(host, port, [ptr](auto, auto, auto status)
					{
						using state = cpp_redis::connect_state;

						/*
						 * ok reports success for both the initial connect and a reconnect. dropped fires
						 * once when the connection is lost, then cpp_redis retries on its own: every
						 * attempt that fails reports failed or lookup_failed, and stopped means it has
						 * given up.
						 */
						switch (status)
						{
						case state::ok:
							ptr->m_connecting = false;
							ptr->EnqueueAction({ globals::actionType::Connection });
							break;
						case state::dropped:
							ptr->EnqueueAction({ globals::actionType::Disconnection });
							break;
						case state::failed:
						case state::lookup_failed:
							ptr->EnqueueAction({ globals::actionType::ReconnectFailed });
							break;
						case state::stopped:
							ptr->EnqueueAction({ globals::actionType::ReconnectStopped });
							break;
						default:
							break;
						}
					}, timeoutMs, maxReconnects, reconnectIntervalMs);
			}
			catch (const std::exception& e)
			{
				ptr->m_connectError = e.what();
				ptr->m_connecting = false;
				ptr->EnqueueAction({ globals::actionType::ConnectionFailed });
			}
		});

	LUA->PushBool(true);
	return 1;
}

DerivedInterfaceMethod(int)::lua_Disconnect(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface* ptr = Get(LUA, 1, true);

	/* let an in-flight connect finish so the socket is not torn down underneath it */
	if (ptr->m_connectThread.joinable())
		ptr->m_connectThread.join();

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
			if (redis::PushCallback(LUA, ptr->m_refOnConnected, 1, "OnConnected"))
			{
				LUA->Push(1);
				if (LUA->PCall(1, 0, -3) != 0)
					redis::ErrorNoHalt(LUA, "[redis OnConnected callback error] ");
			}
			else
				LUA->Pop();
			break;
		case globals::actionType::ConnectionFailed:
			LUA->ReferencePush(redis::globals::iRefDebugTraceBack);
			if (redis::PushCallback(LUA, ptr->m_refOnConnectFailed, 1, "OnConnectFailed"))
			{
				LUA->Push(1);
				LUA->PushString(ptr->m_connectError.c_str());
				if (LUA->PCall(2, 0, -4) != 0)
					redis::ErrorNoHalt(LUA, "[redis OnConnectFailed callback error] ");
			}
			else
				LUA->Pop();
			break;
		case globals::actionType::ReconnectFailed:
			LUA->ReferencePush(redis::globals::iRefDebugTraceBack);
			if (redis::PushCallback(LUA, ptr->m_refOnReconnectFailed, 1, "OnReconnectFailed"))
			{
				LUA->Push(1);
				if (LUA->PCall(1, 0, -3) != 0)
					redis::ErrorNoHalt(LUA, "[redis OnReconnectFailed callback error] ");
			}
			else
				LUA->Pop();
			break;
		case globals::actionType::ReconnectStopped:
			LUA->ReferencePush(redis::globals::iRefDebugTraceBack);
			if (redis::PushCallback(LUA, ptr->m_refOnReconnectStopped, 1, "OnReconnectStopped"))
			{
				LUA->Push(1);
				if (LUA->PCall(1, 0, -3) != 0)
					redis::ErrorNoHalt(LUA, "[redis OnReconnectStopped callback error] ");
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