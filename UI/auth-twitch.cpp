#include "auth-twitch.hpp"

#include <QRegularExpression>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QUuid>

#include <qt-wrappers.hpp>
#include <obs-app.hpp>

#include "window-dock-browser.hpp"
#include "window-basic-main.hpp"
#include "remote-text.hpp"

#include <json11.hpp>

#include "ui-config.h"
#include "obf.h"

using namespace json11;

/* ------------------------------------------------------------------------- */

#define TWITCH_AUTH_URL "https://obs-oauth.jmickelonis.workers.dev/v1/twitch/redirect"
#define TWITCH_TOKEN_URL "https://obs-oauth.jmickelonis.workers.dev/v1/twitch/token"

#define TWITCH_SCOPE_VERSION 1

static Auth::Def twitchDef = {"Twitch", Auth::Type::OAuth_StreamKey};

/* ------------------------------------------------------------------------- */

TwitchAuth::TwitchAuth(const Def &d) : OAuthStreamKey(d)
{
	if (!cef)
		return;

	cef->add_popup_whitelist_url(
		"https://twitch.tv/popout/frankerfacez/chat?ffz-settings",
		this);

	/* enables javascript-based popups.  basically bttv popups */
	cef->add_popup_whitelist_url("about:blank#blocked", this);

	uiLoadTimer.setSingleShot(true);
	uiLoadTimer.setInterval(500);
	connect(&uiLoadTimer, &QTimer::timeout, this,
		&TwitchAuth::TryLoadSecondaryUIPanes);
}

bool TwitchAuth::MakeApiRequest(const char *path, Json &json_out)
{
	std::string client_id = TWITCH_CLIENTID;
	deobfuscate_str(&client_id[0], TWITCH_HASH);

	std::string url = "https://api.twitch.tv/helix/";
	url += std::string(path);

	std::vector<std::string> headers;
	headers.push_back(std::string("Client-ID: ") + client_id);
	headers.push_back(std::string("Authorization: Bearer ") + token);

	std::string output;
	std::string error;
	long error_code = 0;

	bool success = false;

	auto func = [&]() {
		success = GetRemoteFile(url.c_str(), output, error, &error_code,
					"application/json", "", nullptr,
					headers, nullptr, 5);
	};

	ExecThreadedWithoutBlocking(
		func, QTStr("Auth.LoadingChannel.Title"),
		QTStr("Auth.LoadingChannel.Text").arg(service()));
	if (error_code == 403) {
		OBSMessageBox::warning(OBSBasic::Get(),
				       Str("TwitchAuth.TwoFactorFail.Title"),
				       Str("TwitchAuth.TwoFactorFail.Text"),
				       true);
		blog(LOG_WARNING, "%s: %s", __FUNCTION__,
		     "Got 403 from Twitch, user probably does not "
		     "have two-factor authentication enabled on "
		     "their account");
		return false;
	}

	if (!success || output.empty())
		throw ErrorInfo("Failed to get text from remote", error);

	json_out = Json::parse(output, error);
	if (!error.empty())
		throw ErrorInfo("Failed to parse json", error);

	error = json_out["error"].string_value();
	if (!error.empty())
		throw ErrorInfo(error, json_out["message"].string_value());

	return true;
}

bool TwitchAuth::GetChannelInfo()
try {
	std::string client_id = TWITCH_CLIENTID;
	deobfuscate_str(&client_id[0], TWITCH_HASH);

	if (!GetToken(TWITCH_TOKEN_URL, client_id, TWITCH_SCOPE_VERSION))
		return false;
	if (token.empty())
		return false;
	if (!key_.empty())
		return true;

	Json json;
	bool success = MakeApiRequest("users", json);

	if (!success)
		return false;

	name = json["data"][0]["login"].string_value();

	std::string path = "streams/key?broadcaster_id=" +
			   json["data"][0]["id"].string_value();
	success = MakeApiRequest(path.c_str(), json);
	if (!success)
		return false;

	key_ = json["data"][0]["stream_key"].string_value();

	return true;
} catch (ErrorInfo info) {
	QString title = QTStr("Auth.ChannelFailure.Title");
	QString text = QTStr("Auth.ChannelFailure.Text")
			       .arg(service(), info.message.c_str(),
				    info.error.c_str());

	QMessageBox::warning(OBSBasic::Get(), title, text);

	blog(LOG_WARNING, "%s: %s: %s", __FUNCTION__, info.message.c_str(),
	     info.error.c_str());
	return false;
}

