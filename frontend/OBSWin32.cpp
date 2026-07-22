
#include "OBSWin32.hpp"
#pragma comment(lib, "dwmapi")

typedef LONG NTSTATUS;
typedef NTSTATUS(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

namespace Win32 {

static bool _is11OrNewer()
{
	HMODULE module = GetModuleHandleW(L"ntdll.dll");
	if (!module) {
		return false;
	}
	RtlGetVersionPtr fn = (RtlGetVersionPtr)GetProcAddress(module, "RtlGetVersion");
	if (!fn) {
		return false;
	}
	RTL_OSVERSIONINFOW ovi = {0};
	ovi.dwOSVersionInfoSize = sizeof(ovi);
	if (fn(&ovi)) {
		return false;
	}
	return ovi.dwMajorVersion > 10 || ovi.dwMajorVersion == 10 && ovi.dwBuildNumber >= 22000;
}

static bool _11OrNewer = _is11OrNewer();

bool is11OrNewer()
{
	return _11OrNewer;
}

COLORREF getColor(const QColor &color)
{
	return RGB(color.red(), color.green(), color.blue());
}

bool setAttribute(const HWND &handle, const DWORD &attribute, const LPCVOID &value, const DWORD &size)
{
	HRESULT result = DwmSetWindowAttribute(handle, attribute, value, size);
	return SUCCEEDED(result);
}

bool setBlurBehind(const HWND &handle, const bool &enable)
{
	DWM_BLURBEHIND blurBehind = {};
	blurBehind.fEnable = enable;
	if (enable) {
		blurBehind.dwFlags = DWM_BB_ENABLE;
	}
	HRESULT result = DwmEnableBlurBehindWindow(handle, &blurBehind);
	return SUCCEEDED(result);
}

bool setBorderColor(const HWND &handle, const COLORREF &color)
{
	return setAttribute(handle, DWMWA_BORDER_COLOR, &color, sizeof(color));
}

bool setCaptionColor(const HWND &handle, const COLORREF &color)
{
	return setAttribute(handle, DWMWA_CAPTION_COLOR, &color, sizeof(color));
}

bool setCornerPreference(const HWND &handle, const DWM_WINDOW_CORNER_PREFERENCE &preference)
{
	return setAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}

LONG getExtendedStyle(const HWND &handle)
{
	return GetWindowLongW(handle, GWL_EXSTYLE);
}

void setExtendedStyle(const HWND &handle, const LONG &style)
{
	SetWindowLongW(handle, GWL_EXSTYLE, style);
}

LONG getStyle(const HWND &handle)
{
	return GetWindowLong(handle, GWL_STYLE);
}

void setStyle(const HWND &handle, const LONG &style)
{
	SetWindowLong(handle, GWL_STYLE, style);
}

bool setUseImmersiveDarkMode(const HWND &handle, const bool &enable)
{
	BOOL value = enable;
	if (setAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value))) {
		return true;
	}
	// Try the old, undocumented way
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
	if (!_11OrNewer) {
		return setAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &value, sizeof(value));
	}
	return false;
}

bool extendIntoClientArea(const HWND &handle, const MARGINS &margins)
{
	HRESULT result = DwmExtendFrameIntoClientArea(handle, &margins);
	return SUCCEEDED(result);
}

bool enableSheetOfGlass(const HWND &handle)
{
	return extendIntoClientArea(handle, {-1});
}

} // namespace Win32
