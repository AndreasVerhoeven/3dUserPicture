// TrayHook.cpp : Defines the entry point for the DLL application.

/// This module will be a DLL that injects itself into explorer.exe and
/// hooks into the startmenu's user photo window, which it then tries
/// to "replace" with a 3D rotated one.


//////////////////////////////////////////////////////////////////////////////
//
//		copyright (c) Andreas Verhoeven <averhoev.2@hccnet.nl>, 2007.
//		
//		LICENSE:
//		You are free to use this code in whatever you like, however you like.
//
//	
//
////love you nadia////////////////////////////////////////////////////////////

#include "stdafx.h"


#ifdef _MANAGED
#pragma managed(push, off)
#endif


// We're lazy and we are using a premade flipanimation class I wrote 
// one day for another application (avedesk). It has some extra
// features we don't use, but it will do its job just fine.
#include "flipanimation.h"
#include "flipanimation.cpp"

// data shared between all instances of this DLL
#pragma data_seg(".AVESTARTHOOK")
HWND userPic = NULL;		
HMODULE hMod = NULL;		// this DLL's module handle
HHOOK hook = NULL;			// the hook's handle
HWND owner = NULL;			// the handle of the owner window
BOOL hasSubclassed = FALSE;	// TRUE iff we have subclassed the window
UINT unsubclassMsg = 0;		// we register a message that is broadcasted when we need to unsubclass.
HWND rotatingPic = NULL;	// the handle of our 3d rotating picture window.
WCHAR modPath[MAX_PATH] = {0};// the path of this module. Needed for something I guess.
RECT rcUser = {0};			// some rectangle
SIZE rotatingSize = {0};	// some size
FILETIME userPhotoLastModTime = {0};	// time of last modification of the user photo. Used
										// to check if we need to reload the photo.

BOOL hookIsStopping = FALSE;		// to not reent the StopHook() function.
BOOL isOnListView	= FALSE;		// true iff on listview

int lvMouseOverItem = -1;				// the current mouseover item for any listview or treeview


BOOL hasSubclassedSysListView = FALSE;	// true iff we have subclassed the left syslistview
BOOL hasSubclassedTree = FALSE;			// true iff we have subclassed the tree view
BOOL hasSubclassedRightListView = FALSE;// true iff we have subclassed the right listview

HWND treePane = NULL;				// window handle to treeview
HWND rightListView = NULL;			// window handle to right listview
HWND sysListView = NULL;			// main syslistview window

BOOL timerRunning = FALSE;			// true if animation timer is running

SIZE previewIconSize = {80,80};		// preview icon size
BOOL scaleIconsUp = FALSE;			// iff true, icons are scaled up always to match previewIconSize
BOOL scaleIconsDownPretty = TRUE;	// iff true, we scale icons back from the 256x256 icons
BOOL iconsHaveDropShadow = FALSE;	// iff true, we attach a dropshadow to icons
// 0 = jumbo
// 1 = extralarge
// 2 = large
INT  systemIconListToUse = 0;		// which system imagelist to use

BOOL doCheckPixels = TRUE;			// true if we need to check the pixels of large icons
BOOL doNotScaleSmallIconsUp=1;		// iff true, we do not scale small (48px) icons up

HMODULE thisHMod = NULL;

HWND shutDown = NULL;
BOOL hasSubclassedShutDown = FALSE;

HTREEITEM mouseOverItem = NULL;
HTREEITEM prevSelItem = NULL;

int animSpeed = 10;					// the animation speed

int zStart=90;
int xStart=90;

int zLength=90;
int xLength=100;

int zAdd=0;
int xAdd=0;

int zStationary=0;
int xStationary=0;

int rotationLength = 60;
int rotationOffset = 50;
int rotationSpeed = 10;

BOOL posOverride = FALSE;
int posXMargin = 0;
int posYMargin = 0;

BOOL moveShutdown = FALSE;
int shutdownXMargin=0;
int shutdownYMargin=0;

#pragma data_seg()
#pragma comment(linker, "/section:.AVESTARTHOOK,rws")

// offset to the imagelist from the internal datastructure of the userpane,
// we retrieved this by using windbg with debug symbols loaded for explorer
#define EXPLORER_INTERNAL_DATA_USERPANE_IMAGELIST_OFFSET	0x54

// class name of the class we create for our own window.
#define CLSNAME			L"Ave3DUserPicture"
// file where log data will be written to if logging is enabled.
#define LOGFILENAME		L"c:\\avelog.txt"
// message name for unsubclass emssage
#define UNSUBCLASS_MSGNAME	L"AveUnSubclassDesktopPicturePlease"

// start Xrotation
#define START_XROTATION 90.0f
#define START_YROTATION 90.0f


Bitmap* HICONToGDIPlusBitmap(HICON icon)
// pre:  icon is not NULL
// post: A GDI+ bitmap object constructed from icon. If not succesfull, NULL has been returned.
// note: This function fixes the behaviour of GDIPlus::Bitmap::FromHICON() which
//       does not use the per-pixel alpha information of 32 bits icons.
{
	if(NULL == icon)
		return NULL;

	HDC screenDC = GetDC(NULL);
	int screenBitDepth = GetDeviceCaps(screenDC,BITSPIXEL);
	ReleaseDC(NULL,screenDC);

	if(32 == screenBitDepth)
	{
		ICONINFO iconInfo = {0};
		iconInfo.fIcon = TRUE;
		GetIconInfo(icon,&iconInfo);

		int height = iconInfo.xHotspot*2;
		int width = iconInfo.xHotspot*2;

		Bitmap* bmp = new Bitmap(width, height);

		BitmapData data;
		Rect r(0,0,bmp->GetWidth(),bmp->GetHeight());
		bmp->LockBits(&r,ImageLockModeWrite,PixelFormat32bppARGB,&data);
		
		// copies the colour icon, but not yet masked.
		GetBitmapBits(iconInfo.hbmColor,height * width *4,data.Scan0);

		Bitmap* mask( Bitmap::FromHBITMAP(iconInfo.hbmMask,NULL) );
		BitmapData maskData;

		// use 32BPP, because that will align nicely on DWORD boundaries.
		mask->LockBits(&r,ImageLockModeRead,PixelFormat32bppARGB,&maskData);


		BYTE* maskPtr = (BYTE*)maskData.Scan0;
		BYTE* dataPtr = (BYTE*)data.Scan0;

		int numScanLines = maskData.Height;
		int numPixelPerScanLine = maskData.Width;

		// roughly mask the bitmap with its mask
		for(int y=0;y<numScanLines; ++y)
		{
			BYTE* dataScanLineStart = dataPtr;
			BYTE* maskScanLineStart = maskPtr;
			for(int x=0;x<numPixelPerScanLine;++x)
			{
				if(*(maskPtr+1) == 0)
				{
					DWORD* dwPtr = (DWORD*) dataPtr;
					if( ((*dwPtr) & 0xFF000000) == 0)
						*dwPtr = *dwPtr | 0xFF000000;
				}

				dataPtr += sizeof(DWORD);
				maskPtr += sizeof(DWORD);
						
			}

			dataPtr = dataScanLineStart + data.Stride;
			maskPtr = maskScanLineStart + maskData.Stride;
		}

		bmp->UnlockBits(&data);
		mask->UnlockBits(&maskData);


		DeleteObject(iconInfo.hbmColor);
		DeleteObject(iconInfo.hbmMask);

		delete mask;

		return bmp;
		
	}
	else
	{
		// GDI+ is grown-up enough to convert icons with a smaller bitdepth than 32.
		return Bitmap::FromHICON(icon);
	}

}

