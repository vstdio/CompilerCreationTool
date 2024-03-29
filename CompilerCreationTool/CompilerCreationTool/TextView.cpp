#include "pch.h"
#include "TextView.h"
#include "CtrlHelpers.h"

TextView::TextView(wxWindow* parent)
	: wxPanel(parent)
{
	mInput = new wxStyledTextCtrl(
		this, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxTE_DONTWRAP);
	SetupStyledTextCtrlMargins(*mInput);

	mInput->SetScrollWidth(mInput->GetSize().GetWidth());
	mInput->SetScrollWidthTracking(true);

	wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(mInput, 1, wxEXPAND | wxALL, 3);
	SetSizerAndFit(sizer);

	mInput->Bind(wxEVT_STC_UPDATEUI, &TextView::OnUpdateUI, this);
	mInput->Bind(wxEVT_SET_FOCUS, &TextView::OnTextFocusGain, this);
	mInput->Bind(wxEVT_KILL_FOCUS, &TextView::OnTextFocusLost, this);
}

void TextView::SetText(const wxString& text)
{
	mInput->SetText(text);
}

wxString TextView::GetText() const
{
	return mInput->GetText();
}

void TextView::Clear()
{
	mInput->ClearAll();
}

int TextView::GetCurrentLine() const
{
	return mInput->GetCurrentLine() + 1;
}

int TextView::GetCurrentCol() const
{
	return mInput->GetColumn(mInput->GetCurrentPos()) + 1;
}

int TextView::GetCurrentCh() const
{
	int chPosAtLine = 0;
	mInput->GetCurLine(&chPosAtLine);
	return chPosAtLine + 1;
}

SignalScopedConnection TextView::DoOnUIUpdate(UpdateUISignal::slot_type slot)
{
	return mUpdateUISignal.connect(slot);
}

SignalScopedConnection TextView::DoOnFocusChange(FocusChangeSignal::slot_type slot)
{
	return mFocusChangeSignal.connect(slot);
}

void TextView::OnUpdateUI(wxStyledTextEvent& event)
{
	mUpdateUISignal(GetCurrentLine(), GetCurrentCol(), GetCurrentCh());
	event.Skip(true);
}

void TextView::OnTextFocusGain(wxFocusEvent& event)
{
	mFocusChangeSignal(true);
	event.Skip(true);
}

void TextView::OnTextFocusLost(wxFocusEvent& event)
{
	mFocusChangeSignal(false);
	event.Skip(true);
}
