/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>

#include <nds.h>
#include <dswifi9.h>
#include <netinet/in.h>

#include <time.h>
#include <stdlib.h> // qsort
#include <stdarg.h>
#include <string.h>
#include <climits>

#include <base/math.h>
#include <base/vmath.h>
#include <base/system.h>

//#include <game/client/components/menus.h>
//#include <game/client/gameclient.h>

#include <engine/client.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/map.h>
#include <engine/serverbrowser.h>
#include <engine/storage.h>

#include <engine/shared/config.h>
#include <engine/shared/compression.h>
#include <engine/shared/datafile.h>
#include <engine/shared/demo.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/mapchecker.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/ringbuffer.h>
#include <engine/shared/snapshot.h>
#include <engine/shared/fifoconsole.h>

#include <game/version.h>
#include <game/generated/protocol.h>

#include <mastersrv/mastersrv.h>
#include <versionsrv/versionsrv.h>

#include "client.h"

static char m_DesiredName[MAX_NAME_LENGTH];
static char m_DesiredClan[MAX_NAME_LENGTH];

void CSmoothTime::Init(int64 Target)
{
	m_Snap = time_get();
	m_Current = Target;
	m_Target = Target;
	m_aAdjustSpeed[0] = 0.3f;
	m_aAdjustSpeed[1] = 0.3f;
}

void CSmoothTime::SetAdjustSpeed(int Direction, float Value)
{
	m_aAdjustSpeed[Direction] = Value;
}

int64 CSmoothTime::Get(int64 Now)
{
	int64 c = m_Current + (Now - m_Snap);
	int64 t = m_Target + (Now - m_Snap);

	// it's faster to adjust upward instead of downward
	// we might need to adjust these abit

	float AdjustSpeed = m_aAdjustSpeed[0];
	if(t > c)
		AdjustSpeed = m_aAdjustSpeed[1];

	float a = ((Now-m_Snap)/(float)time_freq()) * AdjustSpeed;
	if(a > 1.0f)
		a = 1.0f;

	int64 r = c + (int64)((t-c)*a);

	return r;
}

void CSmoothTime::UpdateInt(int64 Target)
{
	int64 Now = time_get();
	m_Current = Get(Now);
	m_Snap = Now;
	m_Target = Target;
}

void CSmoothTime::Update(int64 Target, int TimeLeft, int AdjustDirection)
{
	int UpdateTimer = 1;

	if(TimeLeft < 0)
	{
		int IsSpike = 0;
		if(TimeLeft < -50)
		{
			IsSpike = 1;

			m_SpikeCounter += 5;
			if(m_SpikeCounter > 50)
				m_SpikeCounter = 50;
		}

		if(IsSpike && m_SpikeCounter < 15)
		{
			// ignore this ping spike
			UpdateTimer = 0;
		}
		else
		{
			if(m_aAdjustSpeed[AdjustDirection] < 30.0f)
				m_aAdjustSpeed[AdjustDirection] *= 2.0f;
		}
	}
	else
	{
		if(m_SpikeCounter)
			m_SpikeCounter--;

		m_aAdjustSpeed[AdjustDirection] *= 0.95f;
		if(m_aAdjustSpeed[AdjustDirection] < 2.0f)
			m_aAdjustSpeed[AdjustDirection] = 2.0f;
	}

	if(UpdateTimer)
		UpdateInt(Target);
}


CClient::CClient() : IClient()
{
	m_pInput = 0;
	
	m_GameTickSpeed = SERVER_TICK_SPEED;

	m_SnapCrcErrors = 0;

	m_AckGameTick[0] = -1;
	m_AckGameTick[1] = -1;
	m_CurrentRecvTick[0] = 0;
	m_CurrentRecvTick[1] = 0;

	// version-checking
	m_aVersionStr[0] = '0';
	m_aVersionStr[1] = 0;

	// pinging
	m_PingStartTime = 0;

	//
	m_aCurrentMap[0] = 0;
	m_CurrentMapCrc = 0;

	//
	m_aCmdConnect[0] = 0;

	m_CurrentServerInfoRequestTime = -1;

	m_CurrentInput[0] = 0;
	m_CurrentInput[1] = 0;
	m_LastDummy = 0;
	m_LastDummy2 = 0;

	mem_zero(&m_aInputs, sizeof(m_aInputs));

	/*
	// map download
	m_aMapdownloadFilename[0] = 0;
	m_aMapdownloadName[0] = 0;
	m_pMapdownloadTask = 0;
	m_MapdownloadFile = 0;
	m_MapdownloadChunk = 0;
	m_MapdownloadCrc = 0;
	m_MapdownloadAmount = -1;
	m_MapdownloadTotalsize = -1;

	m_LocalIDs[0] = 0;
	m_LocalIDs[1] = 0;
	m_Fire = 0;

	mem_zero(&m_DummyInput, sizeof(m_DummyInput));
	mem_zero(&HammerInput, sizeof(HammerInput));
	HammerInput.m_Fire = 0;
	*/

	m_State = IClient::STATE_OFFLINE;
	m_aServerAddressStr[0] = 0;

	mem_zero(m_aSnapshots, sizeof(m_aSnapshots));
	m_SnapshotStorage[0].Init();
	m_SnapshotStorage[1].Init();
	m_ReceivedSnapshots[0] = 0;
	m_ReceivedSnapshots[1] = 0;

	m_VersionInfo.m_State = CVersionInfo::STATE_INIT;

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));
}

// ----- send functions -----
int CClient::SendMsg(CMsgPacker *pMsg, int Flags)
{
	return SendMsgEx(pMsg, Flags, false);
}

