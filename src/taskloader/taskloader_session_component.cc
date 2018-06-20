#include "taskloader_session_component.h"
#include <timer_session/connection.h>
#include <base/env.h>
#include <base/printf.h>
#include <util/xml_node.h>
#include <util/xml_generator.h>
#include <base/attached_rom_dataspace.h>
#include <string>

#include <util/string.h>

#include <trace_session/connection.h>

Taskloader_session_component::Taskloader_session_component(Genode::Env &env) :
	_env(env),
	_ep(env.ep()),
	_shared{_env, _parent_services, _child_services, _trace_quota(), _trace_buf_size()},
	//_cap{},
	_profile_data{_env.ram(),_env.rm(), _profile_ds_size()}
	//_quota(_env.ram().ram_quota().value)
{
	// Load dynamic linker for dynamically linked binaries.
	//static Genode::Rom_connection ldso_rom("ld.lib.so");
	//Genode::Process::dynamic_linker(ldso_rom.dataspace());

	// Names of services provided by the parent.
	static const char* names[] =
	{
		// core services
		"CAP", "RAM", "RM", "PD", "CPU", "IO_MEM", "IO_PORT",
		"IRQ", "ROM", "LOG", "SIGNAL", "Timer", "Nic"
	};
	/*
	for (const char* name : names)
	{
		Genode::Heap _heap { _env.ram(), _env.rm() };
		_shared.parent_services.insert(new (_heap) Genode::Parent_service(name));
	}
	*/
	for (unsigned i = 0; names[i]; i++)
		new (_heap) Task::Parent_service(_parent_services, names[i]);
}

Taskloader_session_component::~Taskloader_session_component()
{
}

void Taskloader_session_component::add_tasks(Genode::Ram_dataspace_capability xml_ds_cap)
{
	Genode::Region_map* rm = &(_env.rm());
	const char* xml = rm->attach(xml_ds_cap);
	if(verbose_debug) PINF("Parsing XML file:\n%s", xml);
	Genode::Xml_node root(xml);
	Rq_task::Rq_task rq_task;

	//Update rq_buffer before adding tasks for online analyses to core 1
	sched.update_rq_buffer(1);

	const auto fn = [this, &rq_task] (const Genode::Xml_node& node)
	{
		_shared.tasks.emplace_back(_env, _shared, node, &sched);
		//Add task to Controller to perform a schedulability test for core 1
		rq_task = _shared.tasks.back().getRqTask();
		int time_before=_shared.timer.elapsed_ms();
		int result = sched.new_task(rq_task, 1);
		Genode::log("Done Analysis. Took: %d",_shared.timer.elapsed_ms()-time_before);
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
	if(verbose_debug) Genode::log("Clearing %d task%s. Binaries still held.", _shared.tasks.size(), _shared.tasks.size() == 1 ? "" : "s");
	stop();

	// Wait for task destruction.
	_shared.timer.msleep(500);
	_shared.tasks.clear();
}

Genode::Ram_dataspace_capability Taskloader_session_component::binary_ds(Genode::Ram_dataspace_capability name_ds_cap, size_t size)
{
	Genode::Region_map* rm = &(_env.rm());
	const char* name = rm->attach(name_ds_cap);
	if(verbose_debug) Genode::log("Reserving %d bytes for binary %s", size, name);
	Genode::Ram_session* ram = &(_env.ram());

	// Hoorray for C++ syntax. This basically forwards ctor arguments, constructing the dataspace in-place so there is no copy or dtor call involved which may invalidate the attached pointer.
	// Also, emplace returns a <iterator, bool> pair indicating insertion success, so we need .first to get the map iterator and ->second to get the actual dataspace.
	Genode::Attached_ram_dataspace& ds = _shared.binaries.emplace(std::piecewise_construct, std::make_tuple(name), std::make_tuple(ram, size)).first->second;
	rm->detach(name);
	return ds.cap();
}

void Taskloader_session_component::start()
{
	if(verbose_debug) Genode::log("Starting ", _shared.tasks.size()," task", _shared.tasks.size() == 1 ? "" : "s", ".");
	for (Task& task : _shared.tasks)
	{
		if(task.isSchedulable())
		{
			Task::_child_start.submit_for_start(&task);
		}
		else
		{
			Task::Event::Type type=Task::Event::NOT_SCHEDULED;
			task.log_profile_data(type, task.get_id(), task.get_shared());
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
			//Task::_child_destructor.submit_for_destruction(&task);
			task._child_destructor.submit_for_destruction(&task);
		}
	}
}

Genode::Ram_dataspace_capability Taskloader_session_component::profile_data()
{
	_profile_data.realloc(&_env.ram(), _profile_ds_size());
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
	Genode::Attached_rom_dataspace config(_env, "config");
	Genode::Xml_node launchpad_node = config.xml().sub_node("trace");
	return launchpad_node.attribute_value<Genode::Number_of_bytes>("quota", 1024 * 1024);
}

Genode::Number_of_bytes Taskloader_session_component::_trace_buf_size()
{
	Genode::Attached_rom_dataspace config(_env, "config");
	Genode::Xml_node launchpad_node = config.xml().sub_node("trace");
	return launchpad_node.attribute_value<Genode::Number_of_bytes>("buf-size", 64 * 1024);
}

Genode::Number_of_bytes Taskloader_session_component::_profile_ds_size()
{
	Genode::Attached_rom_dataspace config(_env, "config");
	Genode::Xml_node launchpad_node = config.xml().sub_node("profile");
	return launchpad_node.attribute_value<Genode::Number_of_bytes>("ds-size", 128 * 1024);
}
