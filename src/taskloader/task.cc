#include <taskloader/task.h>
#include <cstring>
#include <vector>
#include <base/lock.h>

Task::Child_policy::Child_policy(Genode::Env &env, Genode::Allocator &alloc, Task& task) :
	_env(env),_alloc(alloc),
	_session_requester(env.ep().rpc_ep(), _env.ram(), _env.rm()),
	_task{&task},
	//_config_policy{name(), task._config.cap(), &task._child_ep},
	_binary_policy{name(), task._shared.binaries.at(task._desc.binary_name).cap(), &task._child_ep},
	_active{true},
	_child(_env.rm(), _env.ep().rpc_ep(), *this)
{ }

void Task::Child_policy::exit(int exit_value)
{
	Genode::Lock::Guard guard(_exit_lock);
	// Already exited, waiting for destruction.
	if (!_active)
	{
		return;
	}
	_active = false;

	Task::Event::Type type;
	switch (exit_value)
	{
		case 0:
			type = Event::EXIT; break;
		case 17:
			type = Event::EXIT_CRITICAL; break;
		case 19:
			type = Event::EXIT_EXTERNAL; break;
		case 20:
			type = Event::EXIT_PERIOD; break;
		default:
			type = Event::EXIT_ERROR;
	}
	
	_task->log_profile_data(type, _task->_desc.id, _task->_shared);
	if(_task->jobs_done())
	{
		type=Task::Event::JOBS_DONE;
		_task->log_profile_data(type, _task->_desc.id, _task->_shared);
	}
	Task::_child_destructor.submit_for_destruction(_task);
}

Genode::Child_policy::Name Task::Child_policy::name() const 
{
	return _task->name().c_str();
}
bool Task::Child_policy::active() const
{
	return _active;
}

void Task::Child_policy::init(Genode::Pd_session &session, Genode::Capability<Genode::Pd_session> cap) { session.ref_account(_env.pd_session_cap());

	size_t const initial_session_costs =
		session_alloc_batch_size()*_child.session_factory().session_costs();

	Genode::Ram_quota const ram_quota { 100000 > initial_session_costs
	                          ? 100000 - initial_session_costs
	                          : 0 };

	Genode::Cap_quota const cap_quota { 100 };

	try { _env.pd().transfer_quota(cap, cap_quota); }
	catch (Genode::Out_of_caps) {
		error(name(), ": unable to initialize cap quota of PD"); }

	try { _env.ram().transfer_quota(cap, ram_quota); }
	catch (Genode::Out_of_ram) {
		error(name(), ": unable to initialize RAM quota of PD"); }
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
	/*
	cpu{_env,
	    task.name().c_str(),
	    long(128-task._desc.priority)*(Genode::Cpu_session::PRIORITY_LIMIT >> Genode::log2(128)),
	    unsigned(task._desc.deadline*1000),
	    Genode::Affinity(Genode::Affinity::Space(4,1), Genode::Affinity::Location(1,0))
	*/
}


