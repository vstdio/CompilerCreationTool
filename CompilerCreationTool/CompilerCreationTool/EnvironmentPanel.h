#pragma once
#include "fwd.h"
#include <wx/panel.h>

class EnvironmentPanel : public wxPanel
{
public:
	EnvironmentPanel(wxWindow* parent);

private:
	wxStaticBox* m_box;
};
