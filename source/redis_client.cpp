#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <redis_client.hpp>
#include <main.hpp>
#include <cstdint>
#include <lua.hpp>
#include <cpp_redis/cpp_redis>
#include <readerwriterqueue.hpp>
#include <vector>
#include <memory>

namespace redis_client
{

enum class Action
{
	Disconnection,
	Reply
};

struct Response
{
	Action type;
	cpp_redis::reply reply;
	int32_t reference;
};

class Container
{
public:
	cpp_redis::redis_client &GetClient( )
	{
		return client;
	}

	bool EnqueueResponse( const Response &response )
	{
		return queue.enqueue( response );
	}

	bool DequeueResponse( Response &response )
	{
		return queue.try_dequeue( response );
	}

private:
	cpp_redis::redis_client client;
	moodycamel::ReaderWriterQueue<Response> queue;
};

struct UserData
{
	cpp_redis::redis_client *client;
	uint8_t type;
	Container *container;
};

static const char metaname[] = "redis_client";
static const uint8_t metatype = 127;
static const char invalid_error[] = "invalid redis_client";
static const char table_name[] = "redis_clients";

LUA_FUNCTION( Create )
{
	Container *container = nullptr;
	cpp_redis::redis_client *client = nullptr;
	try
	{
		container = new Container;
		client = &container->GetClient( );
	}
	catch( const cpp_redis::redis_error &e )
	{
		LUA->PushNil( );
		LUA->PushString( e.what( ) );
		return 2;
	}

	UserData *udata = static_cast<UserData *>( LUA->NewUserdata( sizeof( UserData ) ) );
	udata->client = client;
	udata->container = container;
	udata->type = metatype;

	LUA->CreateMetaTableType( metaname, metatype );
	LUA->SetMetaTable( -2 );

	LUA->CreateTable( );
	lua_setfenv( state, -2 );

	LUA->GetField( GarrysMod::Lua::INDEX_REGISTRY, table_name );
	LUA->PushUserdata( client );
	LUA->Push( -3 );
	LUA->SetTable( -4 );
	LUA->Pop( );

	return 1;
}

inline void CheckType( lua_State *state, int32_t index )
{
	if( !LUA->IsType( index, metatype ) )
		luaL_typerror( state, index, metaname );
}

inline UserData *GetUserData( lua_State *state, int index )
{
	return static_cast<UserData *>( LUA->GetUserdata( index ) );
}

static cpp_redis::redis_client *Get( lua_State *state, int32_t index, Container **container = nullptr )
{
	CheckType( state, index );
	UserData *udata = GetUserData( state, index );
	cpp_redis::redis_client *client = udata->client;
	if( client == nullptr )
		LUA->ArgError( index, invalid_error );

	if( container != nullptr )
		*container = udata->container;

	return client;
}

LUA_FUNCTION_STATIC( tostring )
{
	lua_pushfstring( state, redis::tostring_format, metaname, Get( state, 1 ) );
	return 1;
}

LUA_FUNCTION_STATIC( eq )
{
	LUA->PushBool( Get( state, 1 ) == Get( state, 2 ) );
	return 1;
}

LUA_FUNCTION_STATIC( index )
{
	CheckType( state, 1 );

	LUA->CreateMetaTableType( metaname, metatype );
	LUA->Push( 2 );
	LUA->RawGet( -2 );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::NIL ) )
		return 1;

	LUA->Pop( 2 );

	lua_getfenv( state, 1 );
	LUA->Push( 2 );
	LUA->RawGet( -2 );
	return 1;
}

LUA_FUNCTION_STATIC( newindex )
{
	CheckType( state, 1 );

	lua_getfenv( state, 1 );
	LUA->Push( 2 );
	LUA->Push( 3 );
	LUA->RawSet( -3 );
	return 0;
}

LUA_FUNCTION_STATIC( gc )
{
	UserData *userdata = GetUserData( state, 1 );
	cpp_redis::redis_client *client = userdata->client;
	if( client == nullptr )
		return 0;

	delete client;
	userdata->client = nullptr;
	return 0;
}

