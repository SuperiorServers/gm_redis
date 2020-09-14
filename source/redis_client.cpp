#include "main.hpp"
#include "redis_client.h"

void redis::client::Initialize(GarrysMod::Lua::ILuaBase* LUA)
{
	BaseInterface::InitMetatable(LUA, "redis_client");

	LUA->PushCFunction(wrap(lua_Send));
	LUA->SetField(-2, "Send");

	LUA->Pop();
}

void buildTable(GarrysMod::Lua::ILuaBase* LUA, const std::vector<cpp_redis::reply>& replies)
{
	LUA->CreateTable();

	int i = 1;
	for (auto reply : replies)
	{
		LUA->PushNumber(i++);

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
			buildTable(LUA, reply.as_array());
			break;

		case cpp_redis::reply::type::null:
			LUA->PushNil();
			break;
		}

		LUA->SetTable(-3);
	}
}

const char* toString(GarrysMod::Lua::ILuaBase* LUA, int32_t idx, size_t* len = nullptr)
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
			LUA->PushFormattedString("%s: %p", LUA->GetTypeName(LUA->GetType(idx)), lua_topointer(LUA->GetState(), idx));
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

			switch (action.data.reply.get_type())
			{
			case cpp_redis::reply::type::error:
			case cpp_redis::reply::type::bulk_string:
			case cpp_redis::reply::type::simple_string:
				LUA->PushString(action.data.reply.as_string().c_str());
				break;

			case cpp_redis::reply::type::integer:
				LUA->PushNumber(static_cast<double>(action.data.reply.as_integer()));
				break;

			case cpp_redis::reply::type::array:
				buildTable(LUA, action.data.reply.as_array());
				break;

			case cpp_redis::reply::type::null:
				LUA->PushNil();
				break;
			}

			if (LUA->PCall(2, 0, -4) != 0)
				redis::ErrorNoHalt(LUA, "[redis Send callback error] ");

			LUA->ReferenceFree(action.data.reference);
		}
	}
}

int redis::client::lua_Send(GarrysMod::Lua::ILuaBase* LUA)
{
	client* ptr = GetClient(LUA, 1, true);
	int reference = GarrysMod::Lua::Type::NONE;

	if (LUA->Top() >= 3 && !LUA->IsType(3, GarrysMod::Lua::Type::NIL))
	{
		LUA->CheckType(3, GarrysMod::Lua::Type::FUNCTION);
		LUA->Push(3);
		reference = LUA->ReferenceCreate();
	}

	std::vector<std::string> keys;
	if (LUA->IsType(2, GarrysMod::Lua::Type::TABLE))
	{
		int32_t k = 1;
		do
		{
			LUA->PushNumber(k);
			LUA->GetTable(2);
			if (LUA->IsType(-1, GarrysMod::Lua::Type::NIL))
			{
				LUA->Pop();
				break;
			}

			keys.push_back(toString(LUA, -1));
			LUA->Pop(2);
			++k;
		} while (true);
	}
	else
		keys.push_back(toString(LUA, 2));

	try
	{
		if (reference == GarrysMod::Lua::Type::NONE)
			ptr->m_iface.send(keys);
		else
			ptr->m_iface.send(keys, [ptr, reference](cpp_redis::reply& reply)
			{
					ptr->EnqueueAction({ redis::globals::actionType::Reply, {reply, reference} });
			});
	}
	catch (const cpp_redis::redis_error& e)
	{
		LUA->ReferenceFree(reference);
		LUA->PushNil();
		LUA->PushString(e.what());
		return 2;
	}

	LUA->PushBool(true);
	return 1;
}