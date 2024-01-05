#include <new>
#include <stdio.h>

#ifdef ARM9
#include <nds.h>
#include <dswifi9.h>
#include <netinet/in.h>
#endif

#include "client.h"
#include "main.h"

char m_DesiredName[MAX_NAME_LENGTH];
char m_DesiredClan[MAX_NAME_LENGTH];

static CClient *CreateClient()
{
	CClient *pClient = static_cast<CClient *>(mem_alloc(sizeof(CClient), 1));
	mem_zero(pClient, sizeof(CClient));
	return new(pClient) CClient;
}

int main(int argc, const char **argv)
{
#ifdef ARM9
	consoleDemoInit();
	consoleDebugInit(DebugDevice_NOCASH);
#endif

	CClient *pClient = CreateClient();
	IKernel *pKernel = IKernel::Create();
	pKernel->RegisterInterface(pClient);
	pClient->RegisterInterfaces();

	IEngine *pEngine = CreateEngine("Teeworlds");
	pKernel->RegisterInterface(pEngine);

	pEngine->Init();
	pClient->InitInterfaces();

#ifdef ARM9
	dbg_msg("wifi", "connecting DS to WiFi network");
	if(!Wifi_InitDefault(WFC_CONNECT)) {
		dbg_msg("wifi", "connection failed. please check your WFC settings by using a retail DS game, or the DSi wifi settings");
		while (1) swiWaitForVBlank();
	} else {

		dbg_msg("wifi", "connection successful");
		/*struct in_addr ip, gateway, mask, dns1, dns2;

		iprintf("ip     : %s\n", inet_ntoa(ip) );
		ip = Wifi_GetIPInfo(&gateway, &mask, &dns1, &dns2);
		
		iprintf("gateway: %s\n", inet_ntoa(gateway) );
		iprintf("mask   : %s\n", inet_ntoa(mask) );
		iprintf("dns1   : %s\n", inet_ntoa(dns1) );
		iprintf("dns2   : %s\n", inet_ntoa(dns2) );*/
	}
#else
	if(secure_random_init() != 0)
	{
		dbg_msg("secure", "could not initialize secure RNG");
		return 0;
	}
#endif

#ifdef ARM9
	str_copy(pClient->m_aCmdConnect, "192.223.30.85:8306", sizeof(pClient->m_aCmdConnect));
	str_copy(m_DesiredName, "libnds", MAX_NAME_LENGTH);
	str_copy(m_DesiredClan, "devkitARM", MAX_CLAN_LENGTH);
	pClient->Run();
	while (1) swiWaitForVBlank();
#else
	str_copy(pClient->m_aCmdConnect, argv[1], sizeof(pClient->m_aCmdConnect));
	str_copy(m_DesiredName, (argc > 2) ? argv[2] : "chatonly", MAX_NAME_LENGTH);
	str_copy(m_DesiredClan, (argc > 3) ? argv[3] : "test", MAX_CLAN_LENGTH);
	pClient->Run();
#endif

	return 0;
}
