#include <taskloader/task.h>
#include <cstring>
#include <vector>
#include <base/lock.h>

Task::Child_policy::Child_policy(Genode::Env &env, Genode::Allocator &alloc, Task& task) :
	_env(env),_alloc(alloc),
	_session_requester(env.ep().rpc_ep(), _env.ram(), _env.rm()),
	_task{task},
	_config_policy{"config", task._config.cap(), &task._child_ep},
	_binary_policy{name(), task._shared.binaries.at(task._desc.binary_name)->cap(), &task._child_ep},
	_active{true},
	soft_exit{false},
	_child(_env.rm(), _env.ep().rpc_ep(), *this)
{ }

void Task::Child_policy::destruct()
{
	//unsigned long long before = _task._shared.timer.elapsed_ms();
	//Genode::log("destruct");
	_task._meta->~Meta_ex();
	_task._meta = nullptr;
	//unsigned long long after = _task._shared.timer.elapsed_ms();
	//Genode::log("Destruct: ",after-before);
}

void Task::send_profile()
{
	Dom0_server::Connection dom0{_env};
	Genode::Attached_ram_dataspace _profile_data(_env.ram(),_env.rm(), 100000);
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
	dom0.send_profile(_profile_data.cap());
}

void Task::Child_policy::exit(int exit_value)
{
	// Already exited, waiting for destruction.
	if (!_active)
	{
		return;
	}
	_active = false;

	if(soft_exit)
	{
		soft_exit=false;
		destruct();
		return;
	}

	Task::Event::Type type;
	switch (exit_value)
	{
		
		case 0:
			type = Event::EXIT;
			_active=true;
			soft_exit=true;
			break;
		case 17:

			type = Event::EXIT_CRITICAL;
			destruct();
			break;
		case 19:
			type = Event::EXIT_EXTERNAL;
			destruct();
			break;
		case 20:
			type = Event::EXIT_PERIOD;
			destruct();
			break;
		case 21:
			type = Event::OUT_OF_CAPS;
			_active=true;
			soft_exit=true;
			break;
		case 22:
			type = Event::OUT_OF_QUOTA;
			_active=true;
			soft_exit=true;
			break;
		default:
			type = Event::EXIT_ERROR;
			destruct();
			break;

	}
	
	_task.log_profile_data(type, _task._desc.id, _task._shared);
	if(_task.jobs_done())
	{
		type=Task::Event::JOBS_DONE;
		_task.log_profile_data(type, _task._desc.id, _task._shared);
	}
	_task.send_profile();
	return;
}

Genode::Child_policy::Name Task::Child_policy::name() const 
{
	return _task.name().c_str();
}
bool Task::Child_policy::active() const
{
	return _active;
}

void Task::Child_policy::init(Genode::Pd_session &session, Genode::Capability<Genode::Pd_session> cap) { session.ref_account(_env.pd_session_cap());

	size_t const initial_session_costs =
		session_alloc_batch_size()*_child.session_factory().session_costs();

	Genode::Ram_quota const ram_quota { _task._desc.quota.value > initial_session_costs
	                          ? _task._desc.quota.value - initial_session_costs
	                          : 0 };

	Genode::Cap_quota const cap_quota { _task._desc.caps };

	try { _env.pd().transfer_quota(cap, cap_quota); }
	catch (Genode::Out_of_caps) {
		error(name(), ": unable to initialize cap quota of PD"); }

	try { _env.ram().transfer_quota(cap, ram_quota); }
	catch (Genode::Out_of_ram) {
		error(name(), ": unable to initialize RAM quota of PD"); }

	//Genode::log("RAM ",_task._desc.quota.value," caps ",_task._desc.caps);
}

void Task::Child_policy::init(Genode::Cpu_session &session, Genode::Capability<Genode::Cpu_session> cap)
{
	static size_t avail = Genode::Cpu_session::quota_lim_upscale( 100, 100);
	size_t const   need = Genode::Cpu_session::quota_lim_upscale( 100, 100);
	size_t need_adj = 0;

	if (need > avail || avail == 0) {
		//warn_insuff_quota(Genode::Cpu_session::quota_lim_downscale(avail, 100));
		need_adj = Genode::Cpu_session::quota_lim_upscale(100, 100);
		avail    = 0;
	} else {
		need_adj = Genode::Cpu_session::quota_lim_upscale(need, avail);
		avail   -= need;
	}
	session.ref_account(_env.cpu_session_cap());
	_env.cpu().transfer_quota(cap, need_adj);
}


