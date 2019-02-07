#include "pch.h"
#include "MainFrame.h"
#include "GrammarPanel.h"

#include "../Grammar/Grammar.h"
#include "../Grammar/Production.h"
#include "../Grammar/ProductionFactory.h"

#include <wx/artprov.h>
#include <wx/statline.h>

#include <string>
#include <sstream>

namespace
{
enum Buttons
{
	Build = 7
};
}

MainFrame::MainFrame(const wxString& title, const wxSize& size)
	: wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, size)
	, m_panel(new MainPanel(this))
{
	wxMenu* file = new wxMenu;
	file->Append(wxID_EXIT);

	wxMenu* help = new wxMenu;
	help->Append(wxID_ABOUT);

	wxMenuBar* menubar = new wxMenuBar;
	menubar->Append(file, wxT("&File"));
	menubar->Append(help, wxT("&Help"));
	SetMenuBar(menubar);

	wxToolBar* toolbar = CreateToolBar(wxTB_FLAT);

	wxIcon newIcon = wxArtProvider::GetIcon(wxART_NEW, wxART_OTHER, wxSize(24, 24));
	wxIcon saveIcon = wxArtProvider::GetIcon(wxART_FILE_SAVE, wxART_OTHER, wxSize(24, 24));
	wxIcon saveAsIcon = wxArtProvider::GetIcon(wxART_FILE_SAVE_AS, wxART_OTHER, wxSize(24, 24));
	wxIcon infoIcon = wxArtProvider::GetIcon(wxART_INFORMATION, wxART_OTHER, wxSize(24, 24));
	wxIcon buildIcon = wxArtProvider::GetIcon(wxART_EXECUTABLE_FILE, wxART_OTHER, wxSize(24, 24));
	wxIcon openIcon = wxArtProvider::GetIcon(wxART_FILE_OPEN, wxART_OTHER, wxSize(24, 24));
	wxIcon undoIcon = wxArtProvider::GetIcon(wxART_UNDO, wxART_OTHER, wxSize(24, 24));
	wxIcon redoIcon = wxArtProvider::GetIcon(wxART_REDO, wxART_OTHER, wxSize(24, 24));
	wxIcon helpIcon = wxArtProvider::GetIcon(wxART_QUESTION, wxART_OTHER, wxSize(24, 24));

	toolbar->AddTool(1, wxT("New"), newIcon, wxT("New Document"));
	toolbar->AddTool(2, wxT("Open"), openIcon, wxT("Open Document"));
	toolbar->AddTool(3, wxT("Save"), saveIcon, wxT("Save Document"));
	toolbar->AddTool(4, wxT("Save As"), saveAsIcon, wxT("Save Document As"));
	toolbar->AddSeparator();
	toolbar->AddTool(5, wxT("Undo"), undoIcon, wxT("Undo Command"));
	toolbar->AddTool(6, wxT("Redo"), redoIcon, wxT("Redo Command"));
	toolbar->AddSeparator();
	toolbar->AddTool(Buttons::Build, wxT("Build"), buildIcon, wxT("Build"));
	toolbar->AddTool(8, wxT("Info"), infoIcon, wxT("Get Information"));
	toolbar->AddTool(9, wxT("Help"), helpIcon, wxT("Help"));
	toolbar->Realize();

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	wxStaticLine* line = new wxStaticLine(this, wxID_ANY);
	sizer->Add(line, 0, wxEXPAND);
	sizer->Add(m_panel, 1, wxEXPAND);
	SetSizer(sizer);

	CreateStatusBar();
	SetStatusText(wxT("Welcome to CompilerCreationTool!"));

	Centre();
}

void MainFrame::OnExit(wxCommandEvent& event)
{
	Close(true);
}

void MainFrame::OnAbout(wxCommandEvent& event)
{
	wxMessageBox(
		"Tool for building compiler.",
		"About CompilerCreationTool",
		wxOK | wxICON_INFORMATION
	);
}

void MainFrame::OnBuild(wxCommandEvent& event)
{
	GrammarPanel* grammarPanel = m_panel->GetGrammarPanel();
	const std::string grammarText = grammarPanel->GetGrammarText();

	try
	{
		auto grammar = std::make_unique<grammarlib::Grammar>();
		auto factory = std::make_unique<grammarlib::ProductionFactory>();

		std::string line;
		std::istringstream strm(grammarText);

		while (getline(strm, line))
		{
			auto production = factory->CreateProduction(line);
			grammar->AddProduction(std::move(production));
		}

		std::cout << grammar->GetProductionsCount() << std::endl;
	}
	catch (const std::exception& ex)
	{
		wxMessageBox("Can't build grammar: " + std::string(ex.what()));
	}
}

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
	EVT_MENU(wxID_EXIT, MainFrame::OnExit)
	EVT_MENU(wxID_ABOUT, MainFrame::OnAbout)
	EVT_TOOL(Buttons::Build, MainFrame::OnBuild)
wxEND_EVENT_TABLE()
