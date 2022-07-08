#pragma once

#include <QDialog>
#include <QList>
#include <QTimer>
#include <string>
#include <memory>

#include <json11.hpp>
#include "auth-oauth.hpp"

class BrowserDock;

class TwitchAuth : public OAuthStreamKey {
	Q_OBJECT

	friend class TwitchLogin;

	QList<
		std::pair<QSharedPointer<BrowserDock>, QSharedPointer<QAction> >
	> docks;

	bool uiLoaded = false;

	std::string name;
	std::string uuid;

	virtual bool RetryLogin() override;

	virtual void SaveInternal() override;
	virtual bool LoadInternal() override;

	bool MakeApiRequest(const char *path, json11::Json &json_out);
	bool GetChannelInfo();

	virtual void LoadUI() override;
	
	struct DockOptions
	{
		unsigned int width = 300;
		unsigned int height = 600;
		unsigned int minWidth = 200;
		unsigned int minHeight = 300;
	};
	BrowserDock *addDock(
		const std::string &name,
		const std::string &localeName,
		const std::string &title,
		const std::string &url,
		const std::string &startupScript,
		const DockOptions &dockOptions);

public:
	TwitchAuth(const Def &d);

	static std::shared_ptr<Auth> Login(QWidget *parent,
					   const std::string &service_name);

	QTimer uiLoadTimer;

public slots:
	void TryLoadSecondaryUIPanes();
	void LoadSecondaryUIPanes();
};