// This function can be used to log data to a file.
// By default it is disabled. Enable it by commenting out he first return.
BOOL Log(const WCHAR* str, BOOL trunc=FALSE)
{
	// COMMENT OUT to enable logging
	return FALSE;

	if(NULL == str)
		return FALSE;

	DWORD openMode = !trunc ? OPEN_ALWAYS : CREATE_ALWAYS;
	HANDLE hFileLog = CreateFile(LOGFILENAME, GENERIC_WRITE, FILE_SHARE_READ, 0, openMode,FILE_ATTRIBUTE_NORMAL, 0);
	if(INVALID_HANDLE_VALUE == hFileLog)
		return FALSE;

	DWORD len = (DWORD)wcslen(str)*2;
	DWORD numWritten = 0;
	WriteFile(hFileLog, str, len, &numWritten, NULL);
	WriteFile(hFileLog, L"\r\n", 2 * sizeof(WCHAR), &numWritten, NULL);

	CloseHandle(hFileLog);

	return TRUE;

}

// some forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
BOOL CALLBACK StopHook();
void StopRotatingTimer();
void StartRotatingTimer();



// structure used to save data in or 3d-picture window.
struct WinParams
{
	CFlipAnimation* anim;	// an instance of the flipanimation class
	Bitmap* output;			// the output bitmap (user photo)
	Bitmap* userPic;		// userpic
	float rot;				// current rotation
	bool up;				// are we going uuuup?
	bool first;				// first time?
	int alpha;				// alpha value
	bool mouseOver;			// true iff the mouse was over the window

	float xRotation;		// rotation on the x axis
	float zRotation;		// rotation on the z axis

	bool doStartAnim;		// iff true, we do a start anim
};

// some helper functions, ripped from other projects.

/// Gets an absolute filepath for a file located in the same folder
/// as this DLL.
/// @param buf [out] output buffer to be filled. Must be able to contain MAX_PATH characters.
/// @param name [in] the filename to make an absolute path of.
void GetFilePath(WCHAR* buf, const WCHAR* name)
{
	wcscpy_s(buf, MAX_PATH, modPath);
	PathRemoveFileSpec(buf);
	PathAppendW(buf, name);
}

/// Sets a GDI+ Bitmap to a (true) layered window.
/// @param hwnd [in] the window to set the layered window to.
/// @param bmp [out] the bitmap to set to the layered window.
/// @param alpha [in] the alpha level of the window.
/// @param screenPos[in] new screenposition of the window. can be null to stay put.
/// @ return TRUE iff successfull.
BOOL SetLayeredWindow(HWND hwnd, Bitmap* bmp, BYTE alpha=255, POINT* screenPos=NULL)
{
	if(NULL == bmp || NULL == hwnd)
		return FALSE;

	HBITMAP hbmp = NULL;
	bmp->GetHBITMAP(NULL, &hbmp);
	if(NULL == hbmp)
		return FALSE;

	SIZE s = {bmp->GetWidth(), bmp->GetHeight()};
	POINT pt = {0,0};

	HDC dc = CreateCompatibleDC(0);
	HBITMAP oldbmp = (HBITMAP)SelectObject(dc, hbmp);

	BLENDFUNCTION bf = {0};
	bf.BlendOp = AC_SRC_OVER;
	bf.AlphaFormat = AC_SRC_ALPHA;
	bf.BlendFlags = 0;
	bf.SourceConstantAlpha = alpha;
	BOOL res = UpdateLayeredWindow(hwnd, 0, screenPos, &s, dc, &pt, 0, &bf, ULW_ALPHA);

	SelectObject(dc, oldbmp);
	DeleteDC(dc);
	DeleteObject(hbmp);

	return res;
}

// this function registers our window class.
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex = {0};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.lpfnWndProc	= WndProc;
	wcex.hInstance		= hInstance;
	wcex.hCursor		= LoadCursor(NULL, IDC_HAND); // we use a hand cursor!
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszClassName	= CLSNAME;

	return RegisterClassEx(&wcex);
}

// gets the rotating window optimal position
POINT GetRotatingOptimalPosition()
{
	POINT ptDummy = {0};
	if(NULL == rotatingPic)
		return ptDummy;

	WinParams* wp = reinterpret_cast<WinParams*>(::GetProp(rotatingPic, L"params"));
	if(NULL == wp)
		return ptDummy;

	RECT rc = {0};
	GetWindowRect(userPic, &rc);
	POINT mid = {(rc.right - rc.left)/2 + rc.left, (rc.bottom - rc.top)/2 + rc.top };

	if(posOverride)
			{
				HWND startMenu = FindWindowW(L"DV2ControlHost", NULL);
				if(startMenu)
				{
					RECT rc = {0};
					GetWindowRect(startMenu, &rc);
					mid.x = rc.left + posXMargin;
					mid.y = rc.top  + posYMargin;
				}
			}
				

	SIZE s = {0,0};
	if(wp != NULL)
		s = wp->anim->GetOptimalSize();
	mid.x -= s.cx/2;
	mid.y -= s.cy/2;

	return mid;
}

// changes the rotating window animation back to normal, 
// will be called when we are no longer on a listview/treeview with icons
void UnsetRotatingPicOnListView()
{
	if(NULL == rotatingPic)
		return;

	WinParams* wp = reinterpret_cast<WinParams*>(::GetProp(rotatingPic, L"params"));
	if(NULL == wp)
		return;

	if(wp->userPic != wp->output)
	{
		delete wp->output;
		wp->output = wp->userPic;
		if(wp->anim)
			wp->anim->SetTextureBitmaps(wp->output, wp->output);
		wp->xRotation = START_XROTATION;
		wp->zRotation = START_YROTATION;
		wp->doStartAnim = true;
	}
	StartRotatingTimer();
}


