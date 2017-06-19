#include "taskloader_session_component.h"
#include <timer_session/connection.h>
#include <base/env.h>
#include <base/printf.h>
#include <base/process.h>
#include <util/xml_node.h>
#include <util/xml_generator.h>

#include <string>

#include <util/string.h>

#include <trace_session/connection.h>

Taskloader_session_component::Taskloader_session_component(Server::Entrypoint& ep) :
	_ep{ep},
	_shared{_trace_quota(), _trace_buf_size()},
	_cap{},
	_quota{Genode::env()->ram_session()->quota()}
{
	// Load dynamic linker for dynamically linked binaries.
	static Genode::Rom_connection ldso_rom("ld.lib.so");
	Genode::Process::dynamic_linker(ldso_rom.dataspace());

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
	Genode::Rm_session* rm = Genode::env()->rm_session();
	const char* xml = rm->attach(xml_ds_cap);
	PDBG("Parsing XML file:\n%s", xml);
	Genode::Xml_node root(xml);
	Rq_task::Rq_task rq_task;

	PDBG("Emplace Tasks in list");
	const auto fn = [this, &rq_task] (const Genode::Xml_node& node)
	{
		_shared.tasks.emplace_back(_ep, _cap, _shared, node);
		_shared.tasks.back().getRqTask(rq_task);
		PDBG("id: %d, prio: %d, wcet: %d, period: %d", rq_task.task_id, rq_task.prio, rq_task.wcet, rq_task.inter_arrival);

		int result = sched.new_task(rq_task);
		PDBG("result = %d", result);
		if (result != 0){
			PINF("Task with id %d was not accepted by the controller", rq_task.task_id);
			_shared.tasks.pop_back();
		}
	};

	root.for_each_sub_node("periodictask", fn);
	rm->detach(xml);
}

void Taskloader_session_component::clear_tasks()
{
	PDBG("Clearing %d task%s. Binaries still held.", _shared.tasks.size(), _shared.tasks.size() == 1 ? "" : "s");
	stop();

	// Wait for task destruction.
	_shared.timer.msleep(500);
	_shared.tasks.clear();
}

Genode::Ram_dataspace_capability Taskloader_session_component::binary_ds(Genode::Ram_dataspace_capability name_ds_cap, size_t size)
{
	Genode::Rm_session* rm = Genode::env()->rm_session();
	const char* name = rm->attach(name_ds_cap);
	PDBG("Reserving %d bytes for binary %s", size, name);
	Genode::Ram_session* ram = Genode::env()->ram_session();

	// Hoorray for C++ syntax. This basically forwards ctor arguments, constructing the dataspace in-place so there is no copy or dtor call involved which may invalidate the attached pointer.
	// Also, emplace returns a <iterator, bool> pair indicating insertion success, so we need .first to get the map iterator and ->second to get the actual dataspace.
	Genode::Attached_ram_dataspace& ds = _shared.binaries.emplace(std::piecewise_construct, std::make_tuple(name), std::make_tuple(ram, size)).first->second;
	rm->detach(name);
	return ds.cap();
}

void Taskloader_session_component::start()
{
	PINF("Starting %d task%s.", _shared.tasks.size(), _shared.tasks.size() == 1 ? "" : "s");
	for (Task& task : _shared.tasks)
	{
		task.run();
	}
}

void Taskloader_session_component::stop()
{
	PINF("Stopping all tasks.");
	for (Task& task : _shared.tasks)
	{
		task.stop();
	}
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
