#pragma once

#include <dom0/task_manager_client.h>
#include <base/connection.h>

struct Task_manager_connection : Genode::Connection<Task_manager_session>, Task_manager_session_client
{
	Task_manager_connection() :
		/* create session */
		Genode::Connection<Task_manager_session>(session("foo, ram_quota=1M")),

		/* initialize RPC interface */
		Task_manager_session_client(cap())
	{
	}
};
