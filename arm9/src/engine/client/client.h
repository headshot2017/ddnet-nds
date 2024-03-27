#include <base/system.h>
#include <engine/client.h>
#include <engine/engine.h>
#include <engine/shared/protocol.h>
#include <engine/shared/network.h>
#include <engine/shared/snapshot.h>
#include <engine/serverbrowser.h>
#include <game/generated/protocol.h>

class CSmoothTime
{
	unsigned m_Snap;
	unsigned m_Current;
	unsigned m_Target;

	int m_SpikeCounter;

	float m_aAdjustSpeed[2]; // 0 = down, 1 = up
public:
	void Init(unsigned Target);
	void SetAdjustSpeed(int Direction, float Value);

	unsigned Get(unsigned Now);

	void UpdateInt(unsigned Target);
	void Update(unsigned Target, int TimeLeft, int AdjustDirection);
};

class CClient : public IClient
{
	// needed interfaces
	IEngine *m_pEngine;
	//IGameClient *m_pGameClient;

	enum
	{
		NUM_SNAPSHOT_TYPES=2,
		PREDICTION_MARGIN=1000/50/2, // magic network prediction value
	};

	class CNetClient m_NetClient[3];
	
	char m_aServerAddressStr[256];

	unsigned m_SnapshotParts;
	unsigned m_LocalStartTime;
	bool m_DDRaceMsgSent;

	NETADDR m_ServerAddress;
	int m_SnapCrcErrors;

	int m_AckGameTick[2];
	int m_CurrentRecvTick[2];
	
	// version-checking
	char m_aVersionStr[10];

	// pinging
	unsigned m_PingStartTime;

	//
	char m_aCurrentMap[256];
	unsigned m_CurrentMapCrc;
	
	// time
	CSmoothTime m_GameTime[2];
	CSmoothTime m_PredictedTime;
	
	// input
	struct // TODO: handle input better
	{
		int m_aData[MAX_INPUT_SIZE]; // the input data
		int m_Tick; // the tick that the input is for
		unsigned m_PredictedTime; // prediction latency when we sent this input
		unsigned m_Time;
	} m_aInputs[2][200];

	int m_CurrentInput[2];
	bool m_LastDummy;
	bool m_LastDummy2;
	
	// the game snapshots are modifiable by the game
	class CSnapshotStorage m_SnapshotStorage[2];
	CSnapshotStorage::CHolder *m_aSnapshots[2][NUM_SNAPSHOT_TYPES];

	int m_ReceivedSnapshots[2];
	char m_aSnapshotIncomingData[CSnapshot::MAX_SIZE];

	class CSnapshotStorage::CHolder m_aDemorecSnapshotHolders[NUM_SNAPSHOT_TYPES];
	char *m_aDemorecSnapshotData[NUM_SNAPSHOT_TYPES][2][CSnapshot::MAX_SIZE];

	class CSnapshotDelta m_SnapshotDelta;

	//
	class CServerInfo m_CurrentServerInfo;
	unsigned m_CurrentServerInfoRequestTime; // >= 0 should request, == -1 got info

	// version info
	struct CVersionInfo
	{
		enum
		{
			STATE_INIT=0,
			STATE_START,
			STATE_READY,
		};

		int m_State;
	} m_VersionInfo;

	CNetObjHandler m_NetObjHandler;

public:
	IEngine *Engine() { return m_pEngine; }

	CClient();

	// ----- send functions -----
	virtual int SendMsg(CMsgPacker *pMsg, int Flags);
	virtual int SendMsgExY(CMsgPacker *pMsg, int Flags, bool System=true, int NetClient=1);

	//
	char m_aCmdConnect[256];
	virtual void Connect(const char *pAddress);
	virtual void DisconnectWithReason(const char *pReason);
	virtual void Disconnect();

	int SendMsgEx(CMsgPacker *pMsg, int Flags, bool System=true);
	void SendInfo();
	void SendEnterGame();
	void SendReady();
	void SendInput();

	// ------ state handling -----
	void SetState(int s);

	// called when the map is loaded and we should init for a new round
	void OnEnterGame();
	virtual void EnterGame();

	virtual void GetServerInfo(class CServerInfo *pServerInfo);
	void ServerInfoRequest();

	// ---

	void *SnapGetItem(int SnapID, int Index, CSnapItem *pItem);
	void SnapInvalidateItem(int SnapID, int Index);
	void *SnapFindItem(int SnapID, int Type, int ID);
	int SnapNumItems(int SnapID);
	void SnapSetStaticsize(int ItemType, int Size);

	virtual const char *ErrorString();
	const char *LatestVersion();
	virtual bool ConnectionProblems();

	void ProcessConnlessPacket(CNetChunk *pPacket);
	void ProcessServerPacket(CNetChunk *pPacket);

	void InfoRequestImpl(const NETADDR &Addr);
	void InfoRequestImpl64(const NETADDR &Addr);

	void RegisterInterfaces();
	void InitInterfaces();

	void PumpNetwork();

	void Update();
	void Run();
};