// does what it says on the can.
void HBITMAPToGDIPlusBitmap(HBITMAP hBmp, SIZE s, Bitmap* bmp)
{
	BitmapData data;
	Rect r(0,0,bmp->GetWidth(),bmp->GetHeight());
	bmp->LockBits(&r,ImageLockModeWrite,PixelFormat32bppARGB,&data);
	GetBitmapBits(hBmp, 4 * data.Stride * s.cy, (LPVOID)data.Scan0);
	bmp->UnlockBits(&data);
}

// gets a Bitmap* from an icon in the system image list.
Bitmap* GetIconFromSysImageList(int index, SIZE s)
{
	Bitmap* bmpRet = NULL;
	HMODULE shell32 = LoadLibrary(L"shell32");

	BOOL notRetrievedIcon = TRUE;

	HRESULT (CALLBACK *PSHGetImageList)(int iImageList, REFIID riid, void **ppv) = NULL;

	PSHGetImageList = (HRESULT (CALLBACK *)(int, REFIID, void **)) GetProcAddress(shell32, (LPCSTR)727);
	if(PSHGetImageList)
	{
		int listToUse = SHIL_JUMBO;
		if(1 == systemIconListToUse)
			listToUse = SHIL_EXTRALARGE;
		else if(2 == systemIconListToUse)
			listToUse = SHIL_LARGE;

		IImageList* il = NULL;
		PSHGetImageList(listToUse,IID_IImageList, (void**)&il);
		if(il != NULL)
		{
			
			if(notRetrievedIcon)
			{
				if(scaleIconsDownPretty && 0 == systemIconListToUse)
				{
					s.cx = 256;
					s.cy = 256;
				}
				
				// this routine is bugged: places smaller icons in the upleft corner,
				// rather than in the middle => we use DrawEx instead
				//il->GetIcon(index, ILD_NORMAL, &hicon);

				HDC dc = CreateCompatibleDC(0);
				Bitmap* tmpBmp = new Bitmap(s.cx, s.cy);
				HBITMAP hBmp = NULL;
				tmpBmp->GetHBITMAP(NULL, &hBmp);
				HBITMAP oldBmp = (HBITMAP)SelectObject(dc, hBmp);
				IMAGELISTDRAWPARAMS drawp = {0};
				drawp.cbSize = sizeof(drawp);
				drawp.hdcDst = dc;
				drawp.i = index;
				drawp.x = 0;
				drawp.y = 0;
				drawp.cx = s.cx;
				drawp.cy = s.cy;
				drawp.fStyle = ILD_NORMAL |  ILD_IMAGE | ILD_SCALE;
				drawp.crEffect = RGB(0,0,0);
				drawp.fState = ILS_ALPHA;
				if(iconsHaveDropShadow)
					drawp.fState |= ILS_SHADOW;
				drawp.Frame = 255;
				il->Draw(&drawp);

				SelectObject(dc, oldBmp);

				BOOL isScaledWrongly = FALSE;
				if(listToUse == SHIL_JUMBO && doCheckPixels)
				{
					DWORD pix[256*10] = {0};
					GetBitmapBits(hBmp, 256 * 10 * 4, &pix);
					DWORD color = pix[20+256*4];
					int i = 21;
					for(; i < 200; ++i)
					{
						if(pix[i+256*4] != color || color == 0)
							break;
					}

					if(i == 200)
						isScaledWrongly = TRUE;
				}

				DeleteDC(dc);			

				

				HBITMAPToGDIPlusBitmap(hBmp, s, tmpBmp);
				bmpRet = tmpBmp;
				DeleteObject(hBmp);

				if(isScaledWrongly)
				{	
					PSHGetImageList(SHIL_EXTRALARGE,IID_IImageList, (void**)&il);
					if(il != NULL)
					{
						HICON icon = NULL;
						il->GetIcon(index, ILD_NORMAL | ILD_SCALE, &icon);
						if(icon != NULL)
						{
							delete bmpRet;
							bmpRet = HICONToGDIPlusBitmap(icon);
							DestroyIcon(icon);
						}
					}
				}
			}
		}

		//il->Release();
	}
	
	FreeLibrary(shell32);
	
	if(NULL == bmpRet)
		bmpRet = new Bitmap(s.cx, s.cy);

	return bmpRet;
}

// called when the rotating picture should change from the userpicture
// to an icon from a listview/treeview.
// if imgList == 1, we use the system image list instead.
void SetRotatingPicOnListView(HIMAGELIST imgList, int index)
{
	if(NULL == rotatingPic)
		return;

	WinParams* wp = reinterpret_cast<WinParams*>(::GetProp(rotatingPic, L"params"));
	if(NULL == wp)
		return;

	if(NULL == imgList)
	{
		StopRotatingTimer();
		return;
	}
	else
	{
		if(wp->output != wp->userPic)
			delete wp->output;

		SIZE bmps = previewIconSize;
		if((HIMAGELIST)1 == imgList)
			wp->output = GetIconFromSysImageList(index, previewIconSize);
		else
		{
			int cx = 0, cy = 0;
			HICON icon = ImageList_GetIcon(imgList, index, 0);
			if(NULL == icon)
				return;

			wp->output = HICONToGDIPlusBitmap(icon);
			DestroyIcon(icon);
		}

		if(wp->output != NULL && wp->output->GetWidth() > (UINT)previewIconSize.cx)
		{
			Bitmap* bmp = new Bitmap(previewIconSize.cx, previewIconSize.cy);
			Graphics g(bmp);
			g.SetSmoothingMode(SmoothingModeAntiAlias);
			g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
			g.DrawImage(wp->output, Rect(0,0,previewIconSize.cx, previewIconSize.cy),
						0,0,wp->output->GetWidth(),wp->output->GetWidth(), UnitPixel, 0,0,0);

			delete wp->output;
			wp->output = bmp;
		}
		else if(wp->output != NULL && wp->output->GetWidth() < (UINT)previewIconSize.cx && scaleIconsUp)
		{
			if(wp->output->GetWidth() != 48 || !doNotScaleSmallIconsUp)
			{
				Bitmap* bmp = new Bitmap(previewIconSize.cx, previewIconSize.cy);
				Graphics g(bmp);
				g.SetSmoothingMode(SmoothingModeAntiAlias);
				g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
				g.DrawImage(wp->output, Rect(0,0,previewIconSize.cx, previewIconSize.cy),
							0,0,wp->output->GetWidth(),wp->output->GetWidth(), UnitPixel, 0,0,0);

				delete wp->output;
				wp->output = bmp;
			}
		}

		if(wp->anim)
			wp->anim->SetTextureBitmaps(wp->output, wp->output);

		wp->xRotation = START_XROTATION;
		wp->zRotation = START_YROTATION;
		wp->doStartAnim = true;
		StartRotatingTimer();

	}
}