void TwitchAuth::SaveInternal()
{
	OBSBasic *main = OBSBasic::Get();
	config_set_string(main->Config(), service(), "Name", name.c_str());
	config_set_string(main->Config(), service(), "UUID", uuid.c_str());

	if (uiLoaded) {
		config_set_string(main->Config(), service(), "DockState",
				  main->saveState().toBase64().constData());
	}
	OAuthStreamKey::SaveInternal();
}

static inline std::string get_config_str(OBSBasic *main, const char *section,
					 const char *name)
{
	const char *val = config_get_string(main->Config(), section, name);
	return val ? val : "";
}

bool TwitchAuth::LoadInternal()
{
	if (!cef)
		return false;

	OBSBasic *main = OBSBasic::Get();
	name = get_config_str(main, service(), "Name");
	uuid = get_config_str(main, service(), "UUID");

	firstLoad = false;
	return OAuthStreamKey::LoadInternal();
}

static const char *ffz_script = "\
var ffz = document.createElement('script');\
ffz.setAttribute('src','https://cdn.frankerfacez.com/script/script.min.js');\
document.head.appendChild(ffz);";

static const char *bttv_script = "\
localStorage.setItem('bttv_clickTwitchEmotes', true);\
localStorage.setItem('bttv_darkenedMode', true);\
localStorage.setItem('bttv_bttvGIFEmotes', true);\
var bttv = document.createElement('script');\
bttv.setAttribute('src','https://cdn.betterttv.net/betterttv.js');\
document.head.appendChild(bttv);";

static const char *referrer_script1 = "\
Object.defineProperty(document, 'referrer', {get : function() { return '";
static const char *referrer_script2 = "'; }});";

static const char *style_script = "\
var _style = document.createElement('style');\
document.head.appendChild(_style);\
function _updateCSS(css) {\
	_style.innerText = css;\
}\
_updateCSS(obsstudio.getCSS('twitch'));\
obsstudio.onCSSChanged('twitch', _updateCSS);";

/* Adds a Twitch dock and its associated menu action to the main window.
 */
BrowserDock *TwitchAuth::addDock(
		const std::string &name,
		const std::string &localeName,
		const std::string &title,
		const std::string &url,
		const std::string &startupScript,
		const DockOptions &dockOptions)
{
	BrowserDock *dock = new BrowserDock();
	dock->setObjectName(QString::fromStdString(name));
	dock->resize(dockOptions.width, dockOptions.height);
	dock->setMinimumSize(dockOptions.minWidth, dockOptions.minHeight);
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);

	QString windowTitle = QTStr(localeName.c_str());
	if (!windowTitle.toStdString().compare(localeName))
		windowTitle = QString::fromStdString(title);
	dock->setWindowTitle(windowTitle);

	QCefWidget *widget = cef->create_widget(dock, url, panel_cookies);
	widget->setStartupScript(startupScript);
	dock->SetWidget(widget);

	OBSBasic *main = OBSBasic::Get();
	main->addDockWidget(Qt::RightDockWidgetArea, dock);
	QAction *action = main->AddDockWidget(dock);

	dock->setFloating(true);
	dock->setVisible(false);

	docks.append(std::make_pair(
		QSharedPointer<BrowserDock>(dock),
		QSharedPointer<QAction>(action)));
	return dock;
}

void TwitchAuth::LoadUI()
{
	if (!cef)
		return;
	if (uiLoaded)
		return;
	if (!GetChannelInfo())
		return;

	OBSBasic::InitBrowserPanelSafeBlock();

	/* Twitch panels require a UUID, it does not actually need to be unique,
	 * and is generated client-side.
	 * It is only for preferences stored in the browser's local store. */
	if (uuid.empty()) {
		QString qtUuid = QUuid::createUuid().toString();
		qtUuid.replace(QRegularExpression("[{}-]"), "");
		uuid = qtUuid.toStdString();
	}

	std::string moderation_tools_url;
	moderation_tools_url = "https://www.twitch.tv/";
	moderation_tools_url += name;
	moderation_tools_url += "/dashboard/settings/moderation?no-reload=true";

	/* ----------------------------------- */

	OBSBasic *main = OBSBasic::Get();
	std::string script;

	if (App()->IsThemeDark()) {
		script = "localStorage.setItem('twilight.theme', 1);";
	} else {
		script = "localStorage.setItem('twilight.theme', 0);";
	}

	const int twAddonChoice =
		config_get_int(main->Config(), service(), "AddonChoice");
	if (twAddonChoice) {
		if (twAddonChoice & 0x1)
			script += bttv_script;
		if (twAddonChoice & 0x2)
			script += ffz_script;
	}

	script += style_script;
	
	BrowserDock *chat = addDock(
		"twitchChat", "Twitch.Chat", "Chat",
		"https://www.twitch.tv/popout/" + name + "/chat",
		script,
		{});

	cef->add_force_popup_url(moderation_tools_url, chat);

	/* ----------------------------------- */

	QSize size = main->frameSize();
	QPoint pos = main->pos();

	chat->move(pos.x() + size.width() - chat->width() - 50, pos.y() + 50);

	if (firstLoad) {
		chat->setVisible(true);
	} else {
		const char *dockStateStr = config_get_string(
			main->Config(), service(), "DockState");
		QByteArray dockState =
			QByteArray::fromBase64(QByteArray(dockStateStr));
		main->restoreState(dockState);
	}

	TryLoadSecondaryUIPanes();

	uiLoaded = true;
}

