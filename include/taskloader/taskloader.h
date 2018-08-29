#pragma once

#include <list>
#include <unordered_map>

#include <base/signal.h>
#include <base/attached_ram_dataspace.h>
#include <os/server.h>
#include <root/component.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <sched_controller_session/connection.h>
#include <base/child.h>
#include <taskloader/task.h>
#include <os/static_parent_services.h>

namespace Taskloader{
class Taskloader
{
	public:
		Taskloader(Genode::Env &env, Genode::Registry<Genode::Registered<Genode::Parent_service> > &parent_services);
		~Taskloader();

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
	
	private:
		//Task::Parent_services _parent_services { };
		Task::Child_services _child_services { };

	protected:
		Genode::Env& _env;
		Genode::Registry<Genode::Registered<Genode::Parent_service> > &_parent_services;
		Task::Shared_data _shared;
		Genode::Heap _heap { _env.ram(), _env.rm() };

		Genode::Attached_ram_dataspace _profile_data;

		size_t _quota {_env.ram().ram_quota().value};

		Genode::Number_of_bytes _profile_ds_size();
		Genode::Number_of_bytes _trace_quota();
		Genode::Number_of_bytes _trace_buf_size();

		Mon_manager::Connection _mon{_env};
		//Dom0_server::Connection dom0{_env};
};
}
