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

		static int Exception(GarrysMod::Lua::ILuaBase* LUA, int reference, const cpp_redis::redis_error& e);

		static int GetCallback(GarrysMod::Lua::ILuaBase* LUA, int stackPos);

		static int GetCallbackOptional(GarrysMod::Lua::ILuaBase* LUA, int stackPos);

		static std::vector<std::string> GetKeys(GarrysMod::Lua::ILuaBase* LUA, int stackPos);

		static std::vector<std::string> CheckKeys(GarrysMod::Lua::ILuaBase* LUA, int stackPos);

		static int lua_Send(GarrysMod::Lua::ILuaBase* LUA);

		static int lua_Auth(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Select(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Publish(GarrysMod::Lua::ILuaBase* LUA);

		static int lua_Exists(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Delete(GarrysMod::Lua::ILuaBase* LUA);

		static int lua_Get(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_Set(GarrysMod::Lua::ILuaBase* LUA);
		static int lua_SetEx(GarrysMod::Lua::ILuaBase* LUA);
	private:
	};
};