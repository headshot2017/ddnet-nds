/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_H
#define ENGINE_CLIENT_H
#include "kernel.h"

#include "message.h"

class IClient : public IInterface
{
	MACRO_INTERFACE("client", 0)
protected:
	// quick access to state of the client
	int m_State;

	// quick access to time variables
	int m_PrevGameTick[2];
	int m_CurGameTick[2];
	float m_GameIntraTick[2];
	float m_GameTickTime[2];

	int m_PredTick[2];
	float m_PredIntraTick[2];

	float m_LocalTime;
	float m_RenderFrameTime;

	int m_GameTickSpeed;
public:
	int m_LocalIDs[2];

	bool m_DummySendConnInfo;

	class CSnapItem
	{
	public:
		int m_Type;
		int m_ID;
		int m_DataSize;
	};

	/* Constants: Client States
		STATE_OFFLINE - The client is offline.
		STATE_CONNECTING - The client is trying to connect to a server.
		STATE_LOADING - The client has connected to a server and is loading resources.
		STATE_ONLINE - The client is connected to a server and running the game.
		STATE_DEMOPLAYBACK - The client is playing a demo
		STATE_QUITING - The client is quiting.
	*/

	enum
	{
		STATE_OFFLINE=0,
		STATE_CONNECTING,
		STATE_LOADING,
		STATE_ONLINE,
		STATE_DEMOPLAYBACK,
		STATE_QUITING,
	};

	//
	inline int State() const { return m_State; }

	// tick time access
	inline int PrevGameTick() const { return m_PrevGameTick[0]; }
	inline int GameTick() const { return m_CurGameTick[0]; }
	inline int PredGameTick() const { return m_PredTick[0]; }
	inline float IntraGameTick() const { return m_GameIntraTick[0]; }
	inline float PredIntraGameTick() const { return m_PredIntraTick[0]; }
	inline float GameTickTime() const { return m_GameTickTime[0]; }
	inline int GameTickSpeed() const { return m_GameTickSpeed; }

	// other time access
	inline float RenderFrameTime() const { return m_RenderFrameTime; }
	inline float LocalTime() const { return m_LocalTime; }

	// actions
	virtual void Connect(const char *pAddress) = 0;
	virtual void Disconnect() = 0;

	// networking
	virtual void EnterGame() = 0;

	//
	//virtual const char *MapDownloadName() = 0;
	//virtual int MapDownloadAmount() = 0;
	//virtual int MapDownloadTotalsize() = 0;

	// input
	//virtual int *GetInput(int Tick) = 0;
	//virtual bool InputExists(int Tick) = 0;

	// remote console
	//virtual void RconAuth(const char *pUsername, const char *pPassword) = 0;
	//virtual bool RconAuthed() = 0;
	//virtual bool UseTempRconCommands() = 0;
	//virtual void Rcon(const char *pLine) = 0;

	// server info
	virtual void GetServerInfo(class CServerInfo *pServerInfo) = 0;

	//virtual void CheckVersionUpdate() = 0;

	// snapshot interface

	enum
	{
		SNAP_CURRENT=0,
		SNAP_PREV=1
	};

	// TODO: Refactor: should redo this a bit i think, too many virtual calls
	virtual int SnapNumItems(int SnapID) = 0;
	virtual void *SnapFindItem(int SnapID, int Type, int ID) = 0;
	virtual void *SnapGetItem(int SnapID, int Index, CSnapItem *pItem) = 0;
	virtual void SnapInvalidateItem(int SnapID, int Index) = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags) = 0;
	virtual int SendMsgExY(CMsgPacker *pMsg, int Flags, bool System=true, int NetClient=1) = 0;

	template<class T>
	int SendPackMsg(T *pMsg, int Flags)
	{
		CMsgPacker Packer(pMsg->MsgID());
		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags);
	}

	//
	virtual const char *ErrorString() = 0;
	virtual const char *LatestVersion() = 0;
	virtual bool ConnectionProblems() = 0;

	//DDRace

	/*
	virtual const char* GetCurrentMap() = 0;
	virtual int GetCurrentMapCrc() = 0;
	virtual const char* RaceRecordStart(const char *pFilename) = 0;
	virtual void RaceRecordStop() = 0;
	virtual bool RaceRecordIsRecording() = 0;

	virtual void DemoSliceBegin() = 0;
	virtual void DemoSliceEnd() = 0;
	virtual void DemoSlice(const char *pDstPath) = 0;

	virtual void RequestDDNetSrvList() = 0;
	virtual bool EditorHasUnsavedData() = 0;
	*/
};

#endif
