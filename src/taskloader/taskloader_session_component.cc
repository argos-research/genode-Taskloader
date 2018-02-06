#include "taskloader_session_component.h"
#include <timer_session/connection.h>
#include <base/env.h>
#include <base/printf.h>
#include <util/xml_node.h>
#include <util/xml_generator.h>

#include <string>

#include <util/string.h>

#include <trace_session/connection.h>

Taskloader_session_component::Taskloader_session_component(Server::Entrypoint& ep) :
	_ep{ep},
	_shared{_trace_quota(), _trace_buf_size()},
	_cap{},
	_profile_data{Genode::env()->ram_session(), _profile_ds_size()},
	_quota{Genode::env()->ram_session()->quota()}
{
	// Load dynamic linker for dynamically linked binaries.
	//static Genode::Rom_connection ldso_rom("ld.lib.so");
	//Genode::Process::dynamic_linker(ldso_rom.dataspace());

	// Names of services provided by the parent.
	static const char* names[] =
	{
		// core services
		"CAP", "RAM", "RM", "PD", "CPU", "IO_MEM", "IO_PORT",
		"IRQ", "ROM", "LOG", "SIGNAL"
	};
	for (const char* name : names)
	{
		_shared.parent_services.insert(new (Genode::env()->heap()) Genode::Parent_service(name));
	}
}

Taskloader_session_component::~Taskloader_session_component()
{
}

void Taskloader_session_component::add_tasks(Genode::Ram_dataspace_capability xml_ds_cap)
{
	Genode::Region_map* rm = Genode::env()->rm_session();
	const char* xml = rm->attach(xml_ds_cap);
	if(verbose_debug) PINF("Parsing XML file:\n%s", xml);
	Genode::Xml_node root(xml);
	Rq_task::Rq_task rq_task;

	//Update rq_buffer before adding tasks for online analyses to core 1
	sched.update_rq_buffer(1);

	const auto fn = [this, &rq_task] (const Genode::Xml_node& node)
	{
		_shared.tasks.emplace_back(_ep, _cap, _shared, node, &sched);
		//Add task to Controller to perform a schedulability test for core 1
		rq_task = _shared.tasks.back().getRqTask();
		int result = sched.new_task(rq_task, 1);
		if (result != 0){
			if(verbose_debug) PINF("Task with id %d was not accepted by the controller", rq_task.task_id);
			_shared.tasks.back().setSchedulable(false);
		}
		else{
			if(verbose_debug) PINF("Task with id %d was accepted by the controller", rq_task.task_id);
			_shared.tasks.back().setSchedulable(true);
		}
	};

	root.for_each_sub_node("periodictask", fn);

	sched.set_opt_goal(xml_ds_cap);
	rm->detach(xml);
}

void Taskloader_session_component::clear_tasks()
{
	if(verbose_debug) PDBG("Clearing %d task%s. Binaries still held.", _shared.tasks.size(), _shared.tasks.size() == 1 ? "" : "s");
	stop();

	// Wait for task destruction.
	_shared.timer.msleep(500);
	_shared.tasks.clear();
}

Genode::Ram_dataspace_capability Taskloader_session_component::binary_ds(Genode::Ram_dataspace_capability name_ds_cap, size_t size)
{
	Genode::Region_map* rm = Genode::env()->rm_session();
	const char* name = rm->attach(name_ds_cap);
	if(verbose_debug) PDBG("Reserving %d bytes for binary %s", size, name);
	Genode::Ram_session* ram = Genode::env()->ram_session();

	// Hoorray for C++ syntax. This basically forwards ctor arguments, constructing the dataspace in-place so there is no copy or dtor call involved which may invalidate the attached pointer.
	// Also, emplace returns a <iterator, bool> pair indicating insertion success, so we need .first to get the map iterator and ->second to get the actual dataspace.
	Genode::Attached_ram_dataspace& ds = _shared.binaries.emplace(std::piecewise_construct, std::make_tuple(name), std::make_tuple(ram, size)).first->second;
	rm->detach(name);
	return ds.cap();
}

void Taskloader_session_component::start()
{
	if(verbose_debug) PINF("Starting %d task%s.", _shared.tasks.size(), _shared.tasks.size() == 1 ? "" : "s");
	for (Task& task : _shared.tasks)
	{
		if (task.isSchedulable())
		{
			Task::_child_start.submit_for_start(&task);
		}
		else
		{
			Task::Event::Type type=Task::Event::NOT_SCHEDULED;
			Task::log_profile_data(type, task.get_id(), task.get_shared());
		}
	}
}

void Taskloader_session_component::stop()
{
	if(verbose_debug) PINF("Stopping all tasks.");
	for (Task& task : _shared.tasks)
	{
		if (task.isSchedulable())
		{
			task.stop();
			Task::_child_destructor.submit_for_destruction(&task);
		}
	}
}

Genode::Ram_dataspace_capability Taskloader_session_component::profile_data()
{
	static Genode::Trace::Connection trace(1024*4096, 64*4096, 0);

	Genode::Trace::Subject_id subjects[32];
	size_t num_subjects = trace.subjects(subjects, 32);


	//Task::log_profile_data(Task::Event::EXTERNAL, -1, _shared);

	// Xml_generator directly writes XML data into the buffer on construction, explaining the heavy recursion here.
	//if(verbose_debug) PDBG("Generating event log. %d events have occurred.", _shared.event_log.size());
	Genode::Xml_generator xml(_profile_data.local_addr<char>(), _profile_data.size(), "profile", [&]()
	{

		xml.node("events", [&]()
		{
			for (const Task::Event& event : _shared.event_log)
			{
				xml.node("event", [&]()
				{
					xml.attribute("type", Task::Event::type_name(event.type));
					xml.attribute("task-id", std::to_string(event.task_id).c_str());
					xml.attribute("time-stamp", std::to_string(event.time_stamp).c_str());
				});
			}
		});
	});

	_shared.event_log.clear();

	return _profile_data.cap();
}

Genode::Number_of_bytes Taskloader_session_component::_trace_quota()
{
	Genode::Xml_node launchpad_node = Genode::config()->xml_node().sub_node("trace");
	return launchpad_node.attribute_value<Genode::Number_of_bytes>("quota", 1024 * 1024);
}

Genode::Number_of_bytes Taskloader_session_component::_trace_buf_size()
{
	Genode::Xml_node launchpad_node = Genode::config()->xml_node().sub_node("trace");
	return launchpad_node.attribute_value<Genode::Number_of_bytes>("buf-size", 64 * 1024);
}

Genode::Number_of_bytes Taskloader_session_component::_profile_ds_size()
{
	Genode::Xml_node launchpad_node = Genode::config()->xml_node().sub_node("profile");
	return launchpad_node.attribute_value<Genode::Number_of_bytes>("ds-size", 128 * 1024);
}
