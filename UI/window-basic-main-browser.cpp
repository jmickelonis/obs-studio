/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

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

#include <QDir>
#include <QThread>
#include <QMessageBox>
#include <qt-wrappers.hpp>
#include "window-basic-main.hpp"

#include <random>

#ifdef BROWSER_AVAILABLE
#include <browser-panel.hpp>
#endif

#include <filesystem>
namespace fs = std::filesystem;

struct QCef;
struct QCefCookieManager;

extern QCef *cef;
extern QCefCookieManager *panel_cookies;

static std::string GenId()
{
	std::random_device rd;
	std::mt19937_64 e2(rd());
	std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFF);

	uint64_t id = dist(e2);

	char id_str[20];
	snprintf(id_str, sizeof(id_str), "%16llX", (unsigned long long)id);
	return std::string(id_str);
}

void CheckExistingCookieId()
{
	OBSBasic *main = OBSBasic::Get();
	if (config_has_user_value(main->Config(), "Panels", "CookieId"))
		return;

	config_set_string(main->Config(), "Panels", "CookieId", GenId().c_str());
}

#ifdef BROWSER_AVAILABLE
static void InitPanelCookieManager()
{
	if (!cef)
		return;
	if (panel_cookies)
		return;

	CheckExistingCookieId();

	OBSBasic *main = OBSBasic::Get();
	const char *cookie_id = config_get_string(main->Config(), "Panels", "CookieId");

	/* jmick:
	 * Newer CEF doesn't allow profiles to exist in sub-directories of depth > 1.
	 * Move the directory to a location that works.
	 */
	std::string sub_path;
	sub_path += "obs_profile_cookies_";
	sub_path += cookie_id;

	BPtr<char> c_root = cef->get_cookie_path("");
	std::string root = c_root.Get();
	std::string old_path = root + "/obs_profile_cookies/" + cookie_id;

	if (fs::is_directory(old_path)) {
		std::string path = root + "/" + sub_path;

		if (!fs::is_directory(path))
			fs::rename(old_path, path);
	}

	panel_cookies = cef->create_cookie_manager(sub_path);
}
#endif

void DestroyPanelCookieManager()
{
#ifdef BROWSER_AVAILABLE
	if (panel_cookies) {
		panel_cookies->FlushStore();
		delete panel_cookies;
		panel_cookies = nullptr;
	}
#endif
}

void DeleteCookies()
{
#ifdef BROWSER_AVAILABLE
	if (panel_cookies) {
		panel_cookies->DeleteCookies("", "");
	}
#endif
}

void DuplicateCurrentCookieProfile(ConfigFile &config)
{
#ifdef BROWSER_AVAILABLE
	if (cef) {
		OBSBasic *main = OBSBasic::Get();
		std::string cookie_id = config_get_string(main->Config(), "Panels", "CookieId");
		std::string new_id = GenId();

		/* jmick:
		 * Stock OBS has a bug in this code,
		 * where the destination directory never gets created/copied to.
		 * We fix that here.
		 */
		BPtr<char> c_root = cef->get_cookie_path("");
		std::string root = c_root.Get();

		std::string src = root + "/obs_profile_cookies_" + cookie_id;
		if (fs::is_directory(src)) {
			std::string dst = root + "/obs_profile_cookies_" + new_id;
			if (!fs::is_directory(dst))
				fs::copy(src, dst, fs::copy_options::recursive);
		}

		config_set_string(config, "Panels", "CookieId", cookie_id.c_str());
		config_set_string(main->Config(), "Panels", "CookieId", new_id.c_str());
	}
#else
	UNUSED_PARAMETER(config);
#endif
}

void OBSBasic::InitBrowserPanelSafeBlock()
{
#ifdef BROWSER_AVAILABLE
	if (!cef)
		return;
	if (cef->init_browser()) {
		InitPanelCookieManager();
		return;
	}

	ExecThreadedWithoutBlocking([] { cef->wait_for_browser_init(); }, QTStr("BrowserPanelInit.Title"),
				    QTStr("BrowserPanelInit.Text"));
	InitPanelCookieManager();
#endif
}
