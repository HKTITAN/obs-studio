#pragma once

#include <oauth/XAuth.hpp>

#include <json11.hpp>

#include <QString>
#include <QVector>

struct XBroadcast {
	QString id;
	QString title;
	QString sourceId;
	QString state;
	bool manualPublish = false;
	bool scheduled = false;
};

inline bool IsXService(const std::string &service)
{
	return service == xServiceDef.service;
}

inline bool XBroadcastIsLive(const QString &state)
{
	return state.compare(QStringLiteral("Running"), Qt::CaseInsensitive) == 0 ||
	       state.compare(QStringLiteral("Live"), Qt::CaseInsensitive) == 0;
}

class XApiWrappers : public XAuth {
	Q_OBJECT

	QString lastError;
	QString pendingGoLiveId;

	bool Request(const QString &url, const char *method, const char *body, json11::Json &jsonOut, bool allowRefresh,
		     long *statusOut);

public:
	explicit XApiWrappers(const Def &d);

	QString LastError() const { return lastError; }

	void SetStreamKey(const QString &key) { key_ = key.toStdString(); }

	void FetchUsername();
	void PrefillKeyFromBroadcasts();
	bool ListBroadcasts(QVector<XBroadcast> &out);
	bool CreateScheduled(const QString &title, const QString &description, const QString &sourceId, qint64 startMs,
			     qint64 endMs, XBroadcast &created);
	bool GoLive(const QString &broadcastId);

	void SetPendingGoLive(const QString &broadcastId) { pendingGoLiveId = broadcastId; }
	bool HasPendingGoLive() const { return !pendingGoLiveId.isEmpty(); }
	bool PublishPendingBroadcast();
};