Genode::Service &Task::Child_policy::resolve_session_request(Genode::Service::Name const &name, Genode::Session_state::Args const &args)
{
	Genode::Service* service = nullptr;

	if ((service = _binary_policy.resolve_session_request(name.string(), args.string())))
	{
		return *service;
	}
	if ((service = _config_policy.resolve_session_request(name.string(), args.string())))
	{
		return *service;
	}
	return find_service(_task._shared.parent_services, name);
}

void Task::Child_policy::resource_request(Genode::Parent::Resource_args const &args)
{
	char foo[3];
	strncpy(foo,args.string(),3);
	const char *cap="cap";
	const char *ram="ram";
	if(!strcmp(foo,cap)) _task._meta->policy._child.exit(21);
	if(!strcmp(foo,ram)) _task._meta->policy._child.exit(22);
}

Genode::Affinity Task::Child_policy::filter_session_affinity(Genode::Affinity const &session_affinity)
{
	Genode::Affinity::Space    const &child_space    = _task._affinity.space();
	Genode::Affinity::Location const &child_location = _task._affinity.location();

	/* check if no valid affinity space was specified */
	if (session_affinity.space().total() == 0)
		return Genode::Affinity(child_space, child_location);

	Genode::Affinity::Space    const &session_space    = session_affinity.space();
	Genode::Affinity::Location const &session_location = session_affinity.location();

	/* scale resolution of resulting space */
	Genode::Affinity::Space space(child_space.multiply(session_space));

	/* subordinate session affinity to child affinity subspace */
	Genode::Affinity::Location location(child_location
	                            .multiply_position(session_space)
	                            .transpose(session_location.xpos(),
	                                       session_location.ypos()));

	return Genode::Affinity(space, location);
}

void Task::Child_policy::filter_session_args(Genode::Service::Name const &service,
                                      char *args, size_t args_len)
{
	long _priority=(long)_task._desc.priority;
	
	long _prio_levels_log2 = Genode::log2(128);
	if (service == Genode::Cpu_session::service_name() && _prio_levels_log2 > 0) {

		unsigned long priority = Genode::Arg_string::find_arg(args, "priority").ulong_value(0);
		/* clamp priority value to valid range */
		priority = Genode::min((unsigned)Genode::Cpu_session::PRIORITY_LIMIT - 1, priority);

		long discarded_prio_lsb_bits_mask = (1 << _prio_levels_log2) - 1;
		if (priority & discarded_prio_lsb_bits_mask)
			Genode::warning("priority band too small, losing least-significant priority bits");

		/* assign child priority to the most significant priority bits */
		
		priority |= _priority*(Genode::Cpu_session::PRIORITY_LIMIT >> _prio_levels_log2);

		/* override priority when delegating the session request to the parent */
		Genode::String<64> value { Genode::Hex(priority) };
		Genode::Arg_string::set_arg(args, args_len, "priority", value.string());
	}
}

void Task::Child_policy::announce_service(Genode::Service::Name const &) 
{

}


Task::Meta_ex::Meta_ex(Genode::Env &env, Task& task) :
		_env(env),
		policy{_env, _heap, task}
{
}



const char* Task::Event::type_name(Type type)
{
	switch (type)
	{
		case START: return "START";
		case EXIT: return "EXIT";
		case EXIT_CRITICAL: return "EXIT_CRITICAL";
		case EXIT_ERROR: return "EXIT_ERROR";
		case EXIT_EXTERNAL: return "EXIT_EXTERNAL";
		case EXIT_PERIOD: return "EXIT_PERIOD";
		case EXTERNAL: return "EXTERNAL";
		case NOT_SCHEDULED: return "NOT_SCHEDULED";
		case JOBS_DONE: return "JOBS_DONE";
		case OUT_OF_QUOTA: return "OUT_OF_QUOTA";
		case OUT_OF_CAPS: return "OUT_OF_CAPS";
		default: return "UNKNOWN";
	}
}



Task::Shared_data::Shared_data(Genode::Env &env, Task::Parent_services &parent_services, Task::Child_services &child_services, size_t trace_quota, size_t trace_buf_size) :
	_env(env),
	binaries{},
	parent_services(parent_services),
	child_services(child_services),
	trace{_env, trace_quota, trace_buf_size, 0}
{
	
}

