/******************************************************************************
    Copyright (C) 2025 by Taylor Giampaolo <warchamp7@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "NativeEventFilter.hpp"

#include <widgets/OBSBasic.hpp>

#include <sstream>
#define WIN32_LEAN_AND_MEAN
#include "../OBSWin32.hpp"

namespace OBS {

bool NativeEventFilter::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
	if (eventType == "windows_generic_MSG") {
		MSG *msg = static_cast<MSG *>(message);

		OBSBasic *main = OBSBasic::Get();
		if (!main) {
			return false;
		}

		switch (msg->message) {

		case WM_NCCALCSIZE:
			if (msg->wParam) {
				QWidget *widget = QWidget::find((WId)msg->hwnd);
				if (widget && widget->property("POPUP_WITH_DROP_SHADOW").toBool()) {
					// Hides the caption/frame but still allows shadows
					*result = 0;
					return true;
				}
			}
			break;

		case WM_SHOWWINDOW: {
			HWND wnd = msg->hwnd;
			QWidget *widget = QWidget::find((WId)wnd);
			if (widget && widget->property("POPUP_WITH_DROP_SHADOW").toBool()) {
				// Set up our popup menu/tooltip styles
				if (Win32::is11OrNewer()) {
					Win32::setBorderColor(wnd, DWMWA_COLOR_NONE);
					Win32::setCornerPreference(wnd, DWMWCP_DONOTROUND);
				}
				Win32::setBlurBehind(wnd, true);
				Win32::enableSheetOfGlass(wnd);

				// Show drop shadows on everything but tooltips
				// (until we can find a way to make the shadows smaller)
				if (widget->foregroundRole() != QPalette::ToolTipText)
					Win32::setStyle(wnd, Win32::getStyle(wnd) | WS_CAPTION | WS_CLIPCHILDREN);
			}
			break;
		}

		case WM_QUERYENDSESSION:
			main->saveAll();
			if (msg->lParam == ENDSESSION_CRITICAL) {
				break;
			}

			if (main->shouldPromptForClose()) {
				if (result) {
					*result = FALSE;
				}
				QTimer::singleShot(1, main, &OBSBasic::close);
				return true;
			}

			return false;
		case WM_ENDSESSION:
			if (msg->wParam == TRUE) {
				// Session is ending, start closing the main window now with no checks or prompts.
				main->closeWindow();
			}

			return true;
		}
	}

	return false;
}
} // namespace OBS
