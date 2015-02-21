// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#pragma once

#define UNICODE

// Change these values to use different versions
#define WINVER		0x0600
#define _WIN32_WINNT	0x0600
#define _WIN32_IE	0x0600
#define _RICHEDIT_VER	0x0100
#include <atlbase.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>

#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>

#if defined _M_IX86
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif


#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

/*
#ifndef XP_COMPAT
#include <dwmapi.h> 
#pragma comment(lib, "dwmapi.lib")
#endif*/

#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

#include <queue>
#include <math.h>

#include <psapi.h>
#pragma comment(lib, "psapi.lib")

#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include <shlobj.h>