LUA_FUNCTION_STATIC( IsValid )
{
	UserData *udata = GetUserData( state, 1 );
	LUA->PushBool( udata->client != nullptr );
	return 1;
}

LUA_FUNCTION_STATIC( IsConnected )
{
	cpp_redis::redis_client *client = Get( state, 1 );
	LUA->PushBool( client->is_connected( ) );
	return 1;
}

LUA_FUNCTION_STATIC( Connect )
{
	Container *container = nullptr;
	cpp_redis::redis_client *client = Get( state, 1, &container );
	const char *host = LUA->GetString( 2 );
	size_t port = static_cast<size_t>( LUA->GetNumber( 3 ) );

	try
	{
		client->connect( host, port, [container]( cpp_redis::redis_client & )
		{
			container->EnqueueResponse( { Action::Disconnection } );
		} );
	}
	catch( const cpp_redis::redis_error &e )
	{
		LUA->PushNil( );
		LUA->PushString( e.what( ) );
		return 2;
	}

	LUA->PushBool( true );
	return 1;
}

LUA_FUNCTION_STATIC( Disconnect )
{
	cpp_redis::redis_client *client = Get( state, 1 );
	client->disconnect( );
	return 0;
}

inline void BuildTable( lua_State *state, const std::vector<cpp_redis::reply> &replies )
{
	LUA->CreateTable( );

	for( size_t k = 0; k < replies.size( ); ++k )
	{
		LUA->PushNumber( static_cast<double>( k ) );
		
		const cpp_redis::reply &reply = replies[k];
		switch( reply.get_type( ) )
		{
		case cpp_redis::reply::type::error:
		case cpp_redis::reply::type::bulk_string:
		case cpp_redis::reply::type::simple_string:
			LUA->PushString( reply.as_string( ).c_str( ) );
			break;

		case cpp_redis::reply::type::integer:
			LUA->PushNumber( static_cast<double>( reply.as_integer( ) ) );
			break;

		case cpp_redis::reply::type::array:
			BuildTable( state, reply.as_array( ) );
			break;

		case cpp_redis::reply::type::null:
			LUA->PushNil( );
			break;
		}

		LUA->SetTable( -3 );
	}
}

LUA_FUNCTION_STATIC( Poll )
{
	Container *container = nullptr;
	Get( state, 1, &container );

	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "debug" );
	LUA->GetField( -1, "traceback" );

	bool had_responses = false;
	Response response;
	while( container->DequeueResponse( response ) )
	{
		LUA->ReferencePush( response.reference );
		LUA->Push( 1 );
		
		switch( response.type )
		{
		case Action::Disconnection:
			if( luaL_callmeta( state, 1, "OnDisconnected" ) == 1 )
				LUA->Pop( );

			break;

		case Action::Reply:
			switch( response.reply.get_type( ) )
			{
			case cpp_redis::reply::type::error:
			case cpp_redis::reply::type::bulk_string:
			case cpp_redis::reply::type::simple_string:
				LUA->PushString( response.reply.as_string( ).c_str( ) );
				break;

			case cpp_redis::reply::type::integer:
				LUA->PushNumber( static_cast<double>( response.reply.as_integer( ) ) );
				break;

			case cpp_redis::reply::type::array:
				BuildTable( state, response.reply.as_array( ) );
				break;

			case cpp_redis::reply::type::null:
				LUA->PushNil( );
				break;
			}

			if( LUA->PCall( 2, 0, -4 ) != 0 )
			{
				static_cast<GarrysMod::Lua::ILuaInterface *>( LUA )->ErrorNoHalt( "\n[redis client callback error] %s\n\n", LUA->GetString( -1 ) );
				LUA->Pop( );
			}

			LUA->ReferenceFree( response.reference );
			break;
		}

		had_responses = true;
	}
	
	LUA->PushBool( had_responses );
	return 1;
}