int CClient::SendMsgEx(CMsgPacker *pMsg, int Flags, bool System)
{
	CNetChunk Packet;

	if(State() == IClient::STATE_OFFLINE)
		return 0;

	mem_zero(&Packet, sizeof(CNetChunk));

	Packet.m_ClientID = 0;
	Packet.m_pData = pMsg->Data();
	Packet.m_DataSize = pMsg->Size();

	// HACK: modify the message id in the packet and store the system flag
	if(*((unsigned char*)Packet.m_pData) == 1 && System && Packet.m_DataSize == 1)
		dbg_break();

	*((unsigned char*)Packet.m_pData) <<= 1;
	if(System)
		*((unsigned char*)Packet.m_pData) |= 1;

	if(Flags&MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags&MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if(!(Flags&MSGFLAG_NOSEND))
	{
		m_NetClient[0].Send(&Packet);
	}

	return 0;
}

int CClient::SendMsgExY(CMsgPacker *pMsg, int Flags, bool System, int NetClient)
{
	CNetChunk Packet;

	mem_zero(&Packet, sizeof(CNetChunk));

	Packet.m_ClientID = 0;
	Packet.m_pData = pMsg->Data();
	Packet.m_DataSize = pMsg->Size();

	// HACK: modify the message id in the packet and store the system flag
	if(*((unsigned char*)Packet.m_pData) == 1 && System && Packet.m_DataSize == 1)
		dbg_break();

	*((unsigned char*)Packet.m_pData) <<= 1;
	if(System)
		*((unsigned char*)Packet.m_pData) |= 1;

	if(Flags&MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags&MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	m_NetClient[NetClient].Send(&Packet);
	return 0;
}

void CClient::SendInfo()
{
	CMsgPacker Msg(NETMSG_INFO);
	Msg.AddString("0.6 626fce9a778df4d4", 128);
	Msg.AddString("", 128); // password
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH);
}


void CClient::SendEnterGame()
{
	CMsgPacker Msg(NETMSG_ENTERGAME);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH);
}

void CClient::SendReady()
{
	CMsgPacker Msg(NETMSG_READY);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH);
}

void CClient::SendInput()
{
	int64 Now = time_get();

	if(m_PredTick[0] <= 0)
		return;

	// fetch input
	int Size = sizeof(CNetObj_PlayerInput);

	if(Size)
	{
		// pack input
		CMsgPacker Msg(NETMSG_INPUT);
		Msg.AddInt(m_AckGameTick[g_Config.m_ClDummy]);
		Msg.AddInt(m_PredTick[g_Config.m_ClDummy]);
		Msg.AddInt(Size);

		m_aInputs[g_Config.m_ClDummy][m_CurrentInput[g_Config.m_ClDummy]].m_Tick = m_PredTick[g_Config.m_ClDummy];
		m_aInputs[g_Config.m_ClDummy][m_CurrentInput[g_Config.m_ClDummy]].m_PredictedTime = m_PredictedTime.Get(Now);
		m_aInputs[g_Config.m_ClDummy][m_CurrentInput[g_Config.m_ClDummy]].m_Time = Now;

		// pack it
		for(int i = 0; i < Size/4; i++)
			Msg.AddInt(m_aInputs[g_Config.m_ClDummy][m_CurrentInput[g_Config.m_ClDummy]].m_aData[i]);

		m_CurrentInput[g_Config.m_ClDummy]++;
		m_CurrentInput[g_Config.m_ClDummy]%=200;

		SendMsgEx(&Msg, MSGFLAG_FLUSH);
	}
}

void CClient::DisconnectWithReason(const char *pReason)
{
	dbg_msg("client", "disconnecting. reason='%s'", pReason?pReason:"unknown");

	// stop demo playback and recorder
	/*
	m_DemoPlayer.Stop();
	for(int i = 0; i < RECORDER_MAX; i++)
		DemoRecorder_Stop(i);
	*/

	//
	//m_RconAuthed[0] = 0;
	//m_UseTempRconCommands = 0;
	//m_pConsole->DeregisterTempAll();
	m_NetClient[0].Disconnect(pReason);
	SetState(IClient::STATE_OFFLINE);

	// clear the current server info
	mem_zero(&m_CurrentServerInfo, sizeof(m_CurrentServerInfo));
	mem_zero(&m_ServerAddress, sizeof(m_ServerAddress));

	// clear snapshots
	m_aSnapshots[0][SNAP_CURRENT] = 0;
	m_aSnapshots[0][SNAP_PREV] = 0;
	m_ReceivedSnapshots[0] = 0;

	//m_pMap->Unload();

	/*
	// disable all downloads
	m_MapdownloadChunk = 0;
	if(m_pMapdownloadTask)
		m_pMapdownloadTask->Abort();
	if(m_MapdownloadFile)
		io_close(m_MapdownloadFile);
	m_MapdownloadFile = 0;
	m_MapdownloadCrc = 0;
	m_MapdownloadTotalsize = -1;
	m_MapdownloadAmount = 0;
	*/
}

void CClient::Disconnect()
{
	DisconnectWithReason(0);
}

void CClient::Connect(const char *pAddress)
{
	int Port = 8303;
	m_DDRaceMsgSent = false;

	Disconnect();

	str_copy(m_aServerAddressStr, pAddress, sizeof(m_aServerAddressStr));

	dbg_msg("client", "connecting to '%s'", m_aServerAddressStr);

	ServerInfoRequest();
	if(net_host_lookup(m_aServerAddressStr, &m_ServerAddress, m_NetClient[0].NetType()) != 0)
	{
		dbg_msg("client", "could not find the address of %s, connecting to localhost", m_aServerAddressStr);
		net_host_lookup("localhost", &m_ServerAddress, m_NetClient[0].NetType());
	}

	//m_RconAuthed[0] = 0;
	if(m_ServerAddress.port == 0)
		m_ServerAddress.port = Port;
	m_NetClient[0].Connect(&m_ServerAddress);
	SetState(IClient::STATE_CONNECTING);

	/*
	for(int i = 0; i < RECORDER_MAX; i++)
		if(m_DemoRecorder[i].IsRecording())
			DemoRecorder_Stop(i);

	m_InputtimeMarginGraph.Init(-150.0f, 150.0f);
	m_GametimeMarginGraph.Init(-150.0f, 150.0f);
	*/
}

void CClient::SetState(int s)
{
	if(m_State == IClient::STATE_QUITING)
		return;

	//int Old = m_State;
	m_State = s;
	//if(Old != s)
		//GameClient()->OnStateChange(m_State, Old);
}

void CClient::EnterGame()
{
	if(State() == IClient::STATE_DEMOPLAYBACK)
		return;

	// now we will wait for two snapshots
	// to finish the connection
	SendEnterGame();
	OnEnterGame();

	ServerInfoRequest(); // fresh one for timeout protection

	//m_TimeoutCodeSent[0] = false;
	//m_TimeoutCodeSent[1] = false;
}

const char *CClient::ErrorString()
{
	return m_NetClient[0].ErrorString();
}

