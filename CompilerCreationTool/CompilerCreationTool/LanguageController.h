#pragma once
#include "Language.h"
#include "MainFrame.h"

class LanguageController
{
public:
	LanguageController(Language* language, MainFrame* frame);

private:
	void OnLanguageBuildButtonPress();
	void OnParserRunButtonPress();
	void OnLanguageInfoButtonPress();

	void OnTerminalsViewFocus();
	void OnActionsViewFocus();

	void OnTerminalPositionChange(int oldPos, int newPos);
	void OnActionPositionChange(int oldPos, int newPos);

	void OnTerminalSelection(int selection);
	void OnActionSelection(int selection);

	void OnTerminalEdit(int index);
	void OnActionEdit(int index);

	void OnGrammarTextCtrlUpdateUI(int line, int col, int ch);
	void OnEditorTextCtrlUpdateUI(int line, int col, int ch);

private:
	Language* m_language;
	MainFrame* m_frame;

	GrammarView* m_grammarView;
	StatesView* m_statesView;
	EditorView* m_editorView;
	TreeView* m_treeView;

	EntitiesListboxView* m_terminalsView;
	EntitiesListboxView* m_actionsView;
	OutputView* m_outputView;

	std::vector<SignalScopedConnection> m_connections;
};