// this is the animation we actually want, however, I just was lazy and experimented a bit:
	//if 0.0f < state <= 0.2f, then sin 0-pi animate to 10°
	//if 0.2f < state <= 0.7f, then sin 0-pi animate to 45°
	//if 0.7f < state <= 1.0f, then sin 0-pi/2 animate back to 15°


// Window procedure for our 3d-photo window.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
	switch(msg)
	{
	case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
		if(1)
		{
			// keep track if we moused over this window.
			WinParams* wp = (WinParams*)GetProp(hwnd, L"params");
			if(wp != NULL)
				wp->mouseOver = true;

				return 0;
		}
		break;
	case WM_MOUSEACTIVATE:
		if(1)
		{
			// because the start menu actually captures mouse input,
			// we can't use click handlers on our window. Thus, we check wheter we
			// are activated by mouse and then simply launch the "users control panel applet".
			WinParams* wp = (WinParams*)GetProp(hwnd, L"params");
			if(wp != NULL && wp->mouseOver)
			{
				ShellExecute(0, L"open", L"control", L"nusrmgr.cpl", 0, SW_SHOW); 
				wp->mouseOver = FALSE;
			}
		}
			return 0;
		break;
		case WM_TIMER:
			if(1 == w)
			{
				// ANIMATION TIME!

				WinParams* wp = (WinParams*)GetProp(hwnd, L"params");
				if(wp != NULL)
				{
					// first do a fade in animation on first display of this window
					// to mask the D3D and userphoto loading times.
					BYTE alpha = 255;
					if(wp->first)
					{
						wp->alpha++;
						float fa = sin( D3DX_PI / 2.0f * float(wp->alpha)/125.0f) * 255.0f;
						alpha = (BYTE)fa;
						if(alpha >= 254)
						{
							alpha = 255;
							wp->first = false;
						}

					}

					// if we have lost the device, just reinit it. It's no problem
					// to not check if we are actually allowed to reinit the device,
					// since our window will only be visible when the startmenu 
					// is visible and the user is not logged out or the window
					// is minimized.
					if(wp->anim->HasLostDevice())
					{
						wp->anim->Uninitialize();
						delete wp->anim;
						wp->anim = new CFlipAnimation(wp->output, wp->output);
						wp->anim->Initialize(hwnd);
						wp->first = true;
						wp->alpha = 0;
						return 0;
					}

					float rotSpeed = float(rotationSpeed) / 10.0f;
					float rotLength = float(rotationLength);
					float rotOffset = float(rotationOffset);
					float rot = 0.0f;

					if(wp->up)
					{
						wp->rot += rotSpeed;
						if(wp->rot >= rotLength)
						{
							wp->rot = rotLength;
							wp->up = false;
						}
					}
					else
					{
						wp->rot -= rotSpeed;
						if(wp->rot <= 0.0f)
						{
							wp->rot = 0.0f;
							wp->up = true;
						}
					}

					float progress = wp->rot / rotLength;
					float sp = sin(D3DX_PI / 2.0f + D3DX_PI * progress);
					sp += 1.0f;
					sp = 2.0f - sp;
					sp /= 2.0f;

					rot = sp * rotLength  - rotOffset;

					float zrot = float(zStationary), xrot = float(xStationary);

					if(wp->doStartAnim)
					{
						wp->zRotation -= float(animSpeed);
						if(wp->zRotation <= 0.0f)
						{
							wp->zRotation = 0.0f;
							wp->xRotation = 0.0f;

							wp->doStartAnim = false;
							wp->up = false;
							

							/*
							float progress = wp->rot / rotLength;
							float sp = sin(D3DX_PI / 2.0f + D3DX_PI * progress);
							sp += 1.0f;
							sp = 2.0f - sp;
							sp /= 2.0f;

							rot = sp * rotLength  - rotOffset;

							0.0 = sp * rotLength - rotOffset;
							rotOffset = sp * rotLength;
							rotOffset / rotLength = sp;
							rotOffset  /rotLength = (2.0 - (sp+1))/2;
							rotOffset / rotLength * 2.0f = 2.0 - (sp + 1);
							rotOffset / rotLength * 2.0f - 2.0f; = -(sp + 1);
							rotOffset / rotLength * 2.0f - 2.0f + 1.0f = -sp;
							-(rotOffset / rotLength * 2.0f - 2.0f + 1.0f) =  sp;
							-(rotOffset / rotLength * 2.0f - 2.0f + 1.0f) = sin(D3DX_PI / 2.0f + D3DX_PI * progress);
							asin(-(rotOffset / rotLength * 2.0f - 2.0f + 1.0f)) = D3DX_PI / 2.0f + D3DX_PI * progress;
							asin(-(rotOffset / rotLength * 2.0f - 2.0f + 1.0f)) - D3DX_PI / 2.0f  = D3DX_PI * progress;
							(asin(-(rotOffset / rotLength * 2.0f - 2.0f + 1.0f)) - D3DX_PI / 2.0f) / D3DX_PI = progress;
							(asin(-(rotOffset / rotLength * 2.0f - 2.0f + 1.0f)) - D3DX_PI / 2.0f) / D3DX_PI =  wp->rot / rotLength;
							(asin(-(rotOffset / rotLength * 2.0f - 2.0f + 1.0f)) - D3DX_PI / 2.0f) / D3DX_PI * rotLength = wp->rot;

							*/


							//wp->rot = (asin(-(rotOffset / rotLength * 2.0f - 2.0f + 1.0f)) - D3DX_PI / 2.0f) / D3DX_PI * rotLength;
							wp->rot = rotationLength / 2.0f;


							rot = 0.0f;
						}
						else
						{
							rot = 0.0f;
							float progress = (90.0f - wp->zRotation) / 90.0f;
							float sp = sin(D3DX_PI / 2.0f * progress);

							zrot = float(zStart) - (sp * float(zLength));
							xrot = float(xStart) - (sp * float(xLength)) + float(xAdd);
							float fa  = sp * 255.0f;
							alpha = (BYTE)fa;
						}
					}

					// flip and rotate the suckah
					Bitmap* flipped = NULL;
					wp->anim->FlipToFront(rot, xrot, zrot, &flipped);

					// if we got an output bitmap from our renderer, display it.
					if(flipped != NULL)
					{
						POINT ptScreen = GetRotatingOptimalPosition();
						SetLayeredWindow(hwnd, flipped, alpha, &ptScreen);
					}

					// don't forget to free it.
					delete flipped;
				}
				
			}
		break;

		case WM_DESTROY:
			if(1)
			{
				// on destroy, free the contents and the structure that
				// holded this windows data.
				WinParams* wp = (WinParams*)GetProp(hwnd, L"params");
				if(wp != NULL)
				{
					wp->anim->Uninitialize();
					delete wp->anim;
					delete wp->output;
					if(wp->output != wp->userPic)
						delete wp->userPic;
					delete wp;
				}
			}
		break;
	}

	return ::DefWindowProc(hwnd, msg, w, l);
}