Task::Task(Genode::Env &env, Shared_data& shared, const Genode::Xml_node& node)://, Sched_controller::Connection* ctrl) :
		_env(env),
		_shared(shared),
		_desc{
			_get_node_value<unsigned int>(node, "id"),
			_get_node_value<unsigned int>(node, "executiontime"),
			_get_node_value<unsigned int>(node, "criticaltime"),
			_get_node_value<unsigned long>(node, "priority"),
			_get_node_value<unsigned int>(node, "deadline"),
			_get_node_value<unsigned int>(node, "period"),
			_get_node_value<unsigned int>(node, "offset"),
			_get_node_value<unsigned int>(node, "numberofjobs"),
			_get_node_value<Genode::Number_of_bytes>(node, "quota"),
			_get_node_value<unsigned int>(node, "caps"),
			_get_node_value(node, "pkg", 32, ""),
			_get_node_value<unsigned int>(node, "cores"),
			_get_node_value<unsigned int>(node, "coreoffset")},
		_config{_env.ram(), _env.rm(), node.sub_node("config").size()},
		_name{_make_name()},
		_iteration{0},
		_paused{true},
		_start_timer{_env},
		_kill_timer{_env},
		_start_dispatcher{_env.ep(), *this, &Task::_start},
		_kill_dispatcher{_env.ep(), *this, &Task::_kill_crit},
		_idle_dispatcher{_env.ep(), *this, &Task::_idle},
		_child_ep{&_env.pd(), 12 * 1024, _name.c_str()},
		_meta{nullptr},
		_schedulable(true),
		_affinity{Genode::Affinity::Space{_desc.cores,1}, Genode::Affinity::Location{(int)_desc.coreoffset,0}}//,
		//_controller(ctrl)
{
	if(_desc.priority>127)
	{
		_desc.priority=0;
	}
	if(_desc.priority<=127)
	{
		_desc.deadline=0;
	}
	//child config
	const Genode::Xml_node& config_node = node.sub_node("config");
	std::strncpy(_config.local_addr<char>(), config_node.addr(), config_node.size());
}

Task::~Task()
{
}

void Task::setSchedulable(bool schedulable)
{
	_schedulable = schedulable;
}

bool Task::isSchedulable()
{
	return _schedulable;
}

unsigned int Task::get_id()
{
	return _desc.id;
}

Task::Shared_data& Task::get_shared()
{
	return _shared;
}

bool Task::jobs_done()
{
	return _iteration==_desc.number_of_jobs;
}

Rq_task::Rq_task Task::getRqTask()
{
	Rq_task::Rq_task rq_task;
	rq_task.task_id = _desc.id;
	rq_task.wcet = _desc.execution_time;
	rq_task.prio = _desc.priority;
	rq_task.inter_arrival = _desc.period;
	rq_task.deadline = _desc.deadline*1000;
	strcpy(rq_task.name, _name.c_str());
	
	if(_desc.deadline > 0)
	{
		rq_task.task_class = Rq_task::Task_class::lo;
		rq_task.task_strategy = Rq_task::Task_strategy::deadline;
	}
	else
	{
		rq_task.task_class = Rq_task::Task_class::hi;
		rq_task.task_strategy = Rq_task::Task_strategy::priority;
	}
	
	return rq_task;
}

void Task::run()
{
	_paused = false;
	Timer::Connection offset_timer {_env};
	offset_timer.msleep(_desc.offset);
	
	_start_timer.sigh(_start_dispatcher);
	_kill_timer.sigh(_kill_dispatcher);

	_start_timer.trigger_periodic(_desc.period * 1000);
}

void Task::stop()
{
	Genode::log("Stopping task ", _name.c_str());
	_paused = true;
	_stop_timers();
	_kill(19);
}

std::string Task::name() const
{
	return _name;
}

bool Task::running() const
{
	return _meta != nullptr;
}

const Task::Description& Task::desc() const
{
	return _desc;
}

Task* Task::task_by_name(std::list<Task>& tasks, const std::string& name)
{
	for (Task& task : tasks)
	{
		if (task.name() == name)
		{
			return &task;
		}
	}
	return nullptr;
}

