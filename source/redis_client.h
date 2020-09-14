#pragma once

struct clientActionData {
	cpp_redis::reply	reply;
	int32_t				reference;
};
typedef redis::action<clientActionData> clientAction;

namespace redis
{
	class client : BaseInterface<clientAction, cpp_redis::client>
	{
	public:
		client(GarrysMod::Lua::ILuaBase* LUA) : BaseInterface(LUA) { }

		static client* GetClient(GarrysMod::Lua::ILuaBase* LUA, int index, bool throwNullError) { return static_cast<client*>(_get(LUA, index, throwNullError)); }


		static void Initialize(GarrysMod::Lua::ILuaBase* LUA);
		void HandleAction(GarrysMod::Lua::ILuaBase* LUA, clientAction action);

		static int lua_Send(GarrysMod::Lua::ILuaBase* LUA);
	private:
	};
};