const char *CClient::LatestVersion()
{
	return m_aVersionStr;
}

bool CClient::ConnectionProblems()
{
	return m_NetClient[0].GotProblems() != 0;
}

void CClient::GetServerInfo(CServerInfo *pServerInfo)
{
	mem_copy(pServerInfo, &m_CurrentServerInfo, sizeof(m_CurrentServerInfo));
}

void CClient::ServerInfoRequest()
{
	mem_zero(&m_CurrentServerInfo, sizeof(m_CurrentServerInfo));
	m_CurrentServerInfoRequestTime = 0;
}

void CClient::OnEnterGame()
{
	// reset input
	int i;
	for(i = 0; i < 200; i++)
	{
		m_aInputs[0][i].m_Tick = -1;
		m_aInputs[1][i].m_Tick = -1;
	}
	m_CurrentInput[0] = 0;
	m_CurrentInput[1] = 0;

	// reset snapshots
	m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] = 0;
	m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV] = 0;
	m_SnapshotStorage[g_Config.m_ClDummy].PurgeAll();
	m_ReceivedSnapshots[g_Config.m_ClDummy] = 0;
	m_SnapshotParts = 0;
	m_PredTick[g_Config.m_ClDummy] = 0;
	m_CurrentRecvTick[g_Config.m_ClDummy] = 0;
	m_CurGameTick[g_Config.m_ClDummy] = 0;
	m_PrevGameTick[g_Config.m_ClDummy] = 0;

	CMsgPacker Msg(NETMSGTYPE_CL_ISDDNET);
	Msg.AddInt(2004);
	SendMsgExY(&Msg, MSGFLAG_VITAL,false, 0);
	m_DDRaceMsgSent = true;
}

void CClient::InfoRequestImpl(const NETADDR &Addr)
{
	unsigned char Buffer[sizeof(SERVERBROWSE_GETINFO)+1];
	CNetChunk Packet;

	mem_copy(Buffer, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO));
	Buffer[sizeof(SERVERBROWSE_GETINFO)] = 1;

	Packet.m_ClientID = -1;
	Packet.m_Address = Addr;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;
	Packet.m_DataSize = sizeof(Buffer);
	Packet.m_pData = Buffer;

	m_NetClient[2].Send(&Packet);
}

void CClient::InfoRequestImpl64(const NETADDR &Addr)
{
	unsigned char Buffer[sizeof(SERVERBROWSE_GETINFO64)+1];
	CNetChunk Packet;

	mem_copy(Buffer, SERVERBROWSE_GETINFO64, sizeof(SERVERBROWSE_GETINFO64));
	Buffer[sizeof(SERVERBROWSE_GETINFO64)] = 1;

	Packet.m_ClientID = -1;
	Packet.m_Address = Addr;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;
	Packet.m_DataSize = sizeof(Buffer);
	Packet.m_pData = Buffer;

	m_NetClient[2].Send(&Packet);
}

void CClient::RegisterInterfaces()
{
	
}

void CClient::InitInterfaces()
{
	// fetch interfaces
	m_pEngine = Kernel()->RequestInterface<IEngine>();
	m_pInput = Kernel()->RequestInterface<IEngineInput>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
}

// ---

void *CClient::SnapGetItem(int SnapID, int Index, CSnapItem *pItem)
{
	CSnapshotItem *i;
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	i = m_aSnapshots[0][SnapID]->m_pAltSnap->GetItem(Index);
	pItem->m_DataSize = m_aSnapshots[0][SnapID]->m_pAltSnap->GetItemSize(Index);
	pItem->m_Type = i->Type();
	pItem->m_ID = i->ID();
	return (void *)i->Data();
}

void CClient::SnapInvalidateItem(int SnapID, int Index)
{
	CSnapshotItem *i;
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	i = m_aSnapshots[0][SnapID]->m_pAltSnap->GetItem(Index);
	if(i)
	{
		if((char *)i < (char *)m_aSnapshots[0][SnapID]->m_pAltSnap || (char *)i > (char *)m_aSnapshots[0][SnapID]->m_pAltSnap + m_aSnapshots[0][SnapID]->m_SnapSize)
			dbg_msg("client", "snap invalidate problem");
		if((char *)i >= (char *)m_aSnapshots[0][SnapID]->m_pSnap && (char *)i < (char *)m_aSnapshots[0][SnapID]->m_pSnap + m_aSnapshots[0][SnapID]->m_SnapSize)
			dbg_msg("client", "snap invalidate problem");
		i->m_TypeAndID = -1;
	}
}

void *CClient::SnapFindItem(int SnapID, int Type, int ID)
{
	// TODO: linear search. should be fixed.
	int i;

	if(!m_aSnapshots[0][SnapID])
		return 0x0;

	for(i = 0; i < m_aSnapshots[0][SnapID]->m_pSnap->NumItems(); i++)
	{
		CSnapshotItem *pItem = m_aSnapshots[0][SnapID]->m_pAltSnap->GetItem(i);
		if(pItem->Type() == Type && pItem->ID() == ID)
			return (void *)pItem->Data();
	}
	return 0x0;
}

int CClient::SnapNumItems(int SnapID)
{
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	if(!m_aSnapshots[0][SnapID])
		return 0;
	return m_aSnapshots[0][SnapID]->m_pSnap->NumItems();
}

void CClient::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

