#include <base/env.h>
#include <base/printf.h>
#include <base/rpc_server.h>
#include <base/sleep.h>
//#include <os/server.h>
#include <base/component.h>
#include <libc/component.h>
#include <base/heap.h>
#include "taskloader_session_component.h"
/*
struct Main
{
	Taskloader_root_component taskloader_root;

	Main(Server::Entrypoint& ep) :
		taskloader_root(&ep, Genode::env()->heap())
	{
		PDBG("task-manager: Hello!\n");
		Genode::env()->parent()->announce(ep.rpc_ep().manage(&taskloader_root));
	}
};
*/
struct Main
{
	Taskloader_root_component taskloader_root;
	Libc::Env &_env;
	Genode::Heap _heap { _env.ram(), _env.rm() };
	
	Main(Libc::Env &env) :
		taskloader_root(_env,&_env.ep(), &_heap), _env(env)
	{
		Genode::log("task-manager: Hello!\n");
		_env.parent().announce(_env.ep().rpc_ep().manage(&taskloader_root));
	}
};

/************
 ** Server **
 ************/
/*
namespace Server
{
	char const *name()             { return "taskloader";      }
	size_t stack_size()            { return 64*1024*sizeof(long); }
	void construct(Entrypoint& ep) { static Main server(ep);     }
}
*/
void Libc::Component::construct(Libc::Env &env)
{
	Libc::with_libc([&] () { static Main main(env); });
}

