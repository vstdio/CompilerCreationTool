#include "pch.h"
#include "ParserStatesView.h"
#include "../Utils/string_utils.h"
#include <map>

namespace
{
const std::string gcNoItem = "<none>";

std::string GetStateFlagsRepresentation(const IParserState& state)
{
	std::set<std::string> flags;
	for (const StateFlag& flag : GetAllStateFlags())
	{
		if (state.GetFlag(flag))
		{
			flags.insert(ToString(flag));
		}
	}
	return flags.empty() ? gcNoItem : string_utils::JoinStrings(flags);
};

int GetBestColumnWidth(int index, int maxWidth)
{
	static const std::map<int, float> percentages = {
		{ 0, 0.2f },
		{ 1, 0.2f },
		{ 2, 0.2f },
		{ 3, 0.2f },
		{ 4, 0.2f }
	};

	auto found = percentages.find(index);
	assert(found != percentages.end());
	const float width = maxWidth * found->second;
	return int(width);
}
}

ParserStatesView::ParserStatesView(wxWindow* parent)
	: wxPanel(parent, wxID_ANY)
{
	m_list = new wxListView(this, wxID_ANY);

	m_list->AppendColumn(wxT("Index"));
	m_list->AppendColumn(wxT("Name"));
	m_list->AppendColumn(wxT("Next"));
	m_list->AppendColumn(wxT("Flags"));
	m_list->AppendColumn(wxT("Acceptables"));

	wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_list, 1, wxEXPAND | wxALL, 5);
	SetSizerAndFit(sizer);
}

void ParserStatesView::SetParserTable(const IParserTable& table)
{
	m_list->DeleteAllItems();

	for (size_t i = 0; i < table.GetStatesCount(); ++i)
	{
		const long index = m_list->InsertItem(m_list->GetItemCount(), std::to_string(i));

		const IParserState& state = table.GetState(i);
		const std::string& name = state.GetName();
		const boost::optional<size_t> address = state.GetNextAddress();
		const bool isAttribute = state.GetFlag(StateFlag::Attribute);
		const std::set<std::string>* acceptables = state.GetAcceptableTerminals();

		m_list->SetItem(index, 1, name);
		m_list->SetItem(index, 2, address ? std::to_string(*address) : gcNoItem);
		m_list->SetItem(index, 3, GetStateFlagsRepresentation(state));
		m_list->SetItem(index, 4, isAttribute ? gcNoItem : string_utils::JoinStrings(*acceptables));
	}
}

void ParserStatesView::AdjustColumnWidth()
{
	const int cWidth = m_list->GetSize().GetWidth();
	for (int i = 0; i < m_list->GetColumnCount(); ++i)
	{
		m_list->SetColumnWidth(i, GetBestColumnWidth(i, cWidth));
	}
}