void Task::log_profile_data(Event::Type type, int task_id, Shared_data& shared)
{
	// Lock to avoid race conditions as this may be called by the child's thread.
	Genode::Lock::Guard guard(shared.log_lock);

	shared.event_log.emplace_back();
	Event& event = shared.event_log.back();

	event.type = type;
	event.task_id = task_id;
	event.time_stamp = shared.timer.elapsed_us()/1000;
}
void Task::Child_policy::wakeup_async_service() 
{
	_session_requester.trigger_update();
}

std::string Task::_make_name() const
{
	char id[4];
	snprintf(id, sizeof(id), "%.2d.", _desc.id);
	return std::string(id) + _desc.binary_name;
}

void Task::_start()
{
	if (jobs_done())
	{
		_stop_timers();
		//trigger optimization to let all remaining tasks finish running
		//_controller->scheduling_allowed(_name.c_str());
		_kill(20);
		send_profile();
		return;
	}
	if(_desc.deadline>0)
	{
		/*if(!_controller->scheduling_allowed(_name.c_str()))
		{
			PINF("%s NOT ALLOWED!", _name.c_str());
			Task::Event::Type type=Task::Event::NOT_SCHEDULED;
			Task::log_profile_data(type, get_id(), get_shared());
			return;
		}
		PINF("%s ALLOWED!", _name.c_str());*/
	}
	if (_paused)
	{
		// This might happen if start timeout is triggered before a stop call but is handled after.
		return;
	}

	if (running())
	{
		//Genode::log("Trying to start ",_name.c_str()," but previous instance still running or undestroyed. Abort.");
		_kill(20);
		return;
	}

	// Check if binary has already been received.
	auto bin_it = _shared.binaries.find(_desc.binary_name);
	if (bin_it == _shared.binaries.end())
	{
		PERR("Binary %s for task %s not found, possibly not yet received by dom0.", _desc.binary_name.c_str(), _name.c_str());
		return;
	}

	if ((size_t)_desc.quota.value < 512 * 1024)
	{
		PWRN("Warning: RAM quota for %s might be too low to hold meta data.", _name.c_str());
	}

	// Dispatch kill timer after critical time.
	if (_desc.critical_time > 0)
	{
		_kill_timer.trigger_once(_desc.critical_time * 1000);
	}

	// Abort if RAM quota insufficient. Alternatively, we could give all remaining quota to the child.
	if (_desc.quota.value > _env.ram().avail_ram().value) {
		Genode::log("Not enough RAM quota for task" , _name.c_str(), "requested:",_desc.quota, "available:", _env.ram().avail_ram().value);
		return;
	}

	if(_meta->policy.active())
	{
		Genode::log("Still active!");
		return;
	}

	try
	{
		//unsigned long long before = _shared.timer.elapsed_ms();
		_meta = new (&_shared.heap) Meta_ex(_env, *this);
		//unsigned long long after = _shared.timer.elapsed_ms();
		//Genode::log("Construction: ",after-before);
	}
	catch (Genode::Cpu_session::Thread_creation_failed)
	{
		PWRN("Failed to create child - Cpu_session::Thread_creation_failed");
		return;
	}
	catch (...)
	{
		PWRN("Failed to create child - unknown reason");
		return;
	}
	_iteration++;

log_profile_data(Event::START, _desc.id, _shared);
}

void Task::_kill_crit()
{
	if (!_paused)
	{
		_kill(17);
	}
}

void Task::_kill(int exit_value)
{
	//Genode::log("kill ",exit_value);
	if (_meta && _meta->policy.active())
	{
		_meta->policy._child.exit(exit_value);
	}
}

void Task::_idle()
{
	// Do nothing.
}

void Task::_stop_timers()
{
	_stop_kill_timer();
	_stop_start_timer();
}

void Task::_stop_kill_timer()
{
	_kill_timer.sigh(_idle_dispatcher);
	_kill_timer.trigger_once(0);
}

void Task::_stop_start_timer()
{
	_start_timer.sigh(_idle_dispatcher);
	_start_timer.trigger_once(0);
}

std::string Task::_get_node_value(const Genode::Xml_node& config_node, const char* type, size_t max_len, const std::string& default_val)
{
	if (config_node.has_sub_node(type))
	{
		std::vector<char> out(max_len);
		config_node.sub_node(type).value(out.data(), max_len);
		return out.data();
	}
	return default_val;
}
