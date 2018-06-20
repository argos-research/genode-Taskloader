#pragma once

#include <list>
#include <unordered_map>

#include <base/signal.h>
#include <taskloader/taskloader_session.h>
#include <base/attached_ram_dataspace.h>
#include <os/server.h>
#include <root/component.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include "sched_controller_session/connection.h"

#include "task.h"

struct Taskloader_session_component : Genode::Rpc_object<Taskloader_session>
{
public:
	Taskloader_session_component(Genode::Env &env);
	virtual ~Taskloader_session_component();

	// Create tasks in idle state from XML description.
	void add_tasks(Genode::Ram_dataspace_capability xml_ds_cap);

	// Allocate and return a capability of a new dataspace to be used for a task binary.
	Genode::Ram_dataspace_capability binary_ds(Genode::Ram_dataspace_capability name_ds_cap, size_t size);

	// Destruct all tasks.
	void clear_tasks();

	// Start idle tasks.
	void start();

	// Stop all tasks.
	void stop();

	Genode::Ram_dataspace_capability profile_data();

	
protected:
	Genode::Env& _env;
	Genode::Entrypoint& _ep;
	Task::Shared_data _shared;
	Genode::Heap _heap { _env.ram(), _env.rm() };
	//Genode::Cap_connection _cap;

	Genode::Attached_ram_dataspace _profile_data;

	size_t _quota {_env.ram().ram_quota().value};

	Genode::Number_of_bytes _profile_ds_size();
	Genode::Number_of_bytes _trace_quota();
	Genode::Number_of_bytes _trace_buf_size();

private:
	bool verbose_debug=false;
	Sched_controller::Connection sched {};
	Task::Parent_services _parent_services { };
	Task::Child_services  _child_services  { };
};

struct Taskloader_root_component : Genode::Root_component<Taskloader_session_component>
{
public:
	Taskloader_root_component(Genode::Env &env, Genode::Entrypoint* ep, Genode::Allocator *allocator) :
		Genode::Root_component<Taskloader_session_component>(&ep->rpc_ep(), allocator),
		_env(env)
	{
		Genode::log("Creating root component.");
	}

protected:
	Genode::Env &_env;

	Taskloader_session_component* _create_session(const char *)
	{
		Genode::log("Creating Taskloader session.");
		return new (md_alloc()) Taskloader_session_component(_env);
	}
};
