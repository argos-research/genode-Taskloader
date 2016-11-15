#pragma once

#include <taskloader/taskloader_client.h>
#include <base/connection.h>

struct Taskloader_connection : Genode::Connection<Taskloader_session>, Taskloader_session_client
{
	Taskloader_connection() :
		/* create session */
		Genode::Connection<Taskloader_session>(session("foo, ram_quota=1M")),

		/* initialize RPC interface */
		Taskloader_session_client(cap())
	{
	}
};
