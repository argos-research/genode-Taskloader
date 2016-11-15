#include <base/env.h>
#include <base/printf.h>
#include <base/rpc_server.h>
#include <base/sleep.h>
#include <os/server.h>

#include "taskloader_session_component.h"

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

/************
 ** Server **
 ************/

namespace Server
{
	char const *name()             { return "taskloader";      }
	size_t stack_size()            { return 64*1024*sizeof(long); }
	void construct(Entrypoint& ep) { static Main server(ep);     }
}
