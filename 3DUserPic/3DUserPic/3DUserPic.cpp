// 3DUserPic.cpp : main source file for 3DUserPic.exe
//

#include "stdafx.h"

#include "resource.h"

#include "MainDlg.h"

CAppModule _Module;

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow)
{
	HRESULT hRes = ::CoInitialize(NULL);
// If you are running on NT 4.0 or higher you can use the following call instead to 
// make the EXE free threaded. This means that calls come in on a random RPC thread.
//	HRESULT hRes = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
	ATLASSERT(SUCCEEDED(hRes));

	// this resolves ATL window thunking problem when Microsoft Layer for Unicode (MSLU) is used
	::DefWindowProc(NULL, 0, 0, 0L);

	AtlInitCommonControls(ICC_BAR_CLASSES);	// add flags to support other controls

	hRes = _Module.Init(NULL, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	CMainDlg::WM_AVE_ACTION = 
        ::RegisterWindowMessage(_T("AvePleaseDoThisForMeOkay3dUser"));

	HANDLE mutex = CreateMutex(NULL, TRUE, L"Ave3dUserPicMutexForRunningOnlyOneApp");
	if(GetLastError() == ERROR_ALREADY_EXISTS)
	{
		SendMessage(HWND_BROADCAST, CMainDlg::WM_AVE_ACTION, 3, 0);
		CloseHandle(mutex);
		return 0;
	}

	if(wcsstr(lpstrCmdLine, L"-show") != 0)
	{
		SendMessage(HWND_BROADCAST, CMainDlg::WM_AVE_ACTION, 3, 0);
		return 0;
	}

	int nRet = 0;
	// BLOCK: Run application
	CMainDlg dlgMain;
	dlgMain.Create(NULL);
	if(wcsstr(lpstrCmdLine, L"-show") != 0)
		dlgMain.ShowWindow(nCmdShow);
	else
	{
		BOOL handled=FALSE;
		dlgMain.OnBnClickedStart(0,0, 0, handled);
	}
		//nRet = dlgMain.DoModal();

	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	nRet = theLoop.Run();

	_Module.RemoveMessageLoop();

	CloseHandle(mutex);

	if(dlgMain.StopHook != NULL)
		dlgMain.StopHook();

	_Module.Term();
	::CoUninitialize();

	return nRet;
}
