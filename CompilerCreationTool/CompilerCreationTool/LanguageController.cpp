#include "pch.h"
#include "LanguageController.h"

#include "TerminalEditDialog.h"
#include "ActionEditDialog.h"
#include "TextCtrlLogger.h"

#include "LanguageSerialization.h"
#include "LanguageInfoDialog.h"
#include "ASTGraphvizVisualizer.h"

#include "../Grammar/GrammarBuilder.h"
#include "../Grammar/GrammarUtils.h"
#include "../Utils/command_utils.h"
#include "../Utils/time_utils.h"
#include "../Utils/string_utils.h"
#include "../Codegen/CodegenVisitor.h"

#include <boost/filesystem.hpp>
#include <wx/filedlg.h>
#include <functional>
#include <fstream>
#include <sstream>

using namespace std::literals::string_literals;

namespace
{
const std::string gcFrameTitle = "CompilerCreationTool";
const std::string gcUntitledFilename = "untitled";

std::vector<std::pair<std::string, std::string>> GetTerminalsArray(const ILexer& lexer)
{
	std::vector<std::pair<std::string, std::string>> arr;
	arr.reserve(lexer.GetPatternsCount());
	for (std::size_t i = 0; i < lexer.GetPatternsCount(); ++i)
	{
		arr.emplace_back(lexer.GetPattern(i).GetName(), lexer.GetPattern(i).GetOrigin());
	}
	return arr;
}

std::vector<std::pair<std::string, std::string>> GetActionsArray(const IParser<ParseResults>& parser)
{
	std::vector<std::pair<std::string, std::string>> arr;
	arr.reserve(parser.GetActionsCount());
	for (std::size_t i = 0; i < parser.GetActionsCount(); ++i)
	{
		arr.emplace_back(parser.GetAction(i).GetName(), ToPrettyString(parser.GetAction(i).GetType()));
	}
	return arr;
}

void ClearStatusBarTextFields(wxStatusBar& statusbar, std::vector<int> && fields)
{
	for (int field : fields)
	{
		statusbar.SetStatusText(wxEmptyString, field);
	}
}
}