void CClient::ProcessConnlessPacket(CNetChunk *pPacket)
{
	// server info
	if(pPacket->m_DataSize >= (int)sizeof(SERVERBROWSE_INFO) && mem_comp(pPacket->m_pData, SERVERBROWSE_INFO, sizeof(SERVERBROWSE_INFO)) == 0)
	{
		// we got ze info
		CUnpacker Up;
		CServerInfo Info = {0};

		Up.Reset((unsigned char*)pPacket->m_pData+sizeof(SERVERBROWSE_INFO), pPacket->m_DataSize-sizeof(SERVERBROWSE_INFO));
		int Token = str_toint(Up.GetString());
		str_copy(Info.m_aVersion, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aVersion));
		str_copy(Info.m_aName, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aName));
		str_copy(Info.m_aMap, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aMap));
		str_copy(Info.m_aGameType, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aGameType));
		Info.m_Flags = str_toint(Up.GetString());
		Info.m_NumPlayers = str_toint(Up.GetString());
		Info.m_MaxPlayers = str_toint(Up.GetString());
		Info.m_NumClients = str_toint(Up.GetString());
		Info.m_MaxClients = str_toint(Up.GetString());

		// don't add invalid info to the server browser list
		if(Info.m_NumClients < 0 || Info.m_NumClients > MAX_CLIENTS || Info.m_MaxClients < 0 || Info.m_MaxClients > MAX_CLIENTS ||
			Info.m_NumPlayers < 0 || Info.m_NumPlayers > Info.m_NumClients || Info.m_MaxPlayers < 0 || Info.m_MaxPlayers > Info.m_MaxClients)
			return;

		net_addr_str(&pPacket->m_Address, Info.m_aAddress, sizeof(Info.m_aAddress), true);

		for(int i = 0; i < Info.m_NumClients; i++)
		{
			str_copy(Info.m_aClients[i].m_aName, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aClients[i].m_aName));
			str_copy(Info.m_aClients[i].m_aClan, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aClients[i].m_aClan));
			Info.m_aClients[i].m_Country = str_toint(Up.GetString());
			Info.m_aClients[i].m_Score = str_toint(Up.GetString());
			Info.m_aClients[i].m_Player = str_toint(Up.GetString()) != 0 ? true : false;
		}

		if(!Up.Error())
		{
			if(net_addr_comp(&m_ServerAddress, &pPacket->m_Address) == 0)
			{
				if(m_CurrentServerInfo.m_MaxClients <= VANILLA_MAX_CLIENTS)
				{
					mem_copy(&m_CurrentServerInfo, &Info, sizeof(m_CurrentServerInfo));
					m_CurrentServerInfo.m_NetAddr = m_ServerAddress;
					m_CurrentServerInfoRequestTime = -1;
				}
			}
		}
	}

	// server info 64
	if(pPacket->m_DataSize >= (int)sizeof(SERVERBROWSE_INFO64) && mem_comp(pPacket->m_pData, SERVERBROWSE_INFO64, sizeof(SERVERBROWSE_INFO64)) == 0)
	{
		// we got ze info
		CUnpacker Up;
		CServerInfo NewInfo = {0};
		CServerInfo &Info = NewInfo;

		Up.Reset((unsigned char*)pPacket->m_pData+sizeof(SERVERBROWSE_INFO64), pPacket->m_DataSize-sizeof(SERVERBROWSE_INFO64));
		int Token = str_toint(Up.GetString());
		str_copy(Info.m_aVersion, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aVersion));
		str_copy(Info.m_aName, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aName));
		str_copy(Info.m_aMap, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aMap));
		str_copy(Info.m_aGameType, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aGameType));
		Info.m_Flags = str_toint(Up.GetString());
		Info.m_NumPlayers = str_toint(Up.GetString());
		Info.m_MaxPlayers = str_toint(Up.GetString());
		Info.m_NumClients = str_toint(Up.GetString());
		Info.m_MaxClients = str_toint(Up.GetString());

		// don't add invalid info to the server browser list
		if(Info.m_NumClients < 0 || Info.m_NumClients > MAX_CLIENTS || Info.m_MaxClients < 0 || Info.m_MaxClients > MAX_CLIENTS ||
			Info.m_NumPlayers < 0 || Info.m_NumPlayers > Info.m_NumClients || Info.m_MaxPlayers < 0 || Info.m_MaxPlayers > Info.m_MaxClients)
			return;

		net_addr_str(&pPacket->m_Address, Info.m_aAddress, sizeof(Info.m_aAddress), true);

		int Offset = Up.GetInt();

		for(int i = max(Offset, 0); i < max(Offset, 0) + 24 && i < MAX_CLIENTS; i++)
		{
			str_copy(Info.m_aClients[i].m_aName, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aClients[i].m_aName));
			str_copy(Info.m_aClients[i].m_aClan, Up.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(Info.m_aClients[i].m_aClan));
			Info.m_aClients[i].m_Country = str_toint(Up.GetString());
			Info.m_aClients[i].m_Score = str_toint(Up.GetString());
			Info.m_aClients[i].m_Player = str_toint(Up.GetString()) != 0 ? true : false;
		}

		if(!Up.Error())
		{
			if(net_addr_comp(&m_ServerAddress, &pPacket->m_Address) == 0)
			{
				mem_copy(&m_CurrentServerInfo, &Info, sizeof(m_CurrentServerInfo));
				m_CurrentServerInfo.m_NetAddr = m_ServerAddress;
				m_CurrentServerInfoRequestTime = -1;
			}
		}
	}
}

