/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <nds.h>

#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/input.h>
#include <engine/keys.h>

#include "input.h"

//print >>f, "int inp_key_code(const char *key_name) { int i; if (!strcmp(key_name, \"-?-\")) return -1; else for (i = 0; i < 512; i++) if (!strcmp(key_strings[i], key_name)) return i; return -1; }"

// this header is protected so you don't include it from anywere
#define KEYS_INCLUDE
#include "keynames.h"
#undef KEYS_INCLUDE

void CInput::AddEvent(int Unicode, int Key, int Flags)
{
	if(m_NumEvents != INPUT_BUFFER_SIZE)
	{
		m_aInputEvents[m_NumEvents].m_Unicode = Unicode;
		m_aInputEvents[m_NumEvents].m_Key = Key;
		m_aInputEvents[m_NumEvents].m_Flags = Flags;
		m_NumEvents++;
	}
}

CInput::CInput()
{
	mem_zero(m_aInputCount, sizeof(m_aInputCount));
	mem_zero(m_aInputState, sizeof(m_aInputState));

	m_InputCurrent = 0;
	m_InputGrabbed = 0;
	m_InputDispatched = false;

	m_LastRelease = 0;
	m_ReleaseDelta = -1;

	m_NumEvents = 0;

	m_VideoRestartNeeded = 0;
}

void CInput::Init()
{
	
}

void CInput::MouseRelative(float *x, float *y)
{
	int nx = 0, ny = 0;

	touchPosition thisXY;
	touchRead(&thisXY);
	//static touchPosition lastXY = {0,0,0,0};

	nx = thisXY.px;
	ny = thisXY.py;

	//lastXY = thisXY;

	*x = nx;
	*y = ny;
}

void CInput::MouseModeAbsolute()
{
	m_InputGrabbed = 0;
}

void CInput::MouseModeRelative()
{
	m_InputGrabbed = 1;
}

int CInput::MouseDoubleClick()
{
	if(m_ReleaseDelta >= 0 && m_ReleaseDelta < (time_freq() / 3))
	{
		m_LastRelease = 0;
		m_ReleaseDelta = -1;
		return 1;
	}
	return 0;
}

void CInput::ClearKeyStates()
{
	mem_zero(m_aInputState, sizeof(m_aInputState));
	mem_zero(m_aInputCount, sizeof(m_aInputCount));
}

int CInput::KeyState(int Key)
{
	return m_aInputState[m_InputCurrent][Key];
}

int CInput::Update()
{
	if(m_InputDispatched)
	{
		// clear and begin count on the other one
		m_InputCurrent^=1;
		mem_zero(&m_aInputCount[m_InputCurrent], sizeof(m_aInputCount[m_InputCurrent]));
		mem_zero(&m_aInputState[m_InputCurrent], sizeof(m_aInputState[m_InputCurrent]));
		m_InputDispatched = false;
	}

	{
		scanKeys();

		int down = keysDown();
		int Key = -1;
		int Action = IInput::FLAG_PRESS;

		if (down & KEY_LEFT) Key = KEY_a;
		if (down & KEY_RIGHT) Key = KEY_d;
		if (down & KEY_A) Key = KEY_RETURN;
		if (down & KEY_B) Key = KEY_SPACE;
		if (down & KEY_TOUCH || down & KEY_Y) Key = KEY_MOUSE_1;
		if (down & KEY_L) Key = KEY_MOUSE_2;
		if (down & KEY_R) Key = KEY_MOUSE_WHEEL_DOWN;
		if (down & KEY_START) Key = KEY_ESCAPE;

		if(Key != -1)
		{
			m_aInputCount[m_InputCurrent][Key].m_Presses++;
			m_aInputState[m_InputCurrent][Key] = 1;
			AddEvent(0, Key, Action);
		}

		int up = keysUp();
		Key = -1;
		Action = IInput::FLAG_RELEASE;

		if (up & KEY_LEFT) Key = KEY_a;
		if (up & KEY_RIGHT) Key = KEY_d;
		if (up & KEY_A) Key = KEY_RETURN;
		if (up & KEY_B) Key = KEY_SPACE;
		if (up & KEY_TOUCH || up & KEY_Y) Key = KEY_MOUSE_1;
		if (up & KEY_L) Key = KEY_MOUSE_2;
		if (up & KEY_R) Key = KEY_MOUSE_WHEEL_DOWN;
		if (up & KEY_START) Key = KEY_ESCAPE;

		if(Key != -1)
		{
			m_aInputCount[m_InputCurrent][Key].m_Presses++;
			AddEvent(0, Key, Action);
		}
	}

	/*
	{
		SDL_Event Event;

		while(SDL_PollEvent(&Event))
		{
			int Key = -1;
			int Action = IInput::FLAG_PRESS;
			switch (Event.type)
			{
				// handle keys
				case SDL_KEYDOWN:
					// skip private use area of the BMP(contains the unicodes for keyboard function keys on MacOS)
					if(Event.key.keysym.unicode < 0xE000 || Event.key.keysym.unicode > 0xF8FF)	// ignore_convention
						AddEvent(Event.key.keysym.unicode, 0, 0); // ignore_convention
					Key = Event.key.keysym.sym; // ignore_convention
					break;
				case SDL_KEYUP:
					Action = IInput::FLAG_RELEASE;
					Key = Event.key.keysym.sym; // ignore_convention
					break;

				// handle mouse buttons
				case SDL_MOUSEBUTTONUP:
					Action = IInput::FLAG_RELEASE;

					if(Event.button.button == 1) // ignore_convention
					{
						m_ReleaseDelta = time_get() - m_LastRelease;
						m_LastRelease = time_get();
					}

					// fall through
				case SDL_MOUSEBUTTONDOWN:
					if(Event.button.button == SDL_BUTTON_LEFT) Key = KEY_MOUSE_1; // ignore_convention
					if(Event.button.button == SDL_BUTTON_RIGHT) Key = KEY_MOUSE_2; // ignore_convention
					if(Event.button.button == SDL_BUTTON_MIDDLE) Key = KEY_MOUSE_3; // ignore_convention
					if(Event.button.button == SDL_BUTTON_WHEELUP) Key = KEY_MOUSE_WHEEL_UP; // ignore_convention
					if(Event.button.button == SDL_BUTTON_WHEELDOWN) Key = KEY_MOUSE_WHEEL_DOWN; // ignore_convention
					if(Event.button.button == 6) Key = KEY_MOUSE_6; // ignore_convention
					if(Event.button.button == 7) Key = KEY_MOUSE_7; // ignore_convention
					if(Event.button.button == 8) Key = KEY_MOUSE_8; // ignore_convention
					if(Event.button.button == 9) Key = KEY_MOUSE_9; // ignore_convention
					break;

				// other messages
				case SDL_QUIT:
					return 1;

#if defined(__ANDROID__)
				case SDL_VIDEORESIZE:
					m_VideoRestartNeeded = 1;
					break;
#endif
			}

			//
			if(Key != -1)
			{
				m_aInputCount[m_InputCurrent][Key].m_Presses++;
				if(Action == IInput::FLAG_PRESS)
					m_aInputState[m_InputCurrent][Key] = 1;
				AddEvent(0, Key, Action);
			}

		}
	}
	*/

	return 0;
}

int CInput::VideoRestartNeeded()
{
	if( m_VideoRestartNeeded )
	{
		m_VideoRestartNeeded = 0;
		return 1;
	}
	return 0;
}

IEngineInput *CreateEngineInput() { return new CInput; }
