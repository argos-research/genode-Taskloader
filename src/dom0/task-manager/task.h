#pragma once

#include <list>
#include <unordered_map>

#include <base/service.h>
#include <cap_session/connection.h>
#include <init/child.h>
#include <os/attached_ram_dataspace.h>
#include <os/server.h>
#include <os/signal_rpc_dispatcher.h>
#include <timer_session/connection.h>
#include <trace_session/connection.h>
#include <util/noncopyable.h>
#include <util/xml_node.h>

// Noncopyable because dataspaces might get invalidated.
class Task : Genode::Noncopyable
{
public:
	// Policy for handling binary and service requests.
	struct Child_policy : Genode::Child_policy
	{
	public:
		Child_policy(Task& task);

		// All methods below will be called from the child thread most of the time, and not the task-manager thread. Watch out for race conditions.
		virtual void exit(int exit_value) override;
		virtual const char *name() const override;
		Genode::Service *resolve_session_request(const char *service_name, const char *args) override;
		void filter_session_args(const char *service, char *args, Genode::size_t args_len) override;
		bool announce_service(
			const char *service_name,
			Genode::Root_capability root,
			Genode::Allocator *alloc,
			Genode::Server*) override;
		void unregister_services() override;

		virtual bool active() const;

	protected:
		Task* _task;
		Init::Child_policy_enforce_labeling _labeling_policy;
		Init::Child_policy_provide_rom_file _config_policy;
		Init::Child_policy_provide_rom_file _binary_policy;
		Genode::Lock _exit_lock;
		bool _active;
	};

	// Part of Meta_ex that needs constructor initialization (transferring ram quota).
	struct Meta
	{
	public:
		Meta(const Task& task);

		Genode::Ram_connection ram;
		Genode::Cpu_connection cpu;
		Genode::Rm_connection rm;
		Genode::Pd_connection pd;
		Genode::Server server;
	};

	// Meta data that needs to be dynamically allocated on each start request.
	struct Meta_ex : Meta
	{
	public:
		Meta_ex(Task& task);

		Child_policy policy;
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
				int id;
				size_t quota;
				size_t used;
				int iteration;
			};

			// Trace subject id. Pretty much useless.
			unsigned id;

			std::string session;
			std::string thread;
			Genode::Trace::CPU_info::State state;
			unsigned long long execution_time;

			// Managed by the task manager.
			bool managed;
			Managed_info managed_info;
		};
		enum Type { START = 0, EXIT, EXIT_CRITICAL, EXIT_ERROR, EXIT_EXTERNAL, EXTERNAL };

		static const char* type_name(Type type);

		// Event trigger type.
		Type type;

		// Task that triggered this event. -1 for EXTERNAL. 0 for task-manager.
		int task_id;

		// Time of trigger.
		unsigned long time_stamp;

		std::list<Task_info> task_infos;
	};

	struct Description
	{
		unsigned int id;
		unsigned int execution_time;
		unsigned int critical_time;
		unsigned int priority;
		unsigned int period;
		unsigned int offset;
		Genode::Number_of_bytes quota;
		std::string binary_name;
	};

	// Shared objects. There is only one instance per task manager. Rest are all references.
	struct Shared_data
	{
		Shared_data(size_t trace_quota, size_t trace_buf_size);

		// All binaries loaded by the task manager.
		std::unordered_map<std::string, Genode::Attached_ram_dataspace> binaries;

		// Heap on which to create the init child.
		Genode::Sliced_heap heap;

		// Core services provided by the parent.
		Genode::Service_registry parent_services;

		// Services provided by the started children, if any.
		Genode::Service_registry child_services;

		// Trace connection used for execution time of tasks.
		Genode::Trace::Connection trace;

		// Log of task events, duh.
		std::list<Task::Event> event_log;

		// Timer used for time stamps in event log.
		Timer::Connection timer;

		// List instead of vector because reallocation would invalidate dataspaces.
		std::list<Task> tasks;

		// Event logging may be called from multiple threads.
		Genode::Lock log_lock;
	};

	Task(Server::Entrypoint& ep, Genode::Cap_connection& cap, Shared_data& shared, const Genode::Xml_node& node);

	// Warning: The Task dtor may be empty but tasks should be stopped before destroying them, preferably with a short wait inbetween to allow the child destructor thread to kill them properly.
	virtual ~Task();

	void run();
	void stop();
	std::string name() const;
	bool running() const;
	const Description& desc() const;

	static Task* task_by_name(std::list<Task>& tasks, const std::string& name);
	static void log_profile_data(Event::Type type, int id, Shared_data& shared);

protected:
	class Child_destructor_thread : Genode::Thread<2*4096>
	{
	public:
		Child_destructor_thread();
		void submit_for_destruction(Task* task);

	private:
		Genode::Lock _lock;
		std::list<Task*> _queued;
		Timer::Connection _timer;

		void entry() override;
	};

	static Child_destructor_thread _child_destructor;

	Shared_data& _shared;
	Description _desc;

	Genode::Attached_ram_dataspace _config;
	const std::string _name;
	int _iteration;

	bool _paused;

	// Periodic timers.
	Timer::Connection _start_timer;
	Timer::Connection _kill_timer;

	// Timer dispatchers registering callbacks.
	Genode::Signal_rpc_member<Task> _start_dispatcher;
	Genode::Signal_rpc_member<Task> _kill_dispatcher;
	Genode::Signal_rpc_member<Task> _idle_dispatcher;

	// Child process entry point.
	Genode::Rpc_entrypoint _child_ep;

	// Child meta data.
	Meta_ex* _meta;

	// Combine ID and binary name into a unique name, e.g. 01.namaste
	std::string _make_name() const;

	// Start task once.
	void _start(unsigned);
	void _kill_crit(unsigned);
	void _kill(int exit_value = 1);
	void _idle(unsigned);
	void _stop_timers();
	void _stop_kill_timer();
	void _stop_start_timer();

	// Check if the provided ELF is dynamic by reading the header.
	static bool _check_dynamic_elf(Genode::Attached_ram_dataspace& ds);

	// Get XML node value (not attribute) if it exists.
	template <typename T>
	static T _get_node_value(const Genode::Xml_node& config_node, const char* type, T default_val = T())
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
	static std::string _get_node_value(const Genode::Xml_node& config_node, const char* type, size_t max_len, const std::string& default_val = "");
};