// this sets up a rotating pic window.
void SetupRotatingPicWindow()
{
	Log(L"entering setup");

	// make sure the class is registered.
	MyRegisterClass(hMod);

	Log(L"registered class");

	// create a layered window with no borders etc...
	HWND hwnd = CreateWindowEx(WS_EX_TOOLWINDOW |WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE, 
	   CLSNAME, L"", WS_POPUP,
      200, 200, 220, 70, NULL, NULL, hMod, NULL);

	if(NULL == hwnd)
		Log(L"error creating window");

	Log(L"created window");

	// find the user photo.
	// The userphoto is on vista always located in %TEMP%/%USERNAME%.bmp. Or, at least,
	// it should be.
	DWORD bufLen = MAX_PATH;
	WCHAR username[MAX_PATH] = {0};
	GetUserName(username, &bufLen);
	wcscat_s(username, MAX_PATH, L".bmp");
	WCHAR userPhotoPath[MAX_PATH] = {0};
	GetTempPath(MAX_PATH, userPhotoPath);
	PathAppend(userPhotoPath, username);

	// read the values.ini holding some primitive skinning data.
	WCHAR valuesPath[MAX_PATH] = {0};
	GetFilePath(valuesPath, L"values.ini");

	posOverride = GetPrivateProfileInt(L"position", L"override", posOverride, valuesPath);
	posXMargin = GetPrivateProfileInt(L"position", L"xmargin", posXMargin, valuesPath);
	posYMargin = GetPrivateProfileInt(L"position", L"ymargin", posYMargin, valuesPath);

	moveShutdown = GetPrivateProfileInt(L"shutdown", L"move", moveShutdown, valuesPath);
	shutdownXMargin = GetPrivateProfileInt(L"shutdown", L"xmargin", shutdownXMargin, valuesPath);
	shutdownYMargin = GetPrivateProfileInt(L"shutdown", L"ymargin", shutdownYMargin, valuesPath);

	int frameX = GetPrivateProfileInt(L"frame", L"x", 0, valuesPath);
	int frameY = GetPrivateProfileInt(L"frame", L"y", 0, valuesPath);
	int frameW = GetPrivateProfileInt(L"frame", L"width", 96, valuesPath);
	int frameH = GetPrivateProfileInt(L"frame", L"height", 96, valuesPath);

	rotatingSize.cx = frameW;
	rotatingSize.cy = frameH;

	int photoX = GetPrivateProfileInt(L"photo", L"x", 16, valuesPath);
	int photoY = GetPrivateProfileInt(L"photo", L"y", 16, valuesPath);
	int photoW = GetPrivateProfileInt(L"photo", L"width", 64, valuesPath);
	int photoH = GetPrivateProfileInt(L"photo", L"height", 64, valuesPath);

	previewIconSize.cx = GetPrivateProfileInt(L"rendering", L"previewIconSize", previewIconSize.cx, valuesPath);
	previewIconSize.cy = previewIconSize.cx;
	scaleIconsUp = GetPrivateProfileInt(L"rendering", L"scaleIconsUp", scaleIconsUp, valuesPath);
	scaleIconsDownPretty = GetPrivateProfileInt(L"rendering", L"scaleIconsDownPretty", scaleIconsDownPretty, valuesPath);
	iconsHaveDropShadow = GetPrivateProfileInt(L"rendering", L"iconsHaveDropShadow", iconsHaveDropShadow, valuesPath);
	systemIconListToUse = GetPrivateProfileInt(L"rendering", L"systemIconListToUse", systemIconListToUse, valuesPath);
	doCheckPixels = GetPrivateProfileInt(L"rendering", L"usePixelCheckToFilterOutSmallIcons", doCheckPixels, valuesPath);
	doNotScaleSmallIconsUp = GetPrivateProfileInt(L"rendering", L"doNotScaleSmallIconsUp", doNotScaleSmallIconsUp, valuesPath);

	animSpeed = GetPrivateProfileInt(L"animation", L"speed", animSpeed, valuesPath);
	zStart = GetPrivateProfileInt(L"animation", L"zStart", zStart, valuesPath);
	xStart = GetPrivateProfileInt(L"animation", L"xStart", xStart, valuesPath);
	zLength = GetPrivateProfileInt(L"animation", L"zLength", zLength, valuesPath);
	xLength = GetPrivateProfileInt(L"animation", L"xLength", xLength, valuesPath);
	zAdd = GetPrivateProfileInt(L"animation", L"zAdd", zAdd, valuesPath);
	xAdd = GetPrivateProfileInt(L"animation", L"xAdd", xAdd, valuesPath);
	zStationary = GetPrivateProfileInt(L"animation", L"zStationary", zStationary, valuesPath);
	xStationary = GetPrivateProfileInt(L"animation", L"xStationary", xStationary, valuesPath);

	rotationLength = GetPrivateProfileInt(L"animation", L"rotationLength", rotationLength, valuesPath);
	rotationSpeed = GetPrivateProfileInt(L"animation", L"rotationSpeed", rotationSpeed, valuesPath);
	rotationOffset = GetPrivateProfileInt(L"animation", L"rotationOffset", rotationOffset, valuesPath);

	// make sure gdi+ has been started.
	GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR pGdiToken;
	GdiplusStartup(&pGdiToken,&gdiplusStartupInput,NULL); 

	// load the user photo
	Bitmap bmp(userPhotoPath);

	// load the frame bitmap
	WCHAR framePath[MAX_PATH] = {0};
	GetFilePath(framePath, L"frame.png");
	Bitmap frame(framePath);

	if(bmp.GetLastStatus() != Ok)
		Log(L"error bmp");

	// draw the userphoto first, then draw the frame over it,
	// to make the userphoto appear to have a small gloss from the frame.
	Bitmap* output = new Bitmap(frameW,frameH);
	if(output != NULL)
	{
		Graphics g(output);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.SetInterpolationMode(InterpolationModeHighQualityBicubic);

		g.DrawImage(&bmp, Rect(photoX,photoY, photoW, photoH),
						0,0,bmp.GetWidth(), bmp.GetHeight(),  UnitPixel,0,0,0);

		g.DrawImage(&frame, Rect(frameX,frameY, frameW, frameH),
						0,0,frame.GetWidth(), frame.GetHeight(),  UnitPixel,0,0,0);

		Log(L"about to create dx");

		// create a structure holding some data used by the window.
		WinParams* wp = new WinParams;
		wp->output = output;
		wp->userPic = output;
		wp->anim = new CFlipAnimation(output, output);
		wp->anim->Initialize(hwnd);
		wp->rot = float(rotationLength) / 2.0f;
		wp->up = true;
		wp->first = true;
		wp->alpha = 0;
		wp->xRotation = START_XROTATION;
		wp->zRotation = START_YROTATION;

		Log(L"initialized dx");

		// if D3D was for some reason not initialized well,
		// just show the composed userphoto as we rendered it with GDI+ and
		// not as rendered by D3D.
		if(wp != NULL && wp->anim != NULL && !wp->anim->IsInitializedOk())
			SetLayeredWindow(hwnd, output, 255);

		::SetProp(hwnd, L"params", (HANDLE)wp);
	}

	rotatingPic = hwnd;
}

