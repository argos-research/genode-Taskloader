#pragma once

#include <taskloader/taskloader_client.h>
#include <base/connection.h>

namespace Taskloader {

	struct Connection : Genode::Connection<Session>, Session_client
	{
		Connection(Genode::Env &env) :
		/* create session */
		Genode::Connection<Taskloader::Session>(env,
							session(env.parent(),
							"ram_quota=6K, cap_quota=4")),
							Session_client(cap())
		{ }
	};
}
