/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_INPUT_H
#define ENGINE_CLIENT_INPUT_H

class CInput : public IEngineInput
{
	int m_InputGrabbed;

	int64 m_LastRelease;
	int64 m_ReleaseDelta;

	int m_VideoRestartNeeded;

	void AddEvent(int Unicode, int Key, int Flags);

public:
	CInput();

	virtual void Init();

	virtual void MouseRelative(float *x, float *y);
	virtual void MouseModeAbsolute();
	virtual void MouseModeRelative();
	virtual int MouseDoubleClick();

	void ClearKeyStates();
	int KeyState(int Key);

	int ButtonPressed(int Button) { return m_aInputState[m_InputCurrent][Button]; }

	virtual int Update();

	virtual int VideoRestartNeeded();
};

#endif