// destroys the 3d user photo window.
void DestroyRotatingPicWindow()
{
	if(IsWindow(rotatingPic))
	{
		DestroyWindow(rotatingPic);
		rotatingPic = NULL;
	}
}

void StartRotatingTimer()
{
	if(!timerRunning)
		SetTimer(rotatingPic, 1, 10, 0);

	timerRunning = TRUE;
}

void StopRotatingTimer()
{
	timerRunning = FALSE;
	::KillTimer(rotatingPic, 1);
}

// Subclass procedure for the userphoto's window.
LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
	if(WM_NCDESTROY == msg)
	{
		if(hasSubclassed)
			hasSubclassed = !RemoveWindowSubclass(userPic, MsgProc, 1);
	}
	// by trial and error, we found out that a WM_ENABLE is the only reliable way
	// to detect if the window needs to be showed.
	if(hwnd == userPic && WM_ENABLE == msg)
	{
		if(wParam)
		{
			// if the userphoto window is enabled, we make OUR window visible.
			Log(L"enabling window");
			// check if the userphoto was changed by comparing the 
			// last loaded file times.
			DWORD bufLen = MAX_PATH;
			WCHAR username[MAX_PATH] = {0};
			GetUserName(username, &bufLen);
			wcscat_s(username, MAX_PATH, L".bmp");
			WCHAR userPhotoPath[MAX_PATH] = {0};
			GetTempPath(MAX_PATH, userPhotoPath);
			PathAppend(userPhotoPath, username);

			BOOL renew = FALSE;
			HANDLE h = CreateFile(userPhotoPath, 0, FILE_SHARE_READ, 0, OPEN_EXISTING,0, 0);
			if(h != INVALID_HANDLE_VALUE)
			{
				FILETIME ft = {0};
				GetFileTime(h, NULL, NULL, &ft);
				renew = CompareFileTime(&ft, &userPhotoLastModTime) > 0;
				userPhotoLastModTime = ft;

				CloseHandle(h);
			}
		
			// if we need to renew the userphoto, just recreate the whole window again.
			// yeah, the lazy way!
			if(renew)
				DestroyRotatingPicWindow();

			// if we don't have a 3d user photo window, set it up.
			if(NULL == rotatingPic)
				SetupRotatingPicWindow();

			WinParams* wp = (WinParams*)GetProp(rotatingPic, L"params");

			// do some position magic.
			RECT rc = {0};
			GetWindowRect(userPic, &rc);
			POINT mid = {(rc.right - rc.left)/2 + rc.left, (rc.bottom - rc.top)/2 + rc.top };
			SIZE s = {0,0};
			if(wp != NULL)
				s = wp->anim->GetOptimalSize();
			mid.x -= s.cx/2;
			mid.y -= s.cy/2;

			Log(L"enabling window 2");

			if(posOverride)
			{
				HWND startMenu = FindWindowW(L"DV2ControlHost", NULL);
				if(startMenu)
				{
					RECT rc = {0};
					GetWindowRect(startMenu, &rc);
					mid.x = rc.left + posXMargin;
					mid.y = rc.top  + posYMargin;
				}
			}
					
			if(wp != NULL)
				wp->mouseOver = false;
			SetWindowPos(rotatingPic, HWND_TOPMOST, mid.x, mid.y, 0, 0, SWP_SHOWWINDOW | SWP_NOACTIVATE);
			StartRotatingTimer();
			ShowWindow(userPic, SW_HIDE);
		}
		else if(!wParam)
		{
			// if the userphoto window is hidden again, kill the animation tmer
			// and hide our damn window.
			StopRotatingTimer();
			SetWindowPos(rotatingPic, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE);
		}

		return 0;
	}
	else if(hwnd == userPic && (WM_WINDOWPOSCHANGING == msg || msg == WM_WINDOWPOSCHANGED))
	{
		// easy solution for making sure that the original user photo window will never
		// be shown: on each windowpos changing/changed message, just hide the sucker.
		WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam);
		if(wp != NULL)
		{
			ShowWindow(userPic, SW_HIDE);
		}
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

int ListView_GetSelectedItem(HWND hwnd)
{
	int count = ListView_GetItemCount(hwnd);
	for(int i=0; i < count; ++i)
	{
		if(ListView_GetItemState(hwnd, i, LVIS_SELECTED) & LVIS_SELECTED)
			return i;
	}

	return -1;
}

