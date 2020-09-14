#pragma once

struct subActionData {
	std::string channel;
	std::string message;
};
typedef redis::action<subActionData> subAction;

namespace redis
{
	class subscriber : BaseInterface<subAction, cpp_redis::subscriber>
	{
	public:
		subscriber(GarrysMod::Lua::ILuaBase* LUA) : BaseInterface(LUA) { }

		static subscriber* GetSubscriber(GarrysMod::Lua::ILuaBase* LUA, int index, bool throwNullError) { return static_cast<subscriber*>(_get(LUA, index, throwNullError)); }

		static void Initialize(GarrysMod::Lua::ILuaBase* LUA);
		void HandleAction(GarrysMod::Lua::ILuaBase* LUA, subAction action);

		static int lua_Subscribe(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Unsubscribe(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_PSubscribe(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_PUnsubscribe(GarrysMod::Lua::ILuaBase* LUA);
	private:
	};
};