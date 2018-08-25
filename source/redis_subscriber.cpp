#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <cpp_redis/cpp_redis>
#include <cstdint>
#include <vector>
#include <memory>
#include "readerwriterqueue.hpp"
#include "redis_subscriber.hpp"
#include "main.hpp"

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
	cpp_redis::subscriber &GetSubscriber( )
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
	cpp_redis::subscriber subscriber;
	moodycamel::ReaderWriterQueue<Response> queue;
};

static const char metaname[] = "redis_subscriber";
static int32_t metatype = GarrysMod::Lua::Type::NONE;
static const char invalid_error[] = "invalid redis_subscriber";
static const char table_name[] = "redis_subscribers";

LUA_FUNCTION( Create )
{
	Container *container = nullptr;
	try
	{
		container = new Container;
	}
	catch( const cpp_redis::redis_error &e )
	{
		LUA->PushNil( );
		LUA->PushString( e.what( ) );
		return 2;
	}

	LUA->PushUserType( container, metatype );

	LUA->PushMetaTable( metatype );
	LUA->SetMetaTable( -2 );

	LUA->CreateTable( );
	lua_setfenv( LUA->GetState( ), -2 );

	LUA->GetField( GarrysMod::Lua::INDEX_REGISTRY, table_name );
	LUA->PushUserdata( &container->GetSubscriber( ) );
	LUA->Push( -3 );
	LUA->SetTable( -4 );
	LUA->Pop( );

	return 1;
}

inline void CheckType( GarrysMod::Lua::ILuaBase *LUA, int32_t index )
{
	if( !LUA->IsType( index, metatype ) )
		luaL_typerror( LUA->GetState( ), index, metaname );
}

inline Container *GetUserData( GarrysMod::Lua::ILuaBase *LUA, int index )
{
	return LUA->GetUserType<Container>( index, metatype );
}

static cpp_redis::subscriber *Get( GarrysMod::Lua::ILuaBase *LUA, int32_t index, Container **container = nullptr )
{
	CheckType( LUA, index );
	Container *udata = GetUserData( LUA, index );
	if( udata == nullptr )
		LUA->ArgError( index, invalid_error );

	if( container != nullptr )
		*container = udata;

	return &udata->GetSubscriber( );
}

LUA_FUNCTION_STATIC( tostring )
{
	LUA->PushFormattedString( redis::tostring_format, metaname, Get( LUA, 1 ) );
	return 1;
}

LUA_FUNCTION_STATIC( eq )
{
	LUA->PushBool( Get( LUA, 1 ) == Get( LUA, 2 ) );
	return 1;
}

LUA_FUNCTION_STATIC( index )
{
	CheckType( LUA, 1 );

	LUA->PushMetaTable( metatype );
	LUA->Push( 2 );
	LUA->RawGet( -2 );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::NIL ) )
		return 1;

	LUA->Pop( 2 );

	lua_getfenv( LUA->GetState( ), 1 );
	LUA->Push( 2 );
	LUA->RawGet( -2 );
	return 1;
}

LUA_FUNCTION_STATIC( newindex )
{
	CheckType( LUA, 1 );

	lua_getfenv( LUA->GetState( ), 1 );
	LUA->Push( 2 );
	LUA->Push( 3 );
	LUA->RawSet( -3 );
	return 0;
}

LUA_FUNCTION_STATIC( gc )
{
	Container *udata = GetUserData( LUA, 1 );
	if( udata == nullptr )
		return 0;

	delete udata;
	
	LUA->SetUserType( 1, nullptr );

	return 0;
}

LUA_FUNCTION_STATIC( IsValid )
{
	cpp_redis::subscriber *subscriber = Get( LUA, 1 );
	LUA->PushBool( subscriber != nullptr );
	return 1;
}

LUA_FUNCTION_STATIC( IsConnected )
{
	cpp_redis::subscriber *subscriber = Get( LUA, 1 );
	LUA->PushBool( subscriber->is_connected( ) );
	return 1;
}

LUA_FUNCTION_STATIC( Connect )
{
	Container *container = nullptr;
	cpp_redis::subscriber *subscriber = Get( LUA, 1, &container );
	const char *host = LUA->CheckString( 2 );
	size_t port = static_cast<size_t>( LUA->CheckNumber( 3 ) );

	try
	{
		subscriber->connect( host, port, [container]( auto, auto, auto status )
		{
			using state = cpp_redis::subscriber::connect_state;
			if( status == state::dropped || status == state::failed ||
				status == state::lookup_failed || status == state::stopped )
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
	cpp_redis::subscriber *subscriber = Get( LUA, 1 );
	subscriber->disconnect( );
	return 0;
}

LUA_FUNCTION_STATIC( Poll )
{
	Container *container = nullptr;
	Get( LUA, 1, &container );

	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "debug" );
	LUA->GetField( -1, "traceback" );

	bool had_responses = false;
	Response response;
	while( container->DequeueResponse( response ) )
	{		
		switch( response.type )
		{
		case Action::Disconnection:
			if( !redis::GetMetaField( LUA, 1, "OnDisconnected" ) )
				break;

			LUA->Push( 1 );
			if( LUA->PCall( 1, 1, -3 ) != 0 )
			{
				static_cast<GarrysMod::Lua::ILuaInterface *>( LUA )->ErrorNoHalt( "\n[redis subscriber callback error] %s\n\n", LUA->GetString( -1 ) );
				LUA->Pop( );
			}

			break;

		case Action::Message:
			if( !redis::GetMetaField( LUA, 1, "OnMessage" ) )
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
	cpp_redis::subscriber *subscriber = Get( LUA, 1, &container );
	const char *channel = LUA->CheckString( 2 );

	try
	{
		subscriber->subscribe( channel, [container]( const std::string &channel, const std::string &message )
		{
			container->EnqueueResponse( { Action::Message, channel, message } );
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
	cpp_redis::subscriber *subscriber = Get( LUA, 1 );
	const char *channel = LUA->CheckString( 2 );

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
	cpp_redis::subscriber *subscriber = Get( LUA, 1, &container );
	const char *channel = LUA->CheckString( 2 );

	try
	{
		subscriber->psubscribe( channel, [container]( const std::string &channel, const std::string &message )
		{
			container->EnqueueResponse( { Action::Message, channel, message } );
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
	cpp_redis::subscriber *subscriber = Get( LUA, 1 );
	const char *channel = LUA->CheckString( 2 );

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
	cpp_redis::subscriber *subscriber = Get( LUA, 1 );

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

void Initialize( GarrysMod::Lua::ILuaBase *LUA )
{
	metatype = LUA->CreateMetaTable( metaname );

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

void Deinitialize( GarrysMod::Lua::ILuaBase *LUA )
{
	LUA->PushNil( );
	LUA->SetField( GarrysMod::Lua::INDEX_REGISTRY, metaname );
}

}
