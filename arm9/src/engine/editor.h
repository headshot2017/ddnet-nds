/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_EDITOR_H
#define ENGINE_EDITOR_H
#include "kernel.h"

class IEditor : public IInterface
{
	MACRO_INTERFACE("editor", 0)
public:

	virtual ~IEditor() {}
	virtual void Init() {}
	virtual void UpdateAndRender() {}
	virtual bool HasUnsavedData() { return false; }
};

extern IEditor *CreateEditor();
#endif