LanguageController::LanguageController(Language* language, MainFrame* frame)
	: mLanguage(language)
	, mFrame(frame)
	, mGrammarView(mFrame->GetDeclarationView())
	, mStatesView(mFrame->GetStatesView())
	, mEditorView(mFrame->GetEditorView())
	, mTreeView(mFrame->GetTreeView())
	, mTerminalsView(mFrame->GetTerminalsView())
	, mActionsView(mFrame->GetActionsView())
	, mOutputView(mFrame->GetOutputView())
	, mDocument(boost::none)
	, mHasUnsavedChanges(false)
	, mNeedCodegen(false)
	, mClosing(false)
{
	namespace ph = std::placeholders;

	mConnections.push_back(mFrame->DoOnClose(std::bind(&LanguageController::OnFrameClose, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::New, std::bind(&LanguageController::OnNewButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Open, std::bind(&LanguageController::OnOpenButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Save, std::bind(&LanguageController::OnSaveButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::SaveAs, std::bind(&LanguageController::OnSaveAsButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Clear, std::bind(&LanguageController::OnClearButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::LogMessages, std::bind(&LanguageController::OnLogMessageButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::EnableCodegen, std::bind(&LanguageController::OnEnableCodegenButtonPress, this)));
	mConnections.push_back(mFrame->DoOnHasUnsavedChangesQuery([this]() { return mHasUnsavedChanges; }));

	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Build, std::bind(&LanguageController::OnBuildButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Run, std::bind(&LanguageController::OnRunButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Cancel, std::bind(&LanguageController::OnCancelButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Info, std::bind(&LanguageController::OnInfoButtonPress, this)));

	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Up, std::bind(&LanguageController::OnUpButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Down, std::bind(&LanguageController::OnDownButtonPress, this)));
	mConnections.push_back(mFrame->DoOnButtonPress(Buttons::Edit, std::bind(&LanguageController::OnEditButtonPress, this)));

	mConnections.push_back(mTerminalsView->DoOnItemDeselection(std::bind(&LanguageController::OnTerminalsViewDeselection, this)));
	mConnections.push_back(mActionsView->DoOnItemDeselection(std::bind(&LanguageController::OnActionsViewDeselection, this)));

	mConnections.push_back(mTerminalsView->DoOnItemSelection(std::bind(&LanguageController::OnTerminalSelection, this, ph::_1)));
	mConnections.push_back(mActionsView->DoOnItemSelection(std::bind(&LanguageController::OnActionSelection, this, ph::_1)));

	mConnections.push_back(mTerminalsView->DoOnItemDoubleSelection(std::bind(&LanguageController::OnTerminalEdit, this, ph::_1)));
	mConnections.push_back(mActionsView->DoOnItemDoubleSelection(std::bind(&LanguageController::OnActionEdit, this, ph::_1)));

	mConnections.push_back(mGrammarView->DoOnUIUpdate(std::bind(&LanguageController::OnGrammarTextCtrlUpdateUI, this, ph::_1, ph::_2, ph::_3)));
	mConnections.push_back(mEditorView->DoOnUIUpdate(std::bind(&LanguageController::OnEditorTextCtrlUpdateUI, this, ph::_1, ph::_2, ph::_3)));

	mConnections.push_back(mGrammarView->DoOnFocusChange(std::bind(&LanguageController::OnGrammarTextFocusChange, this, ph::_1)));
	mConnections.push_back(mEditorView->DoOnFocusChange(std::bind(&LanguageController::OnEditorTextFocusChange, this, ph::_1)));
	UpdateTitle();
}

void LanguageController::UpdateStatusBarOnTextFocusChange(TextView& view, const std::string& name, bool focus)
{
	wxStatusBar* statusbar = mFrame->GetStatusBar();

	if (focus)
	{
		UpdateStatusBarCursorInfo(view.GetCurrentLine(), view.GetCurrentCol(), view.GetCurrentCh());
		statusbar->PushStatusText(name, StatusBarFields::ContextInfo);
	}
	else
	{
		ClearStatusBarTextFields(*statusbar, { StatusBarFields::Line, StatusBarFields::Column, StatusBarFields::Ch });
		statusbar->PopStatusText(StatusBarFields::ContextInfo);
	}
}

void LanguageController::UpdateStatusBarCursorInfo(int line, int col, int ch)
{
	wxStatusBar* statusbar = mFrame->GetStatusBar();
	statusbar->SetStatusText("Ln " + std::to_string(line), StatusBarFields::Line);
	statusbar->SetStatusText("Col " + std::to_string(col), StatusBarFields::Column);
	statusbar->SetStatusText("Ch " + std::to_string(ch), StatusBarFields::Ch);
}

void LanguageController::SwapTerminalPositions(int from, int to)
{
	assert(mLanguage->IsInitialized());
	mLanguage->GetLexer().SwapPatterns(size_t(from), size_t(to));
	mHasUnsavedChanges = true;
	UpdateTitle();
}

void LanguageController::SwapActionPositions(int from, int to)
{
	assert(mLanguage->IsInitialized());
	mLanguage->GetParser().SwapActions(size_t(from), size_t(to));
	mHasUnsavedChanges = true;
	UpdateTitle();
}

void LanguageController::UpdateTitle()
{
	namespace fs = boost::filesystem;

	if (mDocument)
	{
		mFrame->SetTitle((mHasUnsavedChanges ? "*" : "") + fs::path(*mDocument).filename().string() + " - " + gcFrameTitle);
	}
	else
	{
		mFrame->SetTitle((mHasUnsavedChanges ? "*" : "") + gcUntitledFilename + " - " + gcFrameTitle);
	}
}

void LanguageController::OnFrameClose()
{
	mClosing = true;
	if (mLanguage->IsInitialized())
	{
		auto& parser = mLanguage->GetParser();
		if (parser.IsParseTaskRunning())
		{
			parser.CancelParseTask();
		}
	}
}

void LanguageController::OnNewButtonPress()
{
#ifdef _DEBUG
	if (mLanguage->IsInitialized())
	{
		assert(!mLanguage->GetParser().IsParseTaskRunning());
	}
#endif

	if (mHasUnsavedChanges)
	{
		if (wxMessageBox("Current content has not been saved! Proceed?", "Please confirm",
			wxICON_QUESTION | wxYES_NO, mFrame) == wxNO)
		{
			return;
		}
	}

	mLanguage->Reset();
	assert(!mLanguage->IsInitialized());

	mTerminalsView->ClearItems();
	mActionsView->ClearItems();
	mStatesView->ClearItems();
	mGrammarView->Clear();
	mEditorView->Clear();
	mTreeView->UnsetImage();
	mOutputView->GetTextCtrl()->Clear();

	mFrame->GetMenuBar()->Enable(Buttons::LogMessages, false);
	mFrame->GetMenuBar()->Check(Buttons::LogMessages, false);
	mFrame->GetMenuBar()->Enable(Buttons::EnableCodegen, false);
	mFrame->GetMenuBar()->Check(Buttons::EnableCodegen, false);
	mFrame->GetMenuBar()->Enable(Buttons::Run, false);
	mFrame->GetMenuBar()->Enable(Buttons::Info, false);
	mFrame->GetMenuBar()->Enable(Buttons::Up, false);
	mFrame->GetMenuBar()->Enable(Buttons::Down, false);
	mFrame->GetMenuBar()->Enable(Buttons::Edit, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Run, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Info, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Down, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Up, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Edit, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Save, false);
	mFrame->GetToolBar()->EnableTool(Buttons::SaveAs, false);
	mFrame->Refresh(true);

	mDocument = boost::none;
	mHasUnsavedChanges = false;
	mNeedCodegen = false;
	UpdateTitle();
}

void LanguageController::OnOpenButtonPress()
{
#ifdef _DEBUG
	if (mLanguage->IsInitialized())
	{
		assert(!mLanguage->GetParser().IsParseTaskRunning());
	}
#endif

	if (mHasUnsavedChanges)
	{
		if (wxMessageBox("Current content has not been saved! Proceed?", "Please confirm",
			wxICON_QUESTION | wxYES_NO, mFrame) == wxNO)
		{
			return;
		}
	}

	wxFileDialog openFileDialog(
		mFrame, "Open language file", wxEmptyString, wxEmptyString,
		"XML files (*.xml)|*.xml", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (openFileDialog.ShowModal() != wxID_CANCEL)
	{
		// �������������� ������
		UnserializeLanguage(openFileDialog.GetPath(), *mLanguage);
		mLanguage->GetParser().SetLogger(std::make_unique<TextCtrlParserLogger>(mOutputView->GetTextCtrl()));

		// ��������� ������������� ���������� �������
		mTerminalsView->SetItems(GetTerminalsArray(mLanguage->GetLexer()));
		mActionsView->SetItems(GetActionsArray(mLanguage->GetParser()));
		mStatesView->SetParserTable(mLanguage->GetParser().GetTable());
		mGrammarView->SetText(ToText(mLanguage->GetGrammar()));
		mTreeView->UnsetImage();
		mEditorView->Clear();

		// ��������� ������
		mDocument = openFileDialog.GetPath();
		mFrame->GetMenuBar()->Enable(Buttons::LogMessages, true);
		mFrame->GetMenuBar()->Check(Buttons::LogMessages, false);
		mFrame->GetMenuBar()->Enable(Buttons::EnableCodegen, true);
		mFrame->GetMenuBar()->Check(Buttons::EnableCodegen, false);
		mFrame->GetMenuBar()->Enable(Buttons::Run, true);
		mFrame->GetMenuBar()->Enable(Buttons::Info, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Run, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Info, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Save, true);
		mFrame->GetToolBar()->EnableTool(Buttons::SaveAs, true);
		mFrame->Refresh(true);
		mHasUnsavedChanges = false;
		mNeedCodegen = false;
		UpdateTitle();
	}
}

void LanguageController::OnSaveButtonPress()
{
#ifdef _DEBUG
	if (mLanguage->IsInitialized())
	{
		assert(!mLanguage->GetParser().IsParseTaskRunning());
	}
#endif

	if (!mDocument)
	{
		OnSaveAsButtonPress();
		return;
	}

	SerializeLanguage(*mDocument, *mLanguage);
	mHasUnsavedChanges = false;
	UpdateTitle();
}

void LanguageController::OnSaveAsButtonPress()
{
#ifdef _DEBUG
	if (mLanguage->IsInitialized())
	{
		assert(!mLanguage->GetParser().IsParseTaskRunning());
	}
#endif

	wxFileDialog saveFileDialog(mFrame, "Save language to file",
		wxEmptyString, wxEmptyString, "XML files (*.xml)|*.xml",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (saveFileDialog.ShowModal() != wxID_CANCEL)
	{
		SerializeLanguage(saveFileDialog.GetPath(), *mLanguage);
		mDocument = saveFileDialog.GetPath();
		mHasUnsavedChanges = false;
		UpdateTitle();
	}
}

void LanguageController::OnClearButtonPress()
{
	mOutputView->GetTextCtrl()->Clear();
}

void LanguageController::OnLogMessageButtonPress()
{
	if (!mLanguage->IsInitialized())
	{
		throw std::logic_error("parser logger is not initialized");
	}

	if (mFrame->GetMenuBar()->IsChecked(Buttons::LogMessages))
	{
		mLanguage->GetParser().GetLogger()->SetMask(IParserLogger::Action);
	}
	else
	{
		mLanguage->GetParser().GetLogger()->SetMask(IParserLogger::All);
	}
}

void LanguageController::OnEnableCodegenButtonPress()
{
	assert(mLanguage->IsInitialized());
	mNeedCodegen = mFrame->GetMenuBar()->IsChecked(Buttons::EnableCodegen);
}

void LanguageController::OnBuildButtonPress()
{
#ifdef _DEBUG
	if (mLanguage->IsInitialized())
	{
		assert(!mLanguage->GetParser().IsParseTaskRunning());
	}
#endif

	// �������������� ������
	auto builder = std::make_unique<grammarlib::GrammarBuilder>();
	mLanguage->SetGrammar(builder->CreateGrammar(mGrammarView->GetText()));

	// ��������� ������ ������� �� ���������
	mLanguage->GetParser().SetLogger(std::make_unique<TextCtrlParserLogger>(mOutputView->GetTextCtrl()));
	mTerminalsView->SetItems(GetTerminalsArray(mLanguage->GetLexer()));
	mActionsView->SetItems(GetActionsArray(mLanguage->GetParser()));
	mStatesView->SetParserTable(mLanguage->GetParser().GetTable());

	// ��������� �������������
	mFrame->GetMenuBar()->Enable(Buttons::Run, true);
	mFrame->GetMenuBar()->Enable(Buttons::Info, true);
	mFrame->GetToolBar()->EnableTool(Buttons::Run, true);
	mFrame->GetToolBar()->EnableTool(Buttons::Info, true);
	mFrame->GetToolBar()->EnableTool(Buttons::Save, true);
	mFrame->GetToolBar()->EnableTool(Buttons::SaveAs, true);
	mFrame->GetMenuBar()->Enable(Buttons::LogMessages, true);
	mFrame->GetMenuBar()->Check(Buttons::LogMessages, false);
	mFrame->GetMenuBar()->Enable(Buttons::EnableCodegen, true);
	mFrame->GetMenuBar()->Check(Buttons::LogMessages, false);
	mFrame->Refresh(true);

	wxMessageBox("Parser has been successfully built!\n\nElapsed time: " +
		string_utils::TrimTrailingZerosAndPeriod(mLanguage->GetInfo().GetBuildTime().count()) + " seconds.",
		"Build success!", wxICON_INFORMATION);
	mHasUnsavedChanges = true;
	mNeedCodegen = false;
	UpdateTitle();
}

void LanguageController::OnRunButtonPress()
{
	mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_NEW, false);
	mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_OPEN, false);
	mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_SAVE, false);
	mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_SAVEAS, false);
	mFrame->GetMenuBar()->GetMenu(Menubar::Parser)->Enable(Buttons::Build, false);
	mFrame->GetMenuBar()->GetMenu(Menubar::Parser)->Enable(Buttons::Run, false);
	mFrame->GetMenuBar()->GetMenu(Menubar::Parser)->Enable(Buttons::Cancel, true);
	mFrame->GetToolBar()->EnableTool(Buttons::New, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Open, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Save, false);
	mFrame->GetToolBar()->EnableTool(Buttons::SaveAs, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Build, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Run, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Cancel, true);

	std::thread task([this]() {
		assert(mLanguage->IsInitialized());
		IParser<ParseResults>& parser = mLanguage->GetParser();
		IParserLogger* logger = parser.GetLogger();
		assert(logger);

		logger->Log("[" + time_utils::GetCurrentTimeAsString() + "] Parsing started...\n");
		const auto nowTime = std::chrono::steady_clock::now();
		const ParseResults results = parser.Parse(mEditorView->GetText().ToStdString());

		if (mClosing)
		{
			std::cout << "closing while thread works" << std::endl;
			return;
		}

		const auto endTime = std::chrono::steady_clock::now();
		const auto elapsedTime = std::chrono::duration<double>(endTime - nowTime);

		// If task was cancelled, just log it and don't do anything else
		if (results.isCancelled)
		{
			logger->Log("Parsing was cancelled... Elapsed time: "s +
				string_utils::TrimTrailingZerosAndPeriod(elapsedTime.count()) + " seconds.\n"s);
			logger->Log("=========================\n");
			return;
		}

		logger->Log((results.success ? "Successfully parsed!" : "Failed to parse...") +
		" Elapsed time: "s + string_utils::TrimTrailingZerosAndPeriod(elapsedTime.count()) + " seconds.\n"s);

		bool codeGenerated = false;
		bool astDrawn = false;

		if (results.success && (results.expression || results.statement))
		{
			std::ofstream output("ast.dot");
			size_t index = 0;
			output << "digraph {" << std::endl;

			if (results.expression)
			{
				assert(!results.statement);
				ASTGraphvizExpressionVisualizer visualizer(output, index);
				visualizer.Visualize(*results.expression);
			}
			else if (results.statement)
			{
				assert(!results.expression);
				ASTGraphvizStatementVisualizer visualizer(output, index);
				visualizer.Visualize(*results.statement);
			}

			output << "}" << std::endl;
			output.close();

			if (command_utils::RunCommand(L"dot", L"-T png -o ast.png ast.dot"))
			{
				wxImage img;
				img.LoadFile("ast.png");

				if (img.IsOk())
				{
					mTreeView->SetImage(img);
					logger->Log("-- AST has been drawn! --\n");
					astDrawn = true;
				}
				else
				{
					mTreeView->UnsetImage();
					logger->Log("-- Can't draw AST... --\n");
				}
			}
			else
			{
				mTreeView->UnsetImage();
				logger->Log("-- Install Graphviz package to draw AST... --\n");
			}

			if (mNeedCodegen)
			{
				try
				{
					CodegenContext context;
					Codegen codegen(context, std::make_unique<TextCtrlCodegenLogger>(mOutputView->GetTextCtrl()));

					if (results.expression)
					{
						codegen.Generate(*results.expression);
					}
					else if (results.statement)
					{
						codegen.Generate(*results.statement);
					}

					std::ostringstream strm;
					llvm::Module& llvmModule = context.GetUtils().GetModule();
					llvm::raw_os_ostream os(strm);
					llvmModule.print(os, nullptr);

					logger->Log("---------------------------------\n");
					logger->Log("LLVM code has been generated:\n");
					logger->Log(strm.str());
					codeGenerated = true;
				}
				catch (const std::exception& ex)
				{
					logger->Log("Can't generate LLVM code, reason: " + std::string(ex.what()) + ".\n");
				}
			}
		}
		else
		{
			mTreeView->UnsetImage();
		}

		mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_NEW, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_OPEN, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_SAVE, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_SAVEAS, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::Parser)->Enable(Buttons::Build, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::Parser)->Enable(Buttons::Run, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::Parser)->Enable(Buttons::Cancel, false);
		mFrame->GetToolBar()->EnableTool(Buttons::New, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Open, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Save, true);
		mFrame->GetToolBar()->EnableTool(Buttons::SaveAs, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Build, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Run, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Cancel, false);

		logger->Log("=========================\n");
		if (results.success)
		{
			std::string message = "Text has been succesfully parsed!\n\n";
			message += (codeGenerated ? "LLVM code has been generated!\n"s : "");
			message += (astDrawn ? "AST has been drawn!\n\n"s : ""s);
			message += "Elapsed time: "s + string_utils::TrimTrailingZerosAndPeriod(elapsedTime.count()) + " seconds.";
			wxMessageBox(message, "Success!", wxICON_INFORMATION);
		}
		else
		{
			std::string message = "Text doesn't match your grammar...\n\n";
			message += "Error reason: " + results.error + ".\n\n";
			message += "Elapsed time: "s + string_utils::TrimTrailingZerosAndPeriod(elapsedTime.count()) + " seconds.";
			wxMessageBox(message, "Failure...", wxICON_WARNING);
		}
	});

	task.detach();
}

void LanguageController::OnCancelButtonPress()
{
	assert(mLanguage->IsInitialized());
	if (mLanguage->GetParser().IsParseTaskRunning())
	{
		mLanguage->GetParser().CancelParseTask();
		mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_NEW, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_OPEN, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_SAVE, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::File)->Enable(wxID_SAVEAS, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::Parser)->Enable(Buttons::Build, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::Parser)->Enable(Buttons::Run, true);
		mFrame->GetMenuBar()->GetMenu(Menubar::Parser)->Enable(Buttons::Cancel, false);
		mFrame->GetToolBar()->EnableTool(Buttons::New, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Open, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Save, true);
		mFrame->GetToolBar()->EnableTool(Buttons::SaveAs, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Build, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Run, true);
		mFrame->GetToolBar()->EnableTool(Buttons::Cancel, false);
	}
}

void LanguageController::OnInfoButtonPress()
{
	assert(mLanguage->IsInitialized());
	LanguageInfoDialog dialog(mFrame, mLanguage->GetInfo());
	dialog.ShowModal();
}

void LanguageController::OnUpButtonPress()
{
	if (mLanguage->IsInitialized())
	{
		auto& parser = mLanguage->GetParser();
		if (parser.IsParseTaskRunning())
		{
			wxMessageBox("Can't edit items while parser is running!", "Can't edit...", wxICON_WARNING);
			return;
		}
	}

	assert(mTerminalsView->HasSelection() || mActionsView->HasSelection());
	if (mTerminalsView->HasSelection() && mTerminalsView->MoveSelectionUp())
	{
		SwapTerminalPositions(mTerminalsView->GetSelection(), mTerminalsView->GetSelection() + 1);
	}
	else if (mActionsView->HasSelection() && mActionsView->MoveSelectionUp())
	{
		SwapActionPositions(mActionsView->GetSelection(), mActionsView->GetSelection() + 1);
	}
}

void LanguageController::OnDownButtonPress()
{
	if (mLanguage->IsInitialized())
	{
		auto& parser = mLanguage->GetParser();
		if (parser.IsParseTaskRunning())
		{
			wxMessageBox("Can't edit items while parser is running!", "Can't edit...", wxICON_WARNING);
			return;
		}
	}

	assert(mTerminalsView->HasSelection() || mActionsView->HasSelection());
	if (mTerminalsView->HasSelection() && mTerminalsView->MoveSelectionDown())
	{
		SwapTerminalPositions(mTerminalsView->GetSelection(), mTerminalsView->GetSelection() - 1);
	}
	else if (mActionsView->HasSelection() && mActionsView->MoveSelectionDown())
	{
		SwapActionPositions(mActionsView->GetSelection(), mActionsView->GetSelection() - 1);
	}
}

void LanguageController::OnEditButtonPress()
{
	if (mLanguage->IsInitialized())
	{
		auto& parser = mLanguage->GetParser();
		if (parser.IsParseTaskRunning())
		{
			wxMessageBox("Can't edit items while parser is running!", "Can't edit...", wxICON_WARNING);
			return;
		}
	}

	assert(mTerminalsView->HasSelection() || mActionsView->HasSelection());
	if (mTerminalsView->HasSelection())
	{
		OnTerminalEdit(mTerminalsView->GetSelection());
	}
	else if (mActionsView->HasSelection())
	{
		OnActionEdit(mActionsView->GetSelection());
	}
}

void LanguageController::OnTerminalsViewDeselection()
{
	mFrame->GetMenuBar()->Enable(Buttons::Up, false);
	mFrame->GetMenuBar()->Enable(Buttons::Down, false);
	mFrame->GetMenuBar()->Enable(Buttons::Edit, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Up, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Down, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Edit, false);
	mActionsView->DeselectAll();
}

void LanguageController::OnActionsViewDeselection()
{
	mFrame->GetMenuBar()->Enable(Buttons::Up, false);
	mFrame->GetMenuBar()->Enable(Buttons::Down, false);
	mFrame->GetMenuBar()->Enable(Buttons::Edit, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Up, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Down, false);
	mFrame->GetToolBar()->EnableTool(Buttons::Edit, false);
	mTerminalsView->DeselectAll();
}

void LanguageController::OnTerminalSelection(int)
{
	mActionsView->DeselectAll();
	mFrame->GetMenuBar()->Enable(Buttons::Up, true);
	mFrame->GetMenuBar()->Enable(Buttons::Down, true);
	mFrame->GetMenuBar()->Enable(Buttons::Edit, true);
	mFrame->GetToolBar()->EnableTool(Buttons::Up, true);
	mFrame->GetToolBar()->EnableTool(Buttons::Down, true);
	mFrame->GetToolBar()->EnableTool(Buttons::Edit, true);
}

void LanguageController::OnActionSelection(int)
{
	mTerminalsView->DeselectAll();
	mFrame->GetMenuBar()->Enable(Buttons::Up, true);
	mFrame->GetMenuBar()->Enable(Buttons::Down, true);
	mFrame->GetMenuBar()->Enable(Buttons::Edit, true);
	mFrame->GetToolBar()->EnableTool(Buttons::Up, true);
	mFrame->GetToolBar()->EnableTool(Buttons::Down, true);
	mFrame->GetToolBar()->EnableTool(Buttons::Edit, true);
}

void LanguageController::OnTerminalEdit(int index)
{
	assert(mLanguage->IsInitialized());
	assert(index < mLanguage->GetLexer().GetPatternsCount());

	if (mLanguage->IsInitialized())
	{
		auto& parser = mLanguage->GetParser();
		if (parser.IsParseTaskRunning())
		{
			wxMessageBox("Can't edit items while parser is running!", "Can't edit...", wxICON_WARNING);
			return;
		}
	}

	TokenPattern& pattern = mLanguage->GetLexer().GetPattern(index);
	if (pattern.IsEnding())
	{
		using namespace std::string_literals;
		const wxString cTitle = wxT("Information about terminal '" + pattern.GetName() + "'");
		const wxString cMessage = "'"s + pattern.GetName() +
			"' is grammar's ending terminal - you cannot edit it."s;
		wxMessageBox(cMessage, cTitle, wxOK | wxICON_WARNING);
		return;
	}

	TerminalEditDialog dialog(mFrame, pattern);
	if (dialog.ShowModal() == wxID_OK)
	{
		mTerminalsView->SetItemValue(index, pattern.GetOrigin());
		mHasUnsavedChanges = true;
		UpdateTitle();
	}

	OnTerminalSelection(index);
}

void LanguageController::OnActionEdit(int index)
{
	assert(mLanguage->IsInitialized());
	assert(index < mLanguage->GetParser().GetActionsCount());

	if (mLanguage->IsInitialized())
	{
		auto& parser = mLanguage->GetParser();
		if (parser.IsParseTaskRunning())
		{
			wxMessageBox("Can't edit items while parser is running!", "Can't edit...", wxICON_WARNING);
			return;
		}
	}

	IAction& action = mLanguage->GetParser().GetAction(index);
	ActionEditDialog dialog(mFrame, action);

	if (dialog.ShowModal() == wxID_OK)
	{
		const ActionType newActionType = dialog.GetActionTypeSelection();
		const wxString newMessage = dialog.GetActionMessage();

		action.SetType(newActionType);
		action.SetMessage(newMessage);

		mActionsView->SetItemValue(index, ToPrettyString(action.GetType()));
		mHasUnsavedChanges = true;
		UpdateTitle();
	}

	OnActionSelection(index);
}

void LanguageController::OnGrammarTextCtrlUpdateUI(int line, int col, int ch)
{
	UpdateStatusBarCursorInfo(line, col, ch);
}

void LanguageController::OnEditorTextCtrlUpdateUI(int line, int col, int ch)
{
	UpdateStatusBarCursorInfo(line, col, ch);
}

void LanguageController::OnGrammarTextFocusChange(bool focus)
{
	UpdateStatusBarOnTextFocusChange(*mGrammarView, "Grammar", focus);
}

void LanguageController::OnEditorTextFocusChange(bool focus)
{
	UpdateStatusBarOnTextFocusChange(*mEditorView, "Editor", focus);
}
