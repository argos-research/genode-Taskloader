#include <base/env.h>
#include <base/printf.h>
#include <base/rpc_server.h>
#include <base/sleep.h>
#include <os/server.h>

#include "task_manager_session_component.h"

struct Main
{
	Task_manager_root_component task_manager_root;

	Main(Server::Entrypoint& ep) :
		task_manager_root(&ep, Genode::env()->heap())
	{
		PDBG("task-manager: Hello!\n");
		Genode::env()->parent()->announce(ep.rpc_ep().manage(&task_manager_root));
	}
};

/************
 ** Server **
 ************/

namespace Server
{
	char const *name()             { return "task-manager";      }
	size_t stack_size()            { return 64*1024*sizeof(long); }
	void construct(Entrypoint& ep) { static Main server(ep);     }
}
