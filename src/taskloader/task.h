#pragma once

#include <dom0-HW/dom0_connection.h>

#include <list>
#include <unordered_map>
#include <elf.h>

#include <base/service.h>
//#include <cap_session/connection.h>
#include <base/registry.h>
//#include <init/child.h>
#include <init/child_policy.h>
#include <base/child.h>
#include <base/attached_ram_dataspace.h>
#include <os/server.h>
#include <os/signal_rpc_dispatcher.h>
#include <timer_session/connection.h>
#include <trace_session/connection.h>
#include <util/noncopyable.h>
#include <util/xml_node.h>
#include <rm_session/connection.h>
#include "sched_controller_session/connection.h"
#include "mon_manager/mon_manager.h"
#include <base/service.h>
#include <base/affinity.h>
#include <os/session_requester.h>



// Noncopyable because dataspaces might get invalidated.
class Task : Genode::Noncopyable
{
public:
	Genode::Env &_env;
	//static Genode::Env &_env1;
	Genode::Heap _heap {_env.ram(), _env.rm()};
	// Policy for handling binary and service requests.
	struct Child_policy : public Genode::Child_policy,
			public  Genode::Async_service::Wakeup
	{
	private:
		Genode::Env &_env;
		Genode::Allocator &_alloc;
		//Task::Child_destructor_thread &_child_destructor;
		Genode::Session_requester _session_requester;
		template <typename T>
		static Genode::Service *_find_service(Genode::Registry<T> &services,
		                                      Genode::Service::Name const &name)
		{
			Genode::Service *service = nullptr;
			services.for_each([&] (T &s) {
				if (!service && (s.name() == name))
					service = &s; });
			return service;
		}
		void wakeup_async_service() override;
	public:
		//typedef Child_policy::Name Name;
		//typedef Session_label      Label;
		
		//Child_policy(Genode::Env &env, Genode::Allocator &alloc, Task::Child_destructor_thread& child_destructor, Task& task);
		Child_policy(Genode::Env &env, Genode::Allocator &alloc, Task& task);
		Child_policy(const Child_policy&);
		Child_policy& operator = (const Child_policy&);
		~Child_policy()
		{
			using namespace Genode;
			/* unregister services */
			_task->_shared.child_services.for_each(
				[&] (Task::Child_service &service) {
					if (service.has_id_space(_session_requester.id_space()))
						Genode::destroy(_alloc, &service); });
		}
		Genode::Pd_session           &ref_pd()           override { return _env.pd(); }
		Genode::Pd_session_capability ref_pd_cap() const override { return _env.pd_session_cap(); }	
		// All methods below will be called from the child thread most of the time, and not the task-manager thread. Watch out for race conditions.
		virtual void exit(int exit_value) override;

		//virtual const char *name() const override;
		virtual Genode::Child_policy::Name name() const override;
		//virtual void resource_request(Genode::Parent::Resource_args const &) override; 
		Genode::Service &resolve_session_request(Genode::Service::Name const &service_name, Genode::Session_state::Args const &args) override;
		//void filter_session_args(const char *service, char *args, Genode::size_t args_len) override;
		//void filter_session_args(Service::Name service, char *args, Genode::size_t args_len) override;
		/*
		bool announce_service(
			const char *service_name,
			Genode::Root_capability root,
			Genode::Allocator *alloc) override;
		void unregister_services() override;
		*/
		void announce_service(Genode::Service::Name const &service_name) override;
		virtual bool active() const;
		
	protected:
		Task* _task;
		Init::Child_policy_provide_rom_file _config_policy;
		Genode::Lock _exit_lock {};
		bool _active;
		Genode::Child _child;
	};

	// Part of Meta_ex that needs constructor initialization (transferring ram quota).
	struct Meta
	{
	public:
		Genode::Env &_env;
		//Meta(Genode::Env &env, const Task& task);
		Meta(Genode::Env &env, Task& task);	
		//Genode::Ram_connection ram;
		Genode::Cpu_connection cpu;
		//Genode::Rm_connection rm;
		Genode::Pd_connection pd;
		//Genode::Server server;
	};

	// Meta data that needs to be dynamically allocated on each start request.
	struct Meta_ex : Meta
	{
	public:
		Genode::Env& _env;
		Genode::Heap _heap {_env.ram(), _env.rm()};
		Meta_ex(Genode::Env& env, Task& task);
		Mon_manager::Connection mon {};
		Child_policy policy;
		//Genode::Child::Initial_thread _initial_thread;
		Genode::Rom_connection ldso_rom;
		Genode::Region_map_client rmc ;
		Genode::Child child;
	};

	// Single event of the profiling log data.
	struct Event
	{
		struct Task_info
		{
			struct Managed_info
			{
				// Task manager id.
				int id {};
				size_t quota {};
				size_t used {};
				int iteration {};
			};

			// Trace subject id. Pretty much useless.
			unsigned id {};

			std::string session {};
			std::string thread {};
			Genode::Trace::CPU_info::State state {};
			unsigned long long execution_time {};

			// Managed by the task manager.
			bool managed {};
			Managed_info managed_info {};
		};
		enum Type { START = 0, EXIT, EXIT_CRITICAL, EXIT_ERROR, EXIT_EXTERNAL, EXIT_PERIOD, EXTERNAL, NOT_SCHEDULED, JOBS_DONE, OUT_OF_QUOTA };

		static const char* type_name(Type type);

		// Event trigger type.
		Type type {};

		// Task that triggered this event. -1 for EXTERNAL. 0 for task-manager.
		int task_id {};

		// Time of trigger.
		unsigned long time_stamp {};

		std::list<Task_info> task_infos {};
	};