LRESULT CALLBACK TreeMsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
	if(WM_DESTROY == msg)
	{
		if(hasSubclassedTree)
			hasSubclassedTree = !RemoveWindowSubclass(treePane, TreeMsgProc, 1);
	}
	else if(WM_MOUSEMOVE == msg)
	{
		POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		TVHITTESTINFO hi = {0};
		hi.pt = pt; 

		mouseOverItem = TreeView_HitTest(hwnd, &hi);
		if(mouseOverItem != prevSelItem)
		{
			KillTimer(hwnd, 1);
			SetTimer(hwnd, 1, 10, 0);
		}
	}
	else if(WM_MOUSELEAVE == msg)
	{
			lvMouseOverItem = -1;
			isOnListView = FALSE;
			UnsetRotatingPicOnListView();
	}
	else if((WM_TIMER == msg && 1 == wParam) || WM_KEYUP == msg)
	{
		KillTimer(hwnd, 1);
		
		HTREEITEM hotItem = TreeView_GetSelection(hwnd);//ListView_GetHotItem(hwnd);
		if(mouseOverItem != hotItem && mouseOverItem != NULL)
			mouseOverItem = hotItem;

		if(hotItem != prevSelItem)
		{
			
			prevSelItem = hotItem;
			TVITEM lvi = {0};
			lvi.hItem = hotItem;
			lvi.mask = TVIF_IMAGE | TVIF_PARAM;
			TreeView_GetItem(hwnd, &lvi);

			if(lvi.iImage != -1 && hotItem != NULL )
			{
				isOnListView = TRUE;
				SetRotatingPicOnListView((HIMAGELIST)1, lvi.iImage);
			}
			else
			{
				lvMouseOverItem = -1;
				isOnListView = FALSE;
				UnsetRotatingPicOnListView();
			}
		}
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

BOOL sdRecurse = FALSE;

LRESULT CALLBACK ShutDownProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
	if(msg == WM_WINDOWPOSCHANGING)
	{
		WINDOWPOS* wp = (WINDOWPOS*)lParam;
		if(wp != NULL)
		{
			wp->x = shutdownXMargin;
			wp->y = shutdownYMargin;
			wp->flags &= ~SWP_NOZORDER;
			wp->hwnd = HWND_TOP;

			if(!sdRecurse)
			{
				RECT rc = {0};
				GetClientRect(hwnd, &rc);
				sdRecurse = TRUE;
				MoveWindow(hwnd, wp->x, wp->y, rc.right - rc.left, rc.bottom - rc.top, TRUE);
				sdRecurse = FALSE;
			}

			PostMessage(hwnd, WM_TIMER, 1, 0);
			//SetTimer(hwnd, 1, 1, NULL);

			HWND sm = FindWindow(L"DV2ControlHost", NULL);
			RECT rc = {0};
			GetClientRect(sm, &rc);
			InvalidateRect(sm, &rc, TRUE);

			GetClientRect(hwnd, &rc);
			InvalidateRect(hwnd, &rc, TRUE);
		}
	}
	else if(msg == WM_TIMER && wParam == 1)
	{
		KillTimer(hwnd, 1);
		HWND sm = FindWindow(L"DV2ControlHost", NULL);
		RECT rc = {0};
		GetClientRect(sm, &rc);
		InvalidateRect(sm, &rc, TRUE);

		GetClientRect(hwnd, &rc);
		InvalidateRect(hwnd, &rc, TRUE);
		//UpdateWindow(sm);
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}



LRESULT CALLBACK SysListViewMsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
		if(WM_NCDESTROY == msg)
		{
			if(hasSubclassedSysListView && hwnd == sysListView)
				hasSubclassedSysListView = !RemoveWindowSubclass(sysListView, SysListViewMsgProc, 1);

			if(hasSubclassedRightListView && hwnd == rightListView)
				hasSubclassedRightListView = !RemoveWindowSubclass(rightListView, SysListViewMsgProc, 1);
		}
		else if(WM_MOUSEMOVE == msg)
		{
			int hotItem = ListView_GetSelectedItem(hwnd);
			if(hotItem != lvMouseOverItem)
			{
				KillTimer(hwnd, 1);
				SetTimer(hwnd, 1, 10, 0);
			}
		}
		else if(WM_MOUSELEAVE == msg)
		{
			lvMouseOverItem = -1;
			isOnListView = FALSE;
			UnsetRotatingPicOnListView();
		}
		else if((WM_TIMER == msg && 1 == wParam) || WM_KEYUP == msg)
		{
			KillTimer(hwnd, 1);
			//NMHDR* hdr = (NMHDR*)lParam;
			//if(hdr)
			{
				//if(hdr->code == NM_HOVER)
				{
					//int hotItem = ListView_GetSelItem(
					int hotItem = ListView_GetSelectedItem(hwnd);//ListView_GetHotItem(hwnd);
					if(hotItem != lvMouseOverItem)
					{
						
							lvMouseOverItem = hotItem;
							LVITEM lvi = {0};
							lvi.iItem = hotItem;
							lvi.mask = LVIF_IMAGE | LVIF_PARAM;
							ListView_GetItem(hwnd, &lvi);

							HIMAGELIST imgList = ListView_GetImageList(hwnd, LVSIL_NORMAL);
							if(hwnd == sysListView)
								imgList = (HIMAGELIST)1;
							else
							{
								// if we are on the right pane, we need to hack ourself
								// to the correct imagelist.
								// From a windbg session using explorer's debug symbols,
								// we learned that the CUserPane internal object
								// stores the imagelist for when it's doing the userpic->icon
								// animation.
								// This object is associated with a window using a thunking procedure.
								// The object is stored in the window's windata:
								// the imagelist is some bytes offsetted from the start.
								LONG l = GetWindowLong(userPic, 0x0FFFFFFEB);
								l+=EXPLORER_INTERNAL_DATA_USERPANE_IMAGELIST_OFFSET;
								if(l != NULL)
									imgList = *((HIMAGELIST*)l);
							}

							if(lvi.iImage != -1 && lvMouseOverItem != -1 )
							{
								isOnListView = TRUE;
								SetRotatingPicOnListView(imgList, lvi.iImage);
							}
							else
							{
								lvMouseOverItem = -1;
								isOnListView = FALSE;
								UnsetRotatingPicOnListView();
							}
					}
				}
			}
		}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}


LRESULT CALLBACK SysListViewRightMsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR a , DWORD_PTR b)
{
	return SysListViewMsgProc(hwnd, msg, wParam, lParam, a, b);
}


BOOL IsTreePane(HWND hwnd)
{
	const DWORD MAX_CLASS_NAME = 255;
	const WCHAR* parentName = L"NamespaceTreeControl";
	const WCHAR* treeListName = L"SysTreeView32";

	WCHAR className[MAX_CLASS_NAME] = {0};
	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, treeListName) != 0)
		return FALSE;

	className[0] = L'\0';
	hwnd = ::GetParent(hwnd);

	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, parentName) != 0)
		return FALSE;

	return TRUE;
}

BOOL IsSysListViewOfStartMenu(HWND hwnd)
{
	const DWORD MAX_CLASS_NAME = 255;
	const WCHAR* grandParentName = L"Desktop Open Pane Host";
	const WCHAR* parentName = L"DesktopSFTBarHost";
	const WCHAR* sysListName = L"SysListView32";
	WCHAR className[MAX_CLASS_NAME] = {0};
	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, sysListName) != 0)
		return FALSE;
	
	className[0] = L'\0';
	hwnd = ::GetParent(hwnd);

	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, parentName) != 0)
		return FALSE;

	className[0] = L'\0';
	hwnd = ::GetParent(hwnd);

	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, grandParentName) != 0)
		return FALSE;

	return TRUE;
}

BOOL IsListViewRightPane(HWND hwnd)
{
	const DWORD MAX_CLASS_NAME = 255;
	const WCHAR* grandParentName = L"Desktop Open Pane Host";//L"DV2ControlHost";
	const WCHAR* parentName = L"DesktopSFTBarHost";
	const WCHAR* sysListName = L"SysListView32";

	WCHAR className[MAX_CLASS_NAME] = {0};
	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, sysListName) != 0)
		return FALSE;

	className[0] = L'\0';
	hwnd = ::GetParent(hwnd);

	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, parentName) != 0)
		return FALSE;

	className[0] = L'\0';
	hwnd = ::GetParent(hwnd);

	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, grandParentName) == 0) // the grand parent should not be the desktop open pane host
		return FALSE;

	return TRUE;
}