LUA_FUNCTION_STATIC( Send )
{
	Container *container = nullptr;
	cpp_redis::redis_client *client = Get( state, 1, &container );

	int32_t cmdtype = LUA->GetType( 2 );
	if( cmdtype != GarrysMod::Lua::Type::TABLE && cmdtype != GarrysMod::Lua::Type::STRING )
		luaL_typerror( state, 2, "string or table" );

	int32_t reference = LUA_TNONE;
	if( LUA->Top( ) >= 3 )
	{
		LUA->CheckType( 3, GarrysMod::Lua::Type::FUNCTION );
		LUA->Push( 3 );
		reference = LUA->ReferenceCreate( );
	}

	std::vector<std::string> keys;
	if( cmdtype == GarrysMod::Lua::Type::TABLE )
	{
		int32_t k = 1;
		do
		{
			LUA->PushNumber( k );
			LUA->GetTable( 2 );
			if( LUA->IsType( -1, GarrysMod::Lua::Type::NIL ) )
			{
				LUA->Pop( );
				break;
			}

			keys.push_back( LUA->GetString( -1 ) );
			LUA->Pop( );
			++k;
		}
		while( true );
	}
	else if( cmdtype == GarrysMod::Lua::Type::STRING )
		keys.push_back( LUA->GetString( 2 ) );

	if( reference != LUA_TNONE )
	{
		try
		{
			client->send( keys, [container, reference]( cpp_redis::reply &reply )
			{
				container->EnqueueResponse( { Action::Reply, reply, reference } );
			} );
		}
		catch( const cpp_redis::redis_error &e )
		{
			LUA->ReferenceFree( reference );
			LUA->PushNil( );
			LUA->PushString( e.what( ) );
			return 2;
		}
	}
	else
	{
		try
		{
			client->send( keys );
		}
		catch( const cpp_redis::redis_error &e )
		{
			LUA->PushNil( );
			LUA->PushString( e.what( ) );
			return 2;
		}
	}

	LUA->PushBool( true );
	return 1;
}

LUA_FUNCTION_STATIC( Commit )
{
	cpp_redis::redis_client *client = Get( state, 1 );

	try
	{
		client->commit( );
	}
	catch( const cpp_redis::redis_error &e )
	{
		LUA->PushNil( );
		LUA->PushString( e.what( ) );
		return 2;
	}

	LUA->PushBool( true );
	return 1;
}

void Initialize( lua_State *state )
{
	LUA->CreateMetaTableType( metaname, metatype );

	LUA->PushCFunction( tostring );
	LUA->SetField( -2, "__tostring" );

	LUA->PushCFunction( eq );
	LUA->SetField( -2, "__eq" );

	LUA->PushCFunction( index );
	LUA->SetField( -2, "__index" );

	LUA->PushCFunction( newindex );
	LUA->SetField( -2, "__newindex" );

	LUA->PushCFunction( gc );
	LUA->SetField( -2, "__gc" );

	LUA->PushCFunction( gc );
	LUA->SetField( -2, "Destroy" );

	LUA->PushCFunction( IsValid );
	LUA->SetField( -2, "IsValid" );

	LUA->PushCFunction( IsConnected );
	LUA->SetField( -2, "IsConnected" );

	LUA->PushCFunction( Connect );
	LUA->SetField( -2, "Connect" );

	LUA->PushCFunction( Disconnect );
	LUA->SetField( -2, "Disconnect" );

	LUA->PushCFunction( Poll );
	LUA->SetField( -2, "Poll" );
	
	LUA->PushCFunction( Send );
	LUA->SetField( -2, "Send" );

	LUA->PushCFunction( Commit );
	LUA->SetField( -2, "Commit" );

	LUA->Pop( 1 );
}

void Deinitialize( lua_State *state )
{
	LUA->PushNil( );
	LUA->SetField( GarrysMod::Lua::INDEX_REGISTRY, metaname );
}

}