Genode::Service &Task::Child_policy::resolve_session_request(Genode::Service::Name const &name, Genode::Session_state::Args const &args)
{
	Genode::Service* service = nullptr;
	//Genode::log(name, " session request ", args);

	if ((service = _binary_policy.resolve_session_request(name.string(), args.string())))
	{
		//PINF("binary policy");
		return *service;
	}
	/*if ((service = _config_policy.resolve_session_request(name.string(), args.string())))
	{
		PINF("config policy");
		return *service;
	}*/
	//PINF("parent policy");
	return find_service(_task->_shared.parent_services, name);
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
			_get_node_value(node, "pkg", 32, "")},
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
		_schedulable(true)//,
		//_controller(ctrl)
{
	const Genode::Xml_node& config_node = node.sub_node("config");
	std::strncpy(_config.local_addr<char>(), config_node.addr(), config_node.size());
	Genode::log("id:", _desc.id,", name:" , _name.c_str(),", prio:" , _desc.priority,", deadline:", _desc.deadline,", wcet:" , _desc.execution_time,", period:" , _desc.period);
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
	Genode::log("iteration: ",_iteration," num jobs: ",_desc.number_of_jobs," name: ", _name.c_str());
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

	if (_desc.period > 0)
	{
		_start_timer.trigger_periodic(_desc.period * 1000);
	}
	else
	{
		_start();
	}
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
	static const size_t MAX_NUM_SUBJECTS = 128;
	// Lock to avoid race conditions as this may be called by the child's thread.
	Genode::Lock::Guard guard(shared.log_lock);

	Genode::Trace::Subject_id subjects[MAX_NUM_SUBJECTS];
	const size_t num_subjects = shared.trace.subjects(subjects, MAX_NUM_SUBJECTS);
	Genode::Trace::CPU_info info;
	Genode::Trace::RAM_info ram_info;

	shared.event_log.emplace_back();
	Event& event = shared.event_log.back();

	event.type = type;
	event.task_id = task_id;
	event.time_stamp = shared.timer.elapsed_us()/1000;

	Event::Task_info* task_manager_info = nullptr;

	for (Genode::Trace::Subject_id* subject = subjects; subject < subjects + num_subjects; ++subject)
	{
		info = shared.trace.cpu_info(*subject);
		ram_info = shared.trace.ram_info(*subject);
		event.task_infos.emplace_back();
		Event::Task_info& task_info = event.task_infos.back();

		task_info.id = subject->id;
		task_info.session = ram_info.session_label().string(),
		task_info.thread = ram_info.thread_name().string(),
		task_info.state = info.state(),
		task_info.execution_time = info.execution_time().value;

		// Check if the session is started by this task manager (i.e., a managed task).
		size_t leaf_pos = task_info.session.rfind("task-manager -> ");
		Task* task = nullptr;
		if (leaf_pos < std::string::npos)
		{
			const std::string process = task_info.session.substr(leaf_pos + 16);
			if (process == task_info.thread)
			{
				task = task_by_name(shared.tasks, task_info.session.substr(leaf_pos + 16));
			}
		}
		if (task && task->running())
		{
			task_info.managed = true;
			task_info.managed_info.id = task->_desc.id;
			//task_info.managed_info.quota = task->_meta->pd.ram_quota().value;
			//task_info.managed_info.used = task->_meta->pd.used_ram().value;
			task_info.managed_info.iteration = task->_iteration;
		}
		// Check if this is task-manager itself.
		else if (task_info.session.rfind("task-manager") == task_info.session.length() - 12 && task_info.thread == "task-manager")
		{
			task_info.managed = true;
			task_info.managed_info.id = 0;
			task_info.managed_info.quota = _env.ram().ram_quota().value;
			task_info.managed_info.used = _env.ram().used_ram().value;
			task_info.managed_info.iteration = 0;

			// Hack: there are two task-manager processes. We only flag the more active one as managed.
			if (!task_manager_info)
			{
				task_manager_info = &task_info;
			}
			else if (task_manager_info->execution_time < task_info.execution_time)
			{
				task_manager_info->managed = false;
			}
		}
	}
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
		//trigger optimization to let all remaining tasks finish running
		//_controller->scheduling_allowed(_name.c_str());
		PINF("%s JOBS DONE!", _name.c_str());
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
		PINF("Trying to start %s but previous instance still running or undestroyed. Abort.\n", _name.c_str());
		Task::Event::Type type;
		type = Event::EXIT_PERIOD;
		Task::log_profile_data(type, _desc.id, _shared);
		Task::_child_destructor.submit_for_destruction(this);
		return;
	}

	// Check if binary has already been received.
	auto bin_it = _shared.binaries.find(_desc.binary_name);
	if (bin_it == _shared.binaries.end())
	{
		PERR("Binary %s for task %s not found, possibly not yet received by dom0.", _desc.binary_name.c_str(), _name.c_str());
		return;
	}

	//Genode::Attached_ram_dataspace& ds = bin_it->second;

	++_iteration;
	//PINF("Starting task %s with quota %u and priority %u in iteration %d", _name.c_str(), (size_t)_desc.quota, _desc.priority, _iteration);

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
		//PERR("Not enough RAM quota for task %s, requested: %d, available: %d", _name.c_str(), (size_t)_desc.quota, _env.ram().avail_ram().value);
		Genode::log("Not enough RAM quota for task" , _name.c_str(), "requested:",_desc.quota, "available:", _env.ram().avail_ram().value);
		return;
	}

	try
	{
		// Create child and activate entrypoint.
		Genode::log("Trying to create child");
		_meta = new (&_shared.heap) Meta_ex(_env, *this);
		//_child_ep.activate();
	}
	catch (Genode::Cpu_session::Thread_creation_failed)
	{
		PWRN("Failed to create child - Cpu_session::Thread_creation_failed");
	}
	catch (...)
	{
		PWRN("Failed to create child - unknown reason");
	}

log_profile_data(Event::START, _desc.id, _shared);
}

void Task::_kill_crit()
{
	if (!_paused)
	{
		PINF("Critical time reached for %s", _name.c_str());
		_kill(17);
	}
}

void Task::_kill(int exit_value)
{
	if (_meta && _meta->policy.active())
	{
		PINF("Force-exiting %s", _name.c_str());
		// Child::exit() is usually called from the child thread. Use this carefully.
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

Task::Child_destructor_thread::Child_destructor_thread() :
	Thread_deprecated{"child_destructor"},
	_lock{},
	_queued{}
{
	start();
}

void Task::Child_destructor_thread::submit_for_destruction(Task* task)
{
	Genode::log("Destructing ",task->_name.c_str());
	Genode::destroy(task->_shared.heap, task->_meta);
	task->_meta = nullptr;
	
}

void Task::Child_destructor_thread::entry()
{

}

Task::Child_destructor_thread Task::_child_destructor;
