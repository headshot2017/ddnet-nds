/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "system.h"
#include "engine.h"

/*#include <engine/console.h>
#include <engine/storage.h>
#include <engine/shared/config.h>*/
#include "network.h"


class CEngine : public IEngine
{
public:
	bool m_Logging;

	CEngine(const char *pAppname)
	{
		dbg_logger_stdout();
		dbg_logger_debugger();

		//
		dbg_msg("engine", "running on %s-%s-%s", CONF_FAMILY_STRING, CONF_PLATFORM_STRING, CONF_ARCH_STRING);
	#ifdef CONF_ARCH_ENDIAN_LITTLE
		dbg_msg("engine", "arch is little endian");
	#elif defined(CONF_ARCH_ENDIAN_BIG)
		dbg_msg("engine", "arch is big endian");
	#else
		dbg_msg("engine", "unknown endian");
	#endif

		// init the network
		net_init();
		CNetBase::Init();

		m_Logging = false;
	}

	void Init()
	{
	}

	void InitLogfile()
	{
	}
};

IEngine *CreateEngine(const char *pAppname) { return new CEngine(pAppname); }
