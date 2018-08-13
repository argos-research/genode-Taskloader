#pragma once

#include <base/printf.h>
#include <base/rpc_client.h>
#include <taskloader/taskloader_session.h>

namespace Taskloader {
struct Session_client : Genode::Rpc_client<Session>
{
	Session_client(Genode::Capability<Session> cap) :
		Genode::Rpc_client<Session>(cap) { }

	void add_tasks(Genode::Ram_dataspace_capability xml_ds_cap)
	{
		call<Rpc_add_tasks>(xml_ds_cap);
	}

	void clear_tasks()
	{
		call<Rpc_clear_tasks>();
	}

	Genode::Ram_dataspace_capability binary_ds(Genode::Ram_dataspace_capability name_ds_cap, size_t size)
	{
		return call<Rpc_binary_ds>(name_ds_cap, size);
	}

	void start()
	{
		call<Rpc_start>();
	}

	void stop()
	{
		call<Rpc_stop>();
	}

	Genode::Ram_dataspace_capability profile_data()
	{
		return call<Rpc_profile_data>();
	}

};
}
