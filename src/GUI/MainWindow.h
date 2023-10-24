#pragma once

#include <QMainWindow>
#include <QTabWidget>

#include "MainTab.h"
#include "ConsoleTab.h"
#include "SessionTab.h"
#include "RoomListTab.h"

namespace Ui
{
	class MainWindow;
}

class CMainWindow : public QMainWindow
{
    Q_OBJECT

public:
	CMainWindow();
	~CMainWindow();
	
	CMainTab* GetMainTab();
	CConsoleTab* GetConsoleTab();

protected:
	void closeEvent(QCloseEvent* event);

private:
    Ui::MainWindow* m_pUI;
	
	// tabs
	CMainTab* m_pMainTab;
	CConsoleTab* m_pConsoleTab;
	CSessionTab* m_pSessionTab;
	CRoomListTab* m_pRoomListTab;
};