	struct Description
	{
		unsigned int id;
		//int id;
		unsigned int execution_time;
		unsigned int critical_time;
		unsigned long priority;
		unsigned int deadline;
		unsigned int period;
		unsigned int offset;
		unsigned int number_of_jobs;
		Genode::Ram_quota quota;
		std::string binary_name;
	};
	
	typedef Genode::Registered<Genode::Parent_service> Parent_service;
	typedef Genode::Registered<Genode::Child_service>  Child_service;
	typedef Genode::Registry<Parent_service>           Parent_services;
	typedef Genode::Registry<Child_service>  Child_services;
	// Shared objects. There is only one instance per task manager. Rest are all references.
	struct Shared_data
	{
		Genode::Env &_env;
		Shared_data(Genode::Env &env, Task::Parent_services &parent_services, Task::Child_services &child_services, size_t trace_quota, size_t trace_buf_size);

		// All binaries loaded by the task manager.
		std::unordered_map<std::string, Genode::Attached_ram_dataspace> binaries;

		// Heap on which to create the init child.
		//Genode::Heap heap;
		Genode::Heap heap { _env.ram(), _env.rm() };

		// Core services provided by the parent.
		//Genode::Service_registry parent_services;
		Task::Parent_services &parent_services;
		// Services provided by the started children, if any.
		//Genode::Service_registry child_services;
		Task::Child_services  &child_services;
		// Trace connection used for execution time of tasks.
		Genode::Trace::Connection trace;

		// Log of task events, duh.
		std::list<Task::Event> event_log {};

		// Timer used for time stamps in event log.
		Timer::Connection timer {_env};

		// List instead of vector because reallocation would invalidate dataspaces.
		std::list<Task> tasks {};
		// Event logging may be called from multiple threads.
		Genode::Lock log_lock {};
	};

	//Task(Server::Entrypoint& ep, Genode::Cap_connection& cap, Shared_data& shared, const Genode::Xml_node& node, Sched_controller::Connection* ctrl);
	Task(Genode::Env &env, Shared_data& shared, const Genode::Xml_node& node, Sched_controller::Connection* ctrl);
	// Warning: The Task dtor may be empty but tasks should be stopped before destroying them, preferably with a short wait inbetween to allow the child destructor thread to kill them properly.
	virtual ~Task();
	Task(const Task&);
	Task& operator = (const Task&);

	void run();
	void stop();
	std::string name() const;
	bool running() const;
	const Description& desc() const;
	Rq_task::Rq_task getRqTask();

	//static Task* task_by_name(std::list<Task>& tasks, const std::string& name);
	Task* task_by_name(std::list<Task>& tasks, const std::string& name);
	void log_profile_data(Event::Type type, int id, Shared_data& shared);

	void setSchedulable(bool schedulable);
	bool isSchedulable();

	unsigned int get_id();
	Shared_data& get_shared();
	bool jobs_done();

protected:
	class Child_destructor_thread : Genode::Thread_deprecated<2*4096>
	{
	public:
		//Genode::Env &_env;
		Child_destructor_thread();
		void submit_for_destruction(Task* task);

	private:
		Genode::Lock _lock;
		std::list<Task*> _queued;
		static Timer::Connection _timer;

		void entry() override;
	};

	class Child_start_thread : Genode::Thread_deprecated<2*4096>
	{
	public:
		//Genode::Env &_env;
		Child_start_thread();
		void submit_for_start(Task* task);

	private:
		Genode::Lock _lock;
		std::list<Task*> _queued;
		static Timer::Connection _timer;

		void entry() override;
	};

	Shared_data& _shared;

	Description _desc;

	Genode::Attached_ram_dataspace _config;
	const std::string _name;
	unsigned int _iteration;

	bool _paused;

	// Periodic timers.
	Timer::Connection _start_timer {_env};
	Timer::Connection _kill_timer {_env};

	// Timer dispatchers registering callbacks.
	/*
	Genode::Signal_rpc_member<Task> _start_dispatcher;
	Genode::Signal_rpc_member<Task> _kill_dispatcher;
	Genode::Signal_rpc_member<Task> _idle_dispatcher;
	*/
	Genode::Signal_handler<Task> _start_dispatcher;
	Genode::Signal_handler<Task> _kill_dispatcher;
	Genode::Signal_handler<Task> _idle_dispatcher;
	// Child process entry point.
	Genode::Rpc_entrypoint _child_ep;
	//Genode::Entrypoint _child_ep;
	
	// Child meta data.
	Meta_ex* _meta;

	// Combine ID and binary name into a unique name, e.g. 01.namaste
	std::string _make_name() const;

	// Start task once.
	//void _start(unsigned);
	void _start();
	void _kill_crit();
	void _kill(int exit_value = 1);
	void _idle();
	void _stop_timers();
	void _stop_kill_timer();
	void _stop_start_timer();

	// Get XML node value (not attribute) if it exists.
	template <typename T>
	//static T _get_node_value(const Genode::Xml_node& config_node, const char* type, T default_val = T())
	T _get_node_value(const Genode::Xml_node& config_node, const char* type, T default_val = T())
	{
		if (config_node.has_sub_node(type))
		{
			T out{};
			config_node.sub_node(type).value<T>(&out);
			return out;
		}
		return default_val;
	}

	// Get XML node string value (not attribute) if it exists.
	//static std::string _get_node_value(const Genode::Xml_node& config_node, const char* type, size_t max_len, const std::string& default_val = "");
	std::string _get_node_value(const Genode::Xml_node& config_node, const char* type, size_t max_len, const std::string& default_val = "");
private:
	bool _schedulable;
	
public:
	static Child_destructor_thread _child_destructor;
	static Child_start_thread _child_start;
	static Mon_manager::Connection _mon;
	Sched_controller::Connection* _controller;
};
