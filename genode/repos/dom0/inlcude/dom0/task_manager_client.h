#pragma once

#include <base/printf.h>
#include <base/rpc_client.h>
#include <dom0/task_manager_session.h>

struct Task_manager_session_client : Genode::Rpc_client<Task_manager_session>
{
	Task_manager_session_client(Genode::Capability<Task_manager_session> cap) :
		Genode::Rpc_client<Task_manager_session>(cap) { }

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

};