void CClient::ProcessServerPacket(CNetChunk *pPacket)
{
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);

	// unpack msgid and system flag
	int Msg = Unpacker.GetInt();
	int Sys = Msg&1;
	Msg >>= 1;

	if(Unpacker.Error())
		return;

	if(Sys)
	{
		// system message
		if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAP_CHANGE)
		{
			const char *pMap = Unpacker.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
			int MapCrc = Unpacker.GetInt();
			int MapSize = Unpacker.GetInt();
			const char *pError = 0;

			if(Unpacker.Error())
				return;

			for(int i = 0; pMap[i]; i++) // protect the player from nasty map names
			{
				if(pMap[i] == '/' || pMap[i] == '\\')
					pError = "strange character in map name";
			}

			if(MapSize < 0)
				pError = "invalid map size";

			if(pError)
				DisconnectWithReason(pError);
			else
			{
				dbg_msg("client/network", "loading done");
				SendReady();
			}
		}
		else if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_CON_READY)
		{
			CNetMsg_Cl_StartInfo Msg;
			Msg.m_pName = m_DesiredName;
			Msg.m_pClan = m_DesiredClan;
			Msg.m_Country = 0;
			Msg.m_pSkin = "oldschool";
			Msg.m_UseCustomColor = 0;
			Msg.m_ColorBody = 0;
			Msg.m_ColorFeet = 0;
			CMsgPacker Packer(Msg.MsgID());
			Msg.Pack(&Packer);
			SendMsgExY(&Packer, MSGFLAG_VITAL, false, 0);
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker Msg(NETMSG_PING_REPLY);
			SendMsgEx(&Msg, 0);
		}
		else if(Msg == NETMSG_SNAP || Msg == NETMSG_SNAPSINGLE || Msg == NETMSG_SNAPEMPTY)
		{
			int NumParts = 1;
			int Part = 0;
			int GameTick = Unpacker.GetInt();
			int DeltaTick = GameTick-Unpacker.GetInt();
			int PartSize = 0;
			int Crc = 0;
			int CompleteSize = 0;
			const char *pData = 0;

			// only allow packets from the server we actually want
			if(net_addr_comp(&pPacket->m_Address, &m_ServerAddress))
				return;

			// we are not allowed to process snapshot yet
			if(State() < IClient::STATE_LOADING)
				return;

			if(Msg == NETMSG_SNAP)
			{
				NumParts = Unpacker.GetInt();
				Part = Unpacker.GetInt();
			}

			if(Msg != NETMSG_SNAPEMPTY)
			{
				Crc = Unpacker.GetInt();
				PartSize = Unpacker.GetInt();
			}

			pData = (const char *)Unpacker.GetRaw(PartSize);

			if(Unpacker.Error())
				return;
			
			if(GameTick >= m_CurrentRecvTick[0])
			{
				if(GameTick != m_CurrentRecvTick[0])
				{
					m_SnapshotParts = 0;
					m_CurrentRecvTick[0] = GameTick;
				}

				// TODO: clean this up abit
				mem_copy((char*)m_aSnapshotIncomingData + Part*MAX_SNAPSHOT_PACKSIZE, pData, PartSize);
				m_SnapshotParts |= 1<<Part;
				
				if(m_SnapshotParts == (unsigned)((1<<NumParts)-1))
				{
					static CSnapshot Emptysnap;
					CSnapshot *pDeltaShot = &Emptysnap;
					int PurgeTick;
					void *pDeltaData;
					int DeltaSize;
					unsigned char aTmpBuffer2[CSnapshot::MAX_SIZE];
					unsigned char aTmpBuffer3[CSnapshot::MAX_SIZE];
					CSnapshot *pTmpBuffer3 = (CSnapshot*)aTmpBuffer3;	// Fix compiler warning for strict-aliasing
					int SnapSize;

					CompleteSize = (NumParts-1) * MAX_SNAPSHOT_PACKSIZE + PartSize;

					// reset snapshoting
					m_SnapshotParts = 0;

					// find snapshot that we should use as delta
					Emptysnap.Clear();
					
					// find delta
					if(DeltaTick >= 0)
					{
						int DeltashotSize = m_SnapshotStorage[0].Get(DeltaTick, 0, &pDeltaShot, 0);

						if(DeltashotSize < 0)
						{
							// couldn't find the delta snapshots that the server used
							// to compress this snapshot. force the server to resync
							/*if(g_Config.m_Debug)
							{
								char aBuf[256];
								str_format(aBuf, sizeof(aBuf), "error, couldn't find the delta snapshot");
								m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", aBuf);
							}*/

							// ack snapshot
							// TODO: combine this with the input message
							m_AckGameTick[0] = -1;
							return;
						}
					}

					// decompress snapshot
					pDeltaData = m_SnapshotDelta.EmptyDelta();
					DeltaSize = sizeof(int)*3;

					if(CompleteSize)
					{
						int IntSize = CVariableInt::Decompress(m_aSnapshotIncomingData, CompleteSize, aTmpBuffer2);

						if(IntSize < 0) // failure during decompression, bail
							return;

						pDeltaData = aTmpBuffer2;
						DeltaSize = IntSize;
					}

					// unpack delta
					SnapSize = m_SnapshotDelta.UnpackDelta(pDeltaShot, pTmpBuffer3, pDeltaData, DeltaSize);
					if(SnapSize < 0)
					{
						dbg_msg("client", "delta unpack failed!");
						return;
					}

					if(Msg != NETMSG_SNAPEMPTY && pTmpBuffer3->Crc() != Crc)
					{
						/*if(g_Config.m_Debug)
						{
							char aBuf[256];
							str_format(aBuf, sizeof(aBuf), "snapshot crc error #%d - tick=%d wantedcrc=%d gotcrc=%d compressed_size=%d delta_tick=%d",
								m_SnapCrcErrors, GameTick, Crc, pTmpBuffer3->Crc(), CompleteSize, DeltaTick);
							m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", aBuf);
						}*/

						m_SnapCrcErrors++;
						if(m_SnapCrcErrors > 10)
						{
							// to many errors, send reset
							m_AckGameTick[0] = -1;
							SendInput();
							m_SnapCrcErrors = 0;
						}
						return;
					}
					else
					{
						if(m_SnapCrcErrors)
							m_SnapCrcErrors--;
					}

					// purge old snapshots
					PurgeTick = DeltaTick;
					if(m_aSnapshots[0][SNAP_PREV] && m_aSnapshots[0][SNAP_PREV]->m_Tick < PurgeTick)
						PurgeTick = m_aSnapshots[0][SNAP_PREV]->m_Tick;
					if(m_aSnapshots[0][SNAP_CURRENT] && m_aSnapshots[0][SNAP_CURRENT]->m_Tick < PurgeTick)
						PurgeTick = m_aSnapshots[0][SNAP_CURRENT]->m_Tick;
					m_SnapshotStorage[0].PurgeUntil(PurgeTick);

					// add new
					m_SnapshotStorage[0].Add(GameTick, time_get(), SnapSize, pTmpBuffer3, 1);
					
					// for antiping: if the projectile netobjects from the server contains extra data, this is removed and the original content restored before recording demo
					unsigned char aExtraInfoRemoved[CSnapshot::MAX_SIZE];
					mem_copy(aExtraInfoRemoved, pTmpBuffer3, SnapSize);

					// apply snapshot, cycle pointers
					m_ReceivedSnapshots[0]++;

					m_CurrentRecvTick[0] = GameTick;

					// we got two snapshots until we see us self as connected
					if(m_ReceivedSnapshots[0] == 2)
					{
						// start at 200ms and work from there
						m_PredictedTime.Init(GameTick*time_freq()/50);
						m_PredictedTime.SetAdjustSpeed(1, 1000.0f);
						m_GameTime[0].Init((GameTick-1)*time_freq()/50);
						m_aSnapshots[0][SNAP_PREV] = m_SnapshotStorage[0].m_pFirst;
						m_aSnapshots[0][SNAP_CURRENT] = m_SnapshotStorage[0].m_pLast;
						m_LocalStartTime = time_get();
						SetState(IClient::STATE_ONLINE);
					}

					// adjust game time
					if(m_ReceivedSnapshots[0] > 2)
					{
						unsigned Now = m_GameTime[0].Get(time_get());
						unsigned TickStart = GameTick*time_freq()/50;
						unsigned TimeLeft = (TickStart-Now)*1000 / time_freq();
						m_GameTime[0].Update((GameTick-1)*time_freq()/50, TimeLeft, 0);
					}

					// ack snapshot
					m_AckGameTick[0] = GameTick;
				}
			}
		}
	}
	else
	{
		if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0)
		{
			// game message
			/*
			for(int i = 0; i < RECORDER_MAX; i++)
				if(m_DemoRecorder[i].IsRecording())
					m_DemoRecorder[i].RecordMessage(pPacket->m_pData, pPacket->m_DataSize);
			*/

			//GameClient()->OnMessage(Msg, &Unpacker);

			// special messages
			if(Msg == NETMSGTYPE_SV_EXTRAPROJECTILE)
			{
				/*
				int Num = pUnpacker->GetInt();

				for(int k = 0; k < Num; k++)
				{
					CNetObj_Projectile Proj;
					for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
						((int *)&Proj)[i] = pUnpacker->GetInt();

					if(pUnpacker->Error())
						return;

					g_GameClient.m_pItems->AddExtraProjectile(&Proj);

					if(AntiPingWeapons() && Proj.m_Type == WEAPON_GRENADE && !UseExtraInfo(&Proj))
					{
						vec2 StartPos;
						vec2 Direction;
						ExtractInfo(&Proj, &StartPos, &Direction, 1);
						if(CWeaponData *pCurrentData = GetWeaponData(Proj.m_StartTick))
						{
							if(CWeaponData *pMatchingData = FindWeaponData(Proj.m_StartTick))
							{
								if(distance(pMatchingData->m_Direction, Direction) < 0.015)
									Direction = pMatchingData->m_Direction;
								else if(int *pData = Client()->GetInput(Proj.m_StartTick+2))
								{
									CNetObj_PlayerInput *pNextInput = (CNetObj_PlayerInput*) pData;
									vec2 NextDirection = normalize(vec2(pNextInput->m_TargetX, pNextInput->m_TargetY));
									if(distance(NextDirection, Direction) < 0.015)
										Direction = NextDirection;
								}
								if(distance(pMatchingData->StartPos(), StartPos) < 1)
									StartPos = pMatchingData->StartPos();
							}
							pCurrentData->m_Tick = Proj.m_StartTick;
							pCurrentData->m_Direction = Direction;
							pCurrentData->m_Pos = StartPos - Direction * 28.0f * 0.75f;
						}
					}
				}
				*/
				return;
			}
			else if(Msg == NETMSGTYPE_SV_TUNEPARAMS)
			{
				/*
				// unpack the new tuning
				CTuningParams NewTuning;
				int *pParams = (int *)&NewTuning;
				// No jetpack on DDNet incompatible servers:
				NewTuning.m_JetpackStrength = 0;
				for(unsigned i = 0; i < sizeof(CTuningParams)/sizeof(int); i++)
				{
					int value = pUnpacker->GetInt();

					// check for unpacking errors
					if(pUnpacker->Error())
						break;

					pParams[i] = value;
				}

				m_ServerMode = SERVERMODE_PURE;

				// apply new tuning
				m_Tuning[IsDummy ? 0 : 0] = NewTuning;
				*/
				return;
			}

			void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(Msg, &Unpacker);
			if(!pRawMsg)
			{
				dbg_msg("client", "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(Msg), Msg, m_NetObjHandler.FailedMsgOn());
				return;
			}

			// TODO: this should be done smarter
			//for(int i = 0; i < m_All.m_Num; i++)
				//m_All.m_paComponents[i]->OnMessage(MsgId, pRawMsg);

			if(Msg == NETMSGTYPE_SV_READYTOENTER)
			{
				EnterGame();
			}
			else if (Msg == NETMSGTYPE_SV_EMOTICON)
			{
				/*
				CNetMsg_Sv_Emoticon *pMsg = (CNetMsg_Sv_Emoticon *)pRawMsg;

				// apply
				m_aClients[pMsg->m_ClientID].m_Emoticon = pMsg->m_Emoticon;
				m_aClients[pMsg->m_ClientID].m_EmoticonStart = Client()->GameTick();
				*/
			}
			else if(Msg == NETMSGTYPE_SV_SOUNDGLOBAL)
			{
				/*
				if(m_SuppressEvents)
					return;

				// don't enqueue pseudo-global sounds from demos (created by PlayAndRecord)
				CNetMsg_Sv_SoundGlobal *pMsg = (CNetMsg_Sv_SoundGlobal *)pRawMsg;
				if(pMsg->m_SoundID == SOUND_CTF_DROP || pMsg->m_SoundID == SOUND_CTF_RETURN ||
					pMsg->m_SoundID == SOUND_CTF_CAPTURE || pMsg->m_SoundID == SOUND_CTF_GRAB_EN ||
					pMsg->m_SoundID == SOUND_CTF_GRAB_PL)
				{
					if(g_Config.m_SndGame)
						g_GameClient.m_pSounds->Enqueue(CSounds::CHN_GLOBAL, pMsg->m_SoundID);
				}
				else
				{
					if(g_Config.m_SndGame)
						g_GameClient.m_pSounds->Play(CSounds::CHN_GLOBAL, pMsg->m_SoundID, 1.0f);
				}
				*/
			}
			else if(Msg == NETMSGTYPE_SV_TEAMSSTATE)
			{
				/*
				unsigned int i;

				for(i = 0; i < MAX_CLIENTS; i++)
				{
					int Team = pUnpacker->GetInt();
					bool WentWrong = false;

					if(pUnpacker->Error())
						WentWrong = true;

					if(!WentWrong && Team >= 0 && Team < MAX_CLIENTS)
						m_Teams.Team(i, Team);
					else if (Team != MAX_CLIENTS)
						WentWrong = true;

					if(WentWrong)
					{
						m_Teams.Team(i, 0);
						break;
					}
				}

				if (i <= 16)
					m_Teams.m_IsDDRace16 = true;
				*/
			}
			else if(Msg == NETMSGTYPE_SV_PLAYERTIME)
			{
				/*
				CNetMsg_Sv_PlayerTime *pMsg = (CNetMsg_Sv_PlayerTime *)pRawMsg;
				m_aClients[pMsg->m_ClientID].m_Score = pMsg->m_Time;
				*/
			}
			else if(Msg == NETMSGTYPE_SV_CHAT)
			{
				CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
				if (pMsg->m_ClientID == -1)
					dbg_msg("chat", "*** %s", pMsg->m_pMessage);
				else
				{
					dbg_msg("chat", "%d: %s", pMsg->m_ClientID, pMsg->m_pMessage);
					if (str_comp_nocase_num(pMsg->m_pMessage, "!redirect ", 10) == 0)
					{
						// leave this server and connect to specified IP
						Connect(pMsg->m_pMessage+10);
					}
				}
			}
			else if(Msg == NETMSGTYPE_SV_BROADCAST)
			{
				CNetMsg_Sv_Broadcast *pMsg = (CNetMsg_Sv_Broadcast *)pRawMsg;
				dbg_msg("broadcast", "%s", pMsg->m_pMessage);
			}
		}
	}
}