void TwitchAuth::LoadSecondaryUIPanes()
{
	OBSBasic *main = OBSBasic::Get();

	std::string script;

	QSize size = main->frameSize();
	QPoint pos = main->pos();

	if (App()->IsThemeDark()) {
		script = "localStorage.setItem('twilight.theme', 1);";
	} else {
		script = "localStorage.setItem('twilight.theme', 0);";
	}

	script += style_script;
	script += referrer_script1;
	script += "https://www.twitch.tv/";
	script += name;
	script += "/dashboard/live";
	script += referrer_script2;

	const int twAddonChoice =
		config_get_int(main->Config(), service(), "AddonChoice");
	if (twAddonChoice) {
		if (twAddonChoice & 0x1)
			script += bttv_script;
		if (twAddonChoice & 0x2)
			script += ffz_script;
	}

	struct DockConfig
	{
		const std::string &name;
		const std::string &localeName;
		const std::string &title;
		const std::string &url;
		const DockOptions &dockOptions;
		bool visible;  // whether to show the dock on first load
		bool center;  // whether to center the dock over the main window at first
	};
	DockConfig docks[] = {
		{
			"twitchActiveMods", "Twitch.ActiveMods", "Active Mods",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/active-mods?uuid="
				+ uuid,
			{}
		},
		{
			"twitchActivityFeed", "Twitch.ActivityFeed", "Activity Feed",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/activity-feed?uuid="
				+ uuid,
			{}
		},
		{
			"twitchAdManager", "Twitch.AdManager", "Ad Manager",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/streamer-ads-manager-panel?uuid="
				+ uuid,
			{}
		},
		{
			"twitchAutoModQueue", "Twitch.AutoModQueue", "AutoMod Queue",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/auto-mod-queue?uuid="
				+ uuid,
			{}
		},
		{
			"twitchHostingYou", "Twitch.HostingYou", "Hosting You",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/hosting-you?uuid="
				+ uuid,
			{}
		},
		{
			"twitchModActions", "Twitch.ModActions", "Mod Actions",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/moderation-actions?uuid="
				+ uuid,
			{}
		},
		{
			"twitchPredictions", "Twitch.Predictions", "Predictions",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/predictions?uuid="
				+ uuid,
			{}
		},
		{
			"twitchRewardQueue", "Twitch.RewardQueue", "Reward Queue",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/reward-queue?uuid="
				+ uuid,
			{}
		},
		{
			"twitchStats", "Twitch.Stats", "Stats",
			"https://www.twitch.tv/popout/"
				+ name
				+ "/dashboard/live/stats",
			{200, 250},
			false, true
		},
		{
			"twitchStreamHealth", "Twitch.StreamHealth", "Stream Health",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/stream-health?uuid="
				+ uuid,
			{}
		},
		{
			"twitchStreamInfo", "Twitch.StreamInfo", "Stream Info",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/edit-stream-info",
			{},
			true
		},
		// {
		// 	"twitchStreamPreview", "Twitch.StreamPreview", "Stream Preview",
		// 	"https://dashboard.twitch.tv/popout/u/"
		// 		+ name
		// 		+ "/stream-manager/stream-preview?uuid="
		// 		+ uuid,
		// 	{}
		// },
		{
			"twitchQuickActions", "Twitch.QuickActions", "Quick Actions",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/quick-actions?uuid="
				+ uuid,
			{}
		},
		{
			"twitchUnbanRequests", "Twitch.UnbanRequests", "Unban Requests",
			"https://dashboard.twitch.tv/popout/u/"
				+ name
				+ "/stream-manager/unban-requests?uuid="
				+ uuid,
			{}
		},
	};

	QHash<QString, BrowserDock*> dockMap;
	unsigned int offset = 0;

	for (DockConfig config : docks) {
		BrowserDock *dock = addDock(
			config.name, config.localeName, config.title, config.url,
			script, config.dockOptions);
		dockMap[dock->objectName()] = dock;

		if (config.center) {
			// Center the dock if specified...
			QSize frameSize = dock->frameSize();
			dock->move(
				pos.x() + size.width() / 2 - frameSize.width() / 2,
				pos.y() + size.height() / 2 - frameSize.height() / 2);
		}
		else {
			// ... Otherwise, just stagger each dock
			offset += 25;
			dock->move(pos.x() + offset, pos.y() + offset);
		}

		if (firstLoad && config.visible)
			dock->setVisible(true);
	}

	if (!firstLoad) {
		uint32_t lastVersion = config_get_int(App()->GlobalConfig(),
						      "General", "LastVersion");

		if (lastVersion <= MAKE_SEMANTIC_VERSION(23, 0, 2)) {
			dockMap["twitchActivityFeed"]->setVisible(false);
		}

		const char *dockStateStr = config_get_string(
			main->Config(), service(), "DockState");
		QByteArray dockState =
			QByteArray::fromBase64(QByteArray(dockStateStr));
		main->restoreState(dockState);
	}
}

