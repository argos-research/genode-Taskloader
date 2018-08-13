#include <base/env.h>
#include <base/printf.h>
#include <base/rpc_server.h>
#include <base/sleep.h>
#include <root/component.h>
#include <base/component.h>
#include <libc/component.h>
#include <base/heap.h>
#include <taskloader/taskloader.h>
#include <taskloader/taskloader_session.h>
#include <rom_session/rom_session.h>
#include <log_session/log_session.h>

namespace Taskloader {
	struct Main;
	struct Session_component;
	struct Root_component;
}

struct Taskloader::Session_component : Genode::Rpc_object<Session>
{
	private:
		Taskloader* _taskloader=nullptr;
	public:
		enum { CAP_QUOTA = 2 };
		
		void add_tasks(Genode::Ram_dataspace_capability xml_ds_cap)
		{
			_taskloader->add_tasks(xml_ds_cap);
		}

		void clear_tasks()
		{
			_taskloader->clear_tasks();
		}

		Genode::Ram_dataspace_capability binary_ds(Genode::Ram_dataspace_capability name_ds_cap, size_t size)
		{
			return _taskloader->binary_ds(name_ds_cap, size);
		}

		void start()
		{
			_taskloader->start();
		}

		void stop()
		{
			_taskloader->stop();
		}

		Genode::Ram_dataspace_capability profile_data()
		{
			return _taskloader->profile_data();
		}


		Session_component(Taskloader *taskloader)
		: Genode::Rpc_object<Session>()
		{
			_taskloader = taskloader;
		}
	Session_component(const Session_component&);
	Session_component& operator = (const Session_component&);	
};

class Taskloader::Root_component : public Genode::Root_component<Session_component>
{
	private:
		Taskloader* _taskloader { };
	protected:

		Session_component *_create_session(const char*)
		{
			return new (md_alloc()) Session_component(_taskloader);
		}

	public:

		Root_component(Genode::Entrypoint &ep,
		               Genode::Allocator &alloc,
		               Taskloader *taskloader)
		:
			Genode::Root_component<Session_component>(ep, alloc)
		{
			_taskloader=taskloader;
		}
	Root_component(const Root_component&);
	Root_component& operator = (const Root_component&);	
};

struct Taskloader::Main
{
	enum { ROOT_STACK_SIZE = 16*1024 };
	Genode::Env	&_env;
	Genode::Heap	heap	{ _env.ram(), _env.rm() };
	Genode::Static_parent_services<Genode::Ram_session, Genode::Pd_session, Genode::Cpu_session,
	Genode::Rom_session, Genode::Log_session, Timer::Session> _parent_services { };
	Taskloader taskloader { _env , _parent_services };
	Root_component Taskloader_root { _env.ep(), heap , &taskloader};

	Main(Libc::Env &env_) : _env(env_)
	{
		_env.parent().announce(_env.ep().manage(Taskloader_root));
	}
};


Genode::size_t Component::stack_size() { return 32*1024; }

void Libc::Component::construct(Libc::Env &env)
{
	Libc::with_libc([&] () { static Taskloader::Main main(env); });
}
