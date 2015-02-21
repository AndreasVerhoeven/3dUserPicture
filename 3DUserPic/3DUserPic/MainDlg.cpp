// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include "MainDlg.h"

UINT CMainDlg::WM_AVE_ACTION = 0;

void GetFilePath(WCHAR* buf, const WCHAR* name, BOOL ignoreSkin)
{

	GetModuleFileName(NULL, buf, MAX_PATH);
	PathRemoveFileSpec(buf);
	PathAppendW(buf, name);
}

LRESULT CMainDlg::OnAveAction(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	if(3 == wParam)
		ShowWindow(SW_SHOW);

	return 0;
}

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// center the dialog on the screen
	CenterWindow();

	// set icons
	HICON hIcon = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME), 
		IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
	SetIcon(hIcon, TRUE);
	HICON hIconSmall = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME), 
		IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
	SetIcon(hIconSmall, FALSE);

	StartHook = NULL;
	StopHook = NULL;
	IsHookRunning = NULL;


	WCHAR hookPath[MAX_PATH] = {0};
	GetFilePath(hookPath, L"starthook.dll", TRUE);
	//MessageBox(hookPath);
	hMod = LoadLibrary(hookPath);
	if(hMod != NULL)
	{
		StartHook     = (PStartHook)GetProcAddress(hMod, "StartHook");
		StopHook      = (PStopHook)GetProcAddress(hMod, "StopHook");
		IsHookRunning = (PIsHookRunning)GetProcAddress(hMod, "IsHookRunning");
	}

	BOOL isRunning = IsHookRunning && IsHookRunning();
	::EnableWindow(GetDlgItem(IDC_START),!isRunning);
	::EnableWindow(GetDlgItem(IDC_STOP),isRunning);

	return TRUE;
}

LRESULT CMainDlg::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CSimpleDialog<IDD_ABOUTBOX, FALSE> dlg;
	dlg.DoModal();
	return 0;
}

LRESULT CMainDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	// TODO: Add validation code 
	EndDialog(wID);
	return 0;
}

LRESULT CMainDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	EndDialog(wID);
	return 0;
}

LRESULT CMainDlg::OnBnClickedStart(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if(NULL == hMod)
	{
		MessageBox(_T("could not load the hook module"), NULL, MB_ICONERROR);
		return 0;
	}

	StartHook(hMod, m_hWnd);
	BOOL isRunning = IsHookRunning();
	::EnableWindow(GetDlgItem(IDC_START),!isRunning);
	::EnableWindow(GetDlgItem(IDC_STOP),isRunning);

	return 0;
}

LRESULT CMainDlg::OnBnClickedStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if(NULL == hMod)
	{
		MessageBox(_T("could not load the hook module"), NULL, MB_ICONERROR);
		return 0;
	}

	StopHook();
	BOOL isRunning = IsHookRunning();
	::EnableWindow(GetDlgItem(IDC_START),!isRunning);
	::EnableWindow(GetDlgItem(IDC_STOP),isRunning);

	return 0;
}

LRESULT CMainDlg::OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	ShowWindow(SW_HIDE);
	return 0;
}

LRESULT CMainDlg::OnBnClickedQuit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	PostQuitMessage(0);

	return 0;
}
