#include <timer_session/connection.h>
#include <base/env.h>
#include <base/printf.h>
#include <util/xml_node.h>
#include <util/xml_generator.h>
#include <base/attached_rom_dataspace.h>
#include <string>
#include <util/string.h>
#include <trace_session/connection.h>
#include <taskloader/taskloader.h>

namespace Taskloader{
	Taskloader::Taskloader(Genode::Env &env, Genode::Registry<Genode::Registered<Genode::Parent_service> > &parent_services) :
		_env(env),
		_parent_services(parent_services),
		
		_shared{_env, _parent_services, _child_services, _trace_quota(), _trace_buf_size()},
		_profile_data{_env.ram(),_env.rm(), _profile_ds_size()}
	{
	}

	Taskloader::~Taskloader()
	{
	}
	// Create tasks in idle state from XML description.
	void Taskloader::add_tasks(Genode::Ram_dataspace_capability xml_ds_cap)
	{
		Genode::Region_map* rm = &(_env.rm());
		const char* xml = rm->attach(xml_ds_cap);
		//Genode::log("Parsing XML file: ", xml);
		Genode::Xml_node root(xml);
		Rq_task::Rq_task rq_task;

		//Update rq_buffer before adding tasks for online analyses to core 1
		//sched.update_rq_buffer(1);

		const auto fn = [this, &rq_task] (const Genode::Xml_node& node)
		{
			_shared.tasks.emplace_back(_env, _shared, node);
			//Add task to Controller to perform a schedulability test for core 1
			rq_task = _shared.tasks.back().getRqTask();
			//int time_before=_shared.timer.elapsed_ms();
			int result = 0;//sched.new_task(rq_task, 1);
			//Genode::log("Done Analysis. Took: ",_shared.timer.elapsed_ms()-time_before);
			if (result != 0){
				//Genode::log("Task with id ",rq_task.task_id," was not accepted by the controller");
				_shared.tasks.back().setSchedulable(false);
			}
			else{
				//Genode::log("Task with id ",rq_task.task_id," was accepted by the controller");
				_shared.tasks.back().setSchedulable(true);
			}
		};

		root.for_each_sub_node("periodictask", fn);

		//sched.set_opt_goal(xml_ds_cap);
		rm->detach(xml);
	}

	// Allocate and return a capability of a new dataspace to be used for a task binary.
	Genode::Ram_dataspace_capability Taskloader::binary_ds(Genode::Ram_dataspace_capability name_ds_cap, size_t size)
	{
		Genode::Attached_ram_dataspace *bin_ds = new (&_heap) Genode::Attached_ram_dataspace(_env.ram(), _env.rm(), size);
		const char* name = _env.rm().attach(name_ds_cap);
		_shared.binaries.insert({name,bin_ds});
		_env.rm().detach(name);
		return bin_ds->cap();
	}

	// Destruct all tasks.
	void Taskloader::clear_tasks()
	{
		for (Task& task : _shared.tasks)
		{
			task.stop();
		}
		_shared.timer.msleep(1000);
		_shared.tasks.clear();
	}

	// Start idle tasks.
	void Taskloader::start()
	{
		//Genode::log("Starting ", _shared.tasks.size()," task", _shared.tasks.size() == 1 ? "" : "s", ".");
		for (Task& task : _shared.tasks)
		{
			if(task.isSchedulable())
			{
				task.run();
			}
			else
			{
				Task::Event::Type type=Task::Event::NOT_SCHEDULED;
				task.log_profile_data(type, task.get_id(), task.get_shared());
			}
		}
	}

	// Stop all tasks.
	void Taskloader::stop()
	{
		//Genode::log("Stopping ", _shared.tasks.size()," task", _shared.tasks.size() == 1 ? "" : "s", ".");
		for (Task& task : _shared.tasks)
		{
			task.stop();
		}
	}

	Genode::Ram_dataspace_capability Taskloader::profile_data()
	{
		//Genode::log("profile data");
		_profile_data.realloc(&_env.ram(), _profile_ds_size());
		/* There is some shitty race conditions going on, that produces empty logs */
		if(_shared.event_log.size())
		{
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
		}



	return _profile_data.cap();
	}

	Genode::Number_of_bytes Taskloader::_trace_quota()
	{	
		Genode::Attached_rom_dataspace config(_env, "config");
		Genode::Xml_node launchpad_node = config.xml().sub_node("trace");
		return launchpad_node.attribute_value<Genode::Number_of_bytes>("quota", 1024 * 1024);
	}

	Genode::Number_of_bytes Taskloader::_trace_buf_size()
	{
		Genode::Attached_rom_dataspace config(_env, "config");
		Genode::Xml_node launchpad_node = config.xml().sub_node("trace");
		return launchpad_node.attribute_value<Genode::Number_of_bytes>("buf-size", 64 * 1024);
	}

	Genode::Number_of_bytes Taskloader::_profile_ds_size()
	{
		Genode::Attached_rom_dataspace config(_env, "config");
		Genode::Xml_node launchpad_node = config.xml().sub_node("profile");
		return launchpad_node.attribute_value<Genode::Number_of_bytes>("ds-size", 128 * 1024);
	}
}