void CClient::PumpNetwork()
{
	for(int i=0; i<3; i++)
	{
		m_NetClient[i].Update();
	}

	if(State() != IClient::STATE_DEMOPLAYBACK)
	{
		// check for errors
		if(State() != IClient::STATE_OFFLINE && State() != IClient::STATE_QUITING && m_NetClient[0].State() == NETSTATE_OFFLINE)
		{
			SetState(IClient::STATE_OFFLINE);
			Disconnect();
			dbg_msg("client", "offline error='%s'", m_NetClient[0].ErrorString());
		}

		//
		if(State() == IClient::STATE_CONNECTING && m_NetClient[0].State() == NETSTATE_ONLINE)
		{
			// we switched to online
			dbg_msg("client", "connected, sending info");
			SetState(IClient::STATE_LOADING);
			SendInfo();
		}
	}

	// process packets
	CNetChunk Packet;
	for(int i=0; i < 3; i++)
	{
		while(m_NetClient[i].Recv(&Packet))
		{
			if(Packet.m_ClientID == -1 || i > 1)
			{
				ProcessConnlessPacket(&Packet);
			}
			else if(i != 1)
			{
				ProcessServerPacket(&Packet);
			}
		}
	}
}

void CClient::Update()
{
	if(State() == IClient::STATE_ONLINE && m_ReceivedSnapshots[g_Config.m_ClDummy] >= 3)
	{
		if(m_ReceivedSnapshots[!g_Config.m_ClDummy] >= 3)
		{
			// switch dummy snapshot
			int64 Now = m_GameTime[!g_Config.m_ClDummy].Get(time_get());
			while(1)
			{
				CSnapshotStorage::CHolder *pCur = m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT];
				int64 TickStart = (pCur->m_Tick)*time_freq()/50;

				if(TickStart < Now)
				{
					CSnapshotStorage::CHolder *pNext = m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_pNext;
					if(pNext)
					{
						m_aSnapshots[!g_Config.m_ClDummy][SNAP_PREV] = m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT];
						m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT] = pNext;

						// set ticks
						m_CurGameTick[!g_Config.m_ClDummy] = m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick;
						m_PrevGameTick[!g_Config.m_ClDummy] = m_aSnapshots[!g_Config.m_ClDummy][SNAP_PREV]->m_Tick;
					}
					else
						break;
				}
				else
					break;
			}
		}

		// switch snapshot
		int Repredict = 0;
		int64 Freq = time_freq();
		int64 Now = m_GameTime[g_Config.m_ClDummy].Get(time_get());
		int64 PredNow = m_PredictedTime.Get(time_get());

		while(1)
		{
			CSnapshotStorage::CHolder *pCur = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT];
			int64 TickStart = (pCur->m_Tick)*time_freq()/50;

			if(TickStart < Now)
			{
				CSnapshotStorage::CHolder *pNext = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_pNext;
				if(pNext)
				{
					m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV] = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT];
					m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] = pNext;

					// set ticks
					m_CurGameTick[g_Config.m_ClDummy] = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick;
					m_PrevGameTick[g_Config.m_ClDummy] = m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick;

					if (m_LastDummy2 == (bool)g_Config.m_ClDummy && m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] && m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV])
					{
						//GameClient()->OnNewSnapshot();

						// secure snapshot
						{
							int Num = SnapNumItems(IClient::SNAP_CURRENT);
							for(int Index = 0; Index < Num; Index++)
							{
								IClient::CSnapItem Item;
								void *pData = SnapGetItem(IClient::SNAP_CURRENT, Index, &Item);
								if(m_NetObjHandler.ValidateObj(Item.m_Type, pData, Item.m_DataSize) != 0)
								{
									SnapInvalidateItem(IClient::SNAP_CURRENT, Index);
								}
							}
						}

						// go trough all the items in the snapshot and gather the info we want
						{
							int Num = SnapNumItems(IClient::SNAP_CURRENT);
							for(int i = 0; i < Num; i++)
							{
								IClient::CSnapItem Item;
								const void *pData = SnapGetItem(IClient::SNAP_CURRENT, i, &Item);
							}
						}

						Repredict = 1;
					}
				}
				else
					break;
			}
			else
				break;
		}

		if (m_LastDummy2 != (bool)g_Config.m_ClDummy)
		{
			m_LastDummy2 = g_Config.m_ClDummy;
		}

		if(m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] && m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV])
		{
			int64 CurtickStart = (m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick)*time_freq()/50;
			int64 PrevtickStart = (m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick)*time_freq()/50;
			int PrevPredTick = (int)(PredNow*50/time_freq());
			int NewPredTick = PrevPredTick+1;

			m_GameIntraTick[g_Config.m_ClDummy] = (Now - PrevtickStart) / (float)(CurtickStart-PrevtickStart);
			m_GameTickTime[g_Config.m_ClDummy] = (Now - PrevtickStart) / (float)Freq; //(float)SERVER_TICK_SPEED);

			CurtickStart = NewPredTick*time_freq()/50;
			PrevtickStart = PrevPredTick*time_freq()/50;
			m_PredIntraTick[g_Config.m_ClDummy] = (PredNow - PrevtickStart) / (float)(CurtickStart-PrevtickStart);

			if(NewPredTick < m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick-SERVER_TICK_SPEED || NewPredTick > m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick+SERVER_TICK_SPEED)
			{
				//m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", "prediction time reset!");
				dbg_msg("client", "prediction time reset!");
				m_PredictedTime.Init(m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick*time_freq()/50);
			}

			if(NewPredTick > m_PredTick[g_Config.m_ClDummy])
			{
				m_PredTick[g_Config.m_ClDummy] = NewPredTick;
				Repredict = 1;

				// send input
				SendInput();
			}
		}

		// only do sane predictions
		/*if(Repredict)
		{
			if(m_PredTick[g_Config.m_ClDummy] > m_CurGameTick[g_Config.m_ClDummy] && m_PredTick[g_Config.m_ClDummy] < m_CurGameTick[g_Config.m_ClDummy]+50)
				GameClient()->OnPredict();
		}*/

		// fetch server info if we don't have it
		if(State() >= IClient::STATE_LOADING &&
			m_CurrentServerInfoRequestTime >= 0 &&
			time_get() > m_CurrentServerInfoRequestTime)
		{
			// Call both because we can't know what kind the server is
			InfoRequestImpl(m_ServerAddress);
			InfoRequestImpl64(m_ServerAddress);
			m_CurrentServerInfoRequestTime = time_get()+time_freq()*2;
		}
	}

	// STRESS TEST: join the server again
	if(g_Config.m_DbgStress)
	{
		static int64 ActionTaken = 0;
		int64 Now = time_get();
		if(State() == IClient::STATE_OFFLINE)
		{
			if(Now > ActionTaken+time_freq()*2)
			{
				//m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "stress", "reconnecting!");
				Connect(g_Config.m_DbgStressServer);
				ActionTaken = Now;
			}
		}
		else
		{
			if(Now > ActionTaken+time_freq()*(10+g_Config.m_DbgStress))
			{
				//m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "stress", "disconnecting!");
				Disconnect();
				ActionTaken = Now;
			}
		}
	}

	// pump the network
	PumpNetwork();
}