BOOL IsUserPicture(HWND hwnd)
{
	const DWORD MAX_CLASS_NAME = 255;
	const WCHAR* userPicName = L"Desktop User Picture";

	WCHAR className[MAX_CLASS_NAME] = {0};
	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, userPicName) != 0)
		return FALSE;

	return TRUE;
}

HWND GetShutDownWindow()
{
	HWND hwnd = FindWindow(L"DV2ControlHost", NULL);
	if(NULL == hwnd)
		return NULL;

	return FindWindowEx(hwnd, NULL, L"DesktopLogoffPane", NULL);
}

BOOL IsShutDown(HWND hwnd)
{
	const DWORD MAX_CLASS_NAME = 255;
	const WCHAR* parentName = L"DV2ControlHost";
	const WCHAR* shutdownName = L"DesktopLogoffPane";

	WCHAR className[MAX_CLASS_NAME] = {0};
	::GetClassName(hwnd, className, MAX_CLASS_NAME);
	if(wcscmp(className, shutdownName) != 0)
		return FALSE;

	return TRUE;
}

// hook callback function
LRESULT CALLBACK CallWndProc(int code, WPARAM wParam,  LPARAM lParam)
{
	// lotsa yada yada -> replace tray with desktop user photo window.

	// we examine all messages before they reach their destination window.
	// if the window is the tray, subclass the tray (from within the explorer process, thus!).
	// If we get a "stop subclassing" message, we unsubclass the tray again.
	// It's important this is done from this procedure, since this procedure runs
	// on the same thread as the tray window.
	CWPSTRUCT* cpw = reinterpret_cast<CWPSTRUCT*>(lParam);
	if(cpw != NULL)
	{
		if(!hasSubclassed)
		{
			hasSubclassed = SetWindowSubclass(userPic, MsgProc, 1, NULL);
			if(NULL == rotatingPic)
				SetupRotatingPicWindow();
		}

		if(moveShutdown)
		{
			if(cpw->message == WM_SHOWWINDOW && cpw->hwnd == FindWindow(L"DV2ControlHost", NULL))
			{
				HWND sm = cpw->hwnd;
				HWND sd = GetShutDownWindow();
				RECT rc = {0};
				GetClientRect(sd, &rc);
				MoveWindow(sd, shutdownXMargin, shutdownYMargin, rc.right - rc.left, rc.bottom - rc.top, TRUE);
				GetClientRect(sm, &rc);
				InvalidateRect(sm, &rc, TRUE);
				UpdateWindow(sm);
			}
		}

		if(!hasSubclassedShutDown && moveShutdown)
		{
			shutDown = GetShutDownWindow();
			if(shutDown)
				hasSubclassedShutDown = SetWindowSubclass(shutDown, ShutDownProc, 1, NULL);
		}
	}

	if(cpw != NULL && unsubclassMsg == cpw->message )
	{
		DestroyRotatingPicWindow();
		UnregisterClass(CLSNAME, hMod);

		if(hasSubclassed)
			hasSubclassed = !RemoveWindowSubclass(userPic, MsgProc, 1);

		if(hasSubclassedSysListView)
			hasSubclassedSysListView = !RemoveWindowSubclass(sysListView, SysListViewMsgProc, 1);

		if(hasSubclassedRightListView)
			hasSubclassedRightListView = !RemoveWindowSubclass(rightListView, SysListViewRightMsgProc, 1);

		if(hasSubclassedTree)
			hasSubclassedTree = !RemoveWindowSubclass(treePane, TreeMsgProc, 1);

		if(hasSubclassedShutDown)
			hasSubclassedShutDown = RemoveWindowSubclass(shutDown, ShutDownProc, 1);

		if(thisHMod != NULL)
			FreeLibrary(thisHMod);
	}
	else if(cpw != NULL && IsUserPicture(cpw->hwnd))
	{
		if(NULL == thisHMod)
			thisHMod = LoadLibrary(modPath);

		userPic = cpw->hwnd;
		if(!hasSubclassed)
		{
			hasSubclassed = SetWindowSubclass(userPic, MsgProc, 1, NULL);
		}
	}
	else if(cpw != NULL /*&& !hasSubclassedRightListView*/ && IsListViewRightPane(cpw->hwnd))
	{
		if(!hasSubclassedRightListView)
		{
			rightListView = cpw->hwnd;
			hasSubclassedRightListView = SetWindowSubclass(rightListView, SysListViewRightMsgProc, 1, NULL);
		}
	}
	else if(cpw != NULL && !hasSubclassedSysListView && IsSysListViewOfStartMenu(cpw->hwnd))
	{
		if(!hasSubclassedSysListView)
		{
			sysListView = cpw->hwnd;
			hasSubclassedSysListView = SetWindowSubclass(sysListView, SysListViewMsgProc, 1, NULL);
		}
	}
	else if(cpw != NULL && !hasSubclassedTree && IsTreePane(cpw->hwnd))
	{
		if(!hasSubclassedTree)
		{
			treePane = cpw->hwnd;
			hasSubclassedTree = SetWindowSubclass(treePane, TreeMsgProc, 1, NULL);
		}
	}

	return CallNextHookEx(hook, code, wParam, lParam);
}

// [EXPORTED] method to start the hook
BOOL CALLBACK StartHook(HMODULE hMod, HWND hwnd)
{
	if(hook != NULL)
		return FALSE;

	GetModuleFileName(hMod, modPath, MAX_PATH);

	unsubclassMsg = RegisterWindowMessage(UNSUBCLASS_MSGNAME);

	owner = hwnd;

	userPic = FindWindow(L"Desktop User Picture", NULL);
	if(NULL == userPic)
		return FALSE;

	GetWindowRect(userPic, &rcUser);

	DWORD threadid = GetWindowThreadProcessId(userPic, 0);
	hook = SetWindowsHookEx(WH_CALLWNDPROC, CallWndProc, hMod, threadid);
	if(NULL == hook)
		return FALSE;

	//Log(L"the hook has been started");

	return TRUE;
}

// [EXPORTED] method to stop the hook
BOOL CALLBACK StopHook()
{
	if(NULL == hook)
		return FALSE;

	hookIsStopping = TRUE;

	SendMessage(userPic, unsubclassMsg, 0, 0);

	BOOL res = UnhookWindowsHookEx(hook);
	if(res || !::IsWindow(userPic))
	{
		hook = NULL;
	}

	hookIsStopping = FALSE;

	return res;
}

// [EXPORTED] returns TRUE iff the hook is running
BOOL CALLBACK IsHookRunning()
{
	return hook != NULL;
}

#ifdef _MANAGED
#pragma managed(pop)
#endif

