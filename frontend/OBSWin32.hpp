
/* Various functions to expose Windows API features.
 */

#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <QColor>

namespace Win32 {

bool is11OrNewer();

COLORREF getColor(const QColor &color);

bool setAttribute(const HWND &handle, const DWORD &attribute, const LPCVOID &value, const DWORD &size);
bool setBlurBehind(const HWND &handle, const bool &enable);
bool setBorderColor(const HWND &handle, const COLORREF &color);
bool setCaptionColor(const HWND &handle, const COLORREF &color);
bool setCornerPreference(const HWND &handle, const DWM_WINDOW_CORNER_PREFERENCE &preference);
LONG getExtendedStyle(const HWND &handle);
void setExtendedStyle(const HWND &handle, const LONG &style);
LONG getStyle(const HWND &handle);
void setStyle(const HWND &handle, const LONG &style);
bool setUseImmersiveDarkMode(const HWND &handle, const bool &enable);

bool extendIntoClientArea(const HWND &handle, const MARGINS &margins);
bool enableSheetOfGlass(const HWND &handle);

} // namespace Win32
