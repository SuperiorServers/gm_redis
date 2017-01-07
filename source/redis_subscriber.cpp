#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <redis_subscriber.hpp>
#include <main.hpp>
#include <cstdint>
#include <lua.hpp>
#include <cpp_redis/cpp_redis>
#include <readerwriterqueue.hpp>
#include <vector>
#include <memory>

namespace redis_subscriber
{

enum class Action
{
	Disconnection,
	Message
};

struct Response
{
	Action type;
	std::string channel;
	std::string message;
};

class Container
{
public:
	cpp_redis::redis_subscriber &GetSubscriber( )
	{
		return subscriber;
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
	cpp_redis::redis_subscriber subscriber;
	moodycamel::ReaderWriterQueue<Response> queue;
};

struct UserData
{
	cpp_redis::redis_subscriber *subscriber;
	uint8_t type;
	Container *container;
};

static const char metaname[] = "redis_subscriber";
static const uint8_t metatype = 127;
static const char invalid_error[] = "invalid redis_subscriber";
static const char table_name[] = "redis_subscribers";

LUA_FUNCTION( Create )
{
	Container *container = nullptr;
	cpp_redis::redis_subscriber *subscriber = nullptr;
	try
	{
		container = new Container;
		subscriber = &container->GetSubscriber( );
	}
	catch( const cpp_redis::redis_error &e )
	{
		LUA->PushNil( );
		LUA->PushString( e.what( ) );
		return 2;
	}

	UserData *udata = static_cast<UserData *>( LUA->NewUserdata( sizeof( UserData ) ) );
	udata->subscriber = subscriber;
	udata->container = container;
	udata->type = metatype;

	LUA->CreateMetaTableType( metaname, metatype );
	LUA->SetMetaTable( -2 );

	LUA->CreateTable( );
	lua_setfenv( state, -2 );

	LUA->GetField( GarrysMod::Lua::INDEX_REGISTRY, table_name );
	LUA->PushUserdata( subscriber );
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

static cpp_redis::redis_subscriber *Get( lua_State *state, int32_t index, Container **container = nullptr )
{
	CheckType( state, index );
	UserData *udata = GetUserData( state, index );
	cpp_redis::redis_subscriber *subscriber = udata->subscriber;
	if( subscriber == nullptr )
		LUA->ArgError( index, invalid_error );

	if( container != nullptr )
		*container = udata->container;

	return subscriber;
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
	cpp_redis::redis_subscriber *subscriber = userdata->subscriber;
	if( subscriber == nullptr )
		return 0;

	userdata->subscriber = nullptr;
	delete userdata->container;
	userdata->container = nullptr;
	return 0;
}

LUA_FUNCTION_STATIC( IsValid )
{
	UserData *udata = GetUserData( state, 1 );
	LUA->PushBool( udata->subscriber != nullptr );
	return 1;
}

LUA_FUNCTION_STATIC( IsConnected )
{
	cpp_redis::redis_subscriber *subscriber = Get( state, 1 );
	LUA->PushBool( subscriber->is_connected( ) );
	return 1;
}

LUA_FUNCTION_STATIC( Connect )
{
	Container *container = nullptr;
	cpp_redis::redis_subscriber *subscriber = Get( state, 1, &container );
	const char *host = LUA->GetString( 2 );
	size_t port = static_cast<size_t>( LUA->GetNumber( 3 ) );

	try
	{
		subscriber->connect( host, port, [container]( cpp_redis::redis_subscriber & )
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
	cpp_redis::redis_subscriber *subscriber = Get( state, 1 );
	subscriber->disconnect( );
	return 0;
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
		switch( response.type )
		{
		case Action::Disconnection:
			if( !redis::GetMetaField( state, 1, "OnDisconnected" ) )
				break;

			LUA->Push( 1 );
			if( LUA->PCall( 1, 1, -3 ) != 0 )
			{
				static_cast<GarrysMod::Lua::ILuaInterface *>( LUA )->ErrorNoHalt( "\n[redis subscriber callback error] %s\n\n", LUA->GetString( -1 ) );
				LUA->Pop( );
			}

			break;

		case Action::Message:
			if( !redis::GetMetaField( state, 1, "OnMessage" ) )
				break;

			LUA->Push( 1 );
			LUA->PushString( response.channel.c_str( ) );
			LUA->PushString( response.message.c_str( ) );
			if( LUA->PCall( 3, 0, -5 ) != 0 )
			{
				static_cast<GarrysMod::Lua::ILuaInterface *>( LUA )->ErrorNoHalt( "\n[redis subscriber callback error] %s\n\n", LUA->GetString( -1 ) );
				LUA->Pop( );
			}

			break;
		}

		had_responses = true;
	}
	
	LUA->PushBool( had_responses );
	return 1;
}

LUA_FUNCTION_STATIC( Subscribe )
{
	Container *container = nullptr;
	cpp_redis::redis_subscriber *subscriber = Get( state, 1, &container );
	const char *channel = LUA->GetString( 2 );
	LUA->CheckType( 3, GarrysMod::Lua::Type::FUNCTION );

	try
	{
		subscriber->subscribe( channel, [container]( const std::string &channel, const std::string &message )
		{
			container->EnqueueResponse( { Action::Disconnection, channel, message } );
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

LUA_FUNCTION_STATIC( Unsubscribe )
{
	cpp_redis::redis_subscriber *subscriber = Get( state, 1 );
	const char *channel = LUA->GetString( 2 );

	try
	{
		subscriber->unsubscribe( channel );
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

LUA_FUNCTION_STATIC( PSubscribe )
{
	Container *container = nullptr;
	cpp_redis::redis_subscriber *subscriber = Get( state, 1, &container );
	const char *channel = LUA->GetString( 2 );
	LUA->CheckType( 3, GarrysMod::Lua::Type::FUNCTION );

	try
	{
		subscriber->psubscribe( channel, [container]( const std::string &channel, const std::string &message )
		{
			container->EnqueueResponse( { Action::Disconnection, channel, message } );
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

LUA_FUNCTION_STATIC( PUnsubscribe )
{
	cpp_redis::redis_subscriber *subscriber = Get( state, 1 );
	const char *channel = LUA->GetString( 2 );

	try
	{
		subscriber->punsubscribe( channel );
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

LUA_FUNCTION_STATIC( Commit )
{
	cpp_redis::redis_subscriber *subscriber = Get( state, 1 );

	try
	{
		subscriber->commit( );
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
	
	LUA->PushCFunction( Subscribe );
	LUA->SetField( -2, "Subscribe" );

	LUA->PushCFunction( Unsubscribe );
	LUA->SetField( -2, "Unsubscribe" );

	LUA->PushCFunction( PSubscribe );
	LUA->SetField( -2, "PSubscribe" );

	LUA->PushCFunction( PUnsubscribe );
	LUA->SetField( -2, "PUnsubscribe" );

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