/* Twitch.tv has an OAuth for itself.  If we try to load multiple panel pages
 * at once before it's OAuth'ed itself, they will all try to perform the auth
 * process at the same time, get their own request codes, and only the last
 * code will be valid -- so one or more panels are guaranteed to fail.
 *
 * To solve this, we want to load just one panel first (the chat), and then all
 * subsequent panels should only be loaded once we know that Twitch has auth'ed
 * itself (if the cookie "auth-token" exists for twitch.tv).
 *
 * This is annoying to deal with. */
void TwitchAuth::TryLoadSecondaryUIPanes()
{
	QPointer<TwitchAuth> this_ = this;

	auto cb = [this_](bool found) {
		if (!this_) {
			return;
		}

		if (!found) {
			QMetaObject::invokeMethod(&this_->uiLoadTimer, "start");
		} else {
			QMetaObject::invokeMethod(this_,
						  "LoadSecondaryUIPanes");
		}
	};

	panel_cookies->CheckForCookie("https://www.twitch.tv", "auth-token",
				      cb);
}

bool TwitchAuth::RetryLogin()
{
	OAuthLogin login(OBSBasic::Get(), TWITCH_AUTH_URL, false);
	if (login.exec() == QDialog::Rejected) {
		return false;
	}

	std::shared_ptr<TwitchAuth> auth =
		std::make_shared<TwitchAuth>(twitchDef);
	std::string client_id = TWITCH_CLIENTID;
	deobfuscate_str(&client_id[0], TWITCH_HASH);

	return GetToken(TWITCH_TOKEN_URL, client_id, TWITCH_SCOPE_VERSION,
			QT_TO_UTF8(login.GetCode()), true);
}

std::shared_ptr<Auth> TwitchAuth::Login(QWidget *parent, const std::string &)
{
	OAuthLogin login(parent, TWITCH_AUTH_URL, false);
	if (login.exec() == QDialog::Rejected) {
		return nullptr;
	}

	std::shared_ptr<TwitchAuth> auth =
		std::make_shared<TwitchAuth>(twitchDef);

	std::string client_id = TWITCH_CLIENTID;
	deobfuscate_str(&client_id[0], TWITCH_HASH);

	if (!auth->GetToken(TWITCH_TOKEN_URL, client_id, TWITCH_SCOPE_VERSION,
			    QT_TO_UTF8(login.GetCode()))) {
		return nullptr;
	}

	if (auth->GetChannelInfo()) {
		return auth;
	}

	return nullptr;
}

static std::shared_ptr<Auth> CreateTwitchAuth()
{
	return std::make_shared<TwitchAuth>(twitchDef);
}

static void DeleteCookies()
{
	if (panel_cookies)
		panel_cookies->DeleteCookies("twitch.tv", std::string());
}

void RegisterTwitchAuth()
{
	OAuth::RegisterOAuth(twitchDef, CreateTwitchAuth, TwitchAuth::Login,
			     DeleteCookies);
}