void CClient::Run()
{
	m_LocalStartTime = time_get();
	m_SnapshotParts = 0;

	srand(time(NULL));

	// open socket
	{
		NETADDR BindAddr;
		if(net_host_lookup("0.0.0.0", &BindAddr, NETTYPE_ALL) == 0)
		{
			// got bindaddr
			BindAddr.type = NETTYPE_ALL;
		}
		else
		{
			mem_zero(&BindAddr, sizeof(BindAddr));
			BindAddr.type = NETTYPE_ALL;
		}
		for(int i = 0; i < 3; i++)
		{
			do
			{
				BindAddr.port = (rand() % 64511) + 1024;
			}
			while(!m_NetClient[i].Open(BindAddr, 0));
		}
	}

	// init the input
	Input()->Init();

	Input()->MouseModeRelative();

	while (1)
	{
		// handle pending connects
		if(m_aCmdConnect[0])
		{
			Connect(m_aCmdConnect);
			m_aCmdConnect[0] = 0;
		}

		Input()->Update();

		// check conditions
		if(State() == IClient::STATE_QUITING || State() == IClient::STATE_OFFLINE)
			break;

		Update();

		// update local time
		m_LocalTime = (time_get()-m_LocalStartTime)/(float)time_freq();
#ifdef ARM9
		if (keysDown() & KEY_START)
			Disconnect();

		/*CNetObj_PlayerInput* input = (CNetObj_PlayerInput*)m_aInputs[0][m_CurrentInput[0]].m_aData;
		input->m_Direction = (held & KEY_LEFT) ? -1 : (held & KEY_RIGHT) ? 1 : 0;
		input->m_Jump = (held & KEY_UP || held & KEY_B) ? 1 : 0;
		input->m_Hook = (held & KEY_L) ? 1 : 0;

		int fire = ((down & KEY_A || up & KEY_A)) ? 1 : 0;
		if ((input->m_Fire&1) != fire)
		{
			input->m_Fire++;
			input->m_Fire &= INPUT_STATE_MASK;
		}

		// aiming
		if (held & KEY_TOUCH)
		{
			touchPosition thisXY;
			static touchPosition lastXY = {0,0,0,0};
			touchRead(&thisXY);

			int16 dx = (thisXY.px - lastXY.px) / 2;
			int16 dy = (thisXY.py - lastXY.py) / 2;

			input->m_TargetX += dx;
			input->m_TargetY += dy;

			lastXY = thisXY;
		}
		*/

		swiWaitForVBlank();
#endif
	}
}



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

	// create the components
	IEngine *pEngine = CreateEngine("Teeworlds");
	IConsole *pConsole = CreateConsole(CFGFLAG_CLIENT);
	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_CLIENT, argc, argv); // ignore_convention
	IConfig *pConfig = CreateConfig();
	IEngineInput *pEngineInput = CreateEngineInput();

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfig);

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineInput*>(pEngineInput)); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IInput*>(pEngineInput));

		pKernel->RegisterInterface(pStorage);

		while (RegisterFail) swiWaitForVBlank(); // stop here if registering an interface fails
	}

	pEngine->Init();
	pConfig->Init();

	// register all console commands
	//pClient->RegisterCommands();

	//pKernel->RequestInterface<IGameClient>()->OnConsoleInit();

	// init client's interfaces
	pClient->InitInterfaces();

	pClient->Engine()->InitLogfile();

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
	str_copy(pClient->m_aCmdConnect, "74.91.124.108:8300", sizeof(pClient->m_aCmdConnect));
	str_copy(m_DesiredName, "libnds", MAX_NAME_LENGTH);
	str_copy(m_DesiredClan, "BlocksDS", MAX_CLAN_LENGTH);
	pClient->Run();
	while (1) swiWaitForVBlank();
#else
	str_copy(pClient->m_aCmdConnect, argv[1], sizeof(pClient->m_aCmdConnect));
	str_copy(m_DesiredName, (argc > 2) ? argv[2] : "chatonly", MAX_NAME_LENGTH);
	str_copy(m_DesiredClan, (argc > 3) ? argv[3] : "test", MAX_CLAN_LENGTH);
	pClient->Run();
#endif

	// write down the config and quit
	pConfig->Save();

	return 0;
}