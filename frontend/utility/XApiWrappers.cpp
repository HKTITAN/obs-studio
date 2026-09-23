#include "XApiWrappers.hpp"

#include <OBSApp.hpp>
#include <utility/RemoteTextThread.hpp>

#include <qt-wrappers.hpp>

#include <QRegularExpression>
#include <QUrl>

#include <json11.hpp>

#include "moc_XApiWrappers.cpp"

using namespace json11;

namespace {

constexpr const char *XApiHost = "https://api.x.com";

QString JsonAsString(const Json &value)
{
	if (value.is_string()) {
		return QString::fromStdString(value.string_value());
	}
	if (value.is_number()) {
		return QString::number(static_cast<qlonglong>(value.number_value()));
	}
	return {};
}

QString BroadcastPathId(const Json &item)
{
	// Path :id is broadcast_id (or id). scheduled_broadcast_id is the numeric
	// scheduler id and is rejected by POST .../live.
	static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9]{1,13}$"));
	const char *keys[] = {"broadcast_id", "id"};
	for (const char *key : keys) {
		const QString id = JsonAsString(item[key]);
		if (re.match(id).hasMatch()) {
			return id;
		}
	}
	return {};
}

QString ErrorText(const Json &json)
{
	const std::string message = json["message"].string_value();
	if (!message.empty()) {
		return QString::fromStdString(message);
	}
	const std::string detail = json["detail"].string_value();
	if (!detail.empty()) {
		return QString::fromStdString(detail);
	}
	const std::string title = json["title"].string_value();
	if (!title.empty()) {
		return QString::fromStdString(title);
	}
	const auto &errors = json["errors"].array_items();
	if (!errors.empty()) {
		const QString nested = ErrorText(errors[0]);
		if (!nested.isEmpty()) {
			return nested;
		}
	}
	const std::string error = json["error"].string_value();
	if (!error.empty()) {
		const std::string description = json["error_description"].string_value();
		if (!description.empty()) {
			return QString::fromStdString(error + ": " + description);
		}
		return QString::fromStdString(error);
	}
	return {};
}

XBroadcast ParseBroadcast(const Json &item, bool scheduled)
{
	XBroadcast broadcast;
	broadcast.id = BroadcastPathId(item);
	broadcast.title = JsonAsString(item["title"]);
	broadcast.sourceId = JsonAsString(item["source_id"]);
	broadcast.state = JsonAsString(item["state"]);
	broadcast.manualPublish = item["manual_publish"].bool_value();
	broadcast.scheduled = scheduled;
	return broadcast;
}

void MergeBroadcast(QVector<XBroadcast> &out, const XBroadcast &incoming)
{
	if (incoming.id.isEmpty()) {
		return;
	}
	for (XBroadcast &existing : out) {
		if (existing.id != incoming.id) {
			continue;
		}
		if (existing.sourceId.isEmpty()) {
			existing.sourceId = incoming.sourceId;
		}
		if (incoming.scheduled) {
			existing.scheduled = true;
			existing.manualPublish = incoming.manualPublish;
		}
		if (XBroadcastIsLive(incoming.state)) {
			existing.state = incoming.state;
		} else if (existing.state.isEmpty()) {
			existing.state = incoming.state;
		}
		if (existing.title.isEmpty()) {
			existing.title = incoming.title;
		}
		return;
	}
	out.push_back(incoming);
}

} // namespace

XApiWrappers::XApiWrappers(const Def &d) : XAuth(d) {}

bool XApiWrappers::Request(const QString &url, const char *method, const char *body, Json &jsonOut, bool allowRefresh,
			   long *statusOut)
{
	lastError.clear();
	if ((token.empty() || TokenExpired()) && allowRefresh) {
		if (!RefreshToken()) {
			lastError = QTStr("X.Actions.Error.Api").arg(QStringLiteral("unauthorized"));
			return false;
		}
	}
	if (token.empty()) {
		lastError = QTStr("X.Actions.Error.Api").arg(QStringLiteral("unauthorized"));
		return false;
	}

	std::string output;
	std::string error;
	long status = 0;
	const std::vector<std::string> headers = {"Authorization: Bearer " + token, "Accept: application/json"};
	const bool success = GetRemoteFile(QT_TO_UTF8(url), output, error, &status, "application/json", method, body,
					   headers, nullptr, 20, false);
	if (statusOut) {
		*statusOut = status;
	}

	if (status == 401 && allowRefresh) {
		if (!RefreshToken()) {
			lastError = QTStr("X.Actions.Error.Api").arg(QStringLiteral("unauthorized"));
			return false;
		}
		return Request(url, method, body, jsonOut, false, statusOut);
	}

	if (!output.empty()) {
		std::string parseError;
		jsonOut = Json::parse(output, parseError);
		if (!parseError.empty()) {
			lastError = QTStr("X.Actions.Error.Api").arg(QStringLiteral("invalid JSON"));
			return false;
		}
	}

	if (!success || status >= 400 || status == 0) {
		QString message = ErrorText(jsonOut);
		if (message.isEmpty()) {
			message = error.empty() ? QString::number(status) : QString::fromStdString(error);
		}
		lastError = QTStr("X.Actions.Error.Api").arg(message);
		blog(LOG_WARNING, "X API %s failed (%ld)", method ? method : "GET", status);
		return false;
	}
	return true;
}

void XApiWrappers::FetchUsername()
{
	// The broadcasts schema has no account name. /2/users/me is the
	// user-context identity call and needs the users.read scope.
	Json json;
	const QString url = QStringLiteral("%1/2/users/me?user.fields=id,name,username").arg(XApiHost);
	if (!Request(url, "GET", nullptr, json, true, nullptr)) {
		return;
	}
	const Json data = json["data"].is_object() ? json["data"] : json;
	QString name = JsonAsString(data["username"]);
	if (name.isEmpty()) {
		name = JsonAsString(data["name"]);
	}
	if (!name.isEmpty()) {
		username = name;
	}
}

bool XApiWrappers::ListBroadcasts(QVector<XBroadcast> &out)
{
	out.clear();
	bool scheduledOk = true;
	QString page;
	for (int i = 0; i < 10 && scheduledOk; i++) {
		QString url = QStringLiteral("%1/2/broadcasts/scheduled?max_results=100").arg(XApiHost);
		if (!page.isEmpty()) {
			url += QStringLiteral("&pagination_token=") + QString::fromUtf8(QUrl::toPercentEncoding(page));
		}
		Json json;
		if (!Request(url, "GET", nullptr, json, true, nullptr)) {
			scheduledOk = false;
			break;
		}
		for (const Json &item : json["data"].array_items()) {
			MergeBroadcast(out, ParseBroadcast(item, true));
		}
		page = JsonAsString(json["meta"]["next_token"]);
		if (page.isEmpty()) {
			break;
		}
	}

	bool liveOk = true;
	page.clear();
	for (int i = 0; i < 10 && liveOk; i++) {
		QString url = QStringLiteral(
			"%1/2/broadcasts?max_results=100&broadcast.fields=broadcast_id,id,title,state,source_id,scheduled_start_ms,scheduled_end_ms")
				      .arg(XApiHost);
		if (!page.isEmpty()) {
			url += QStringLiteral("&pagination_token=") + QString::fromUtf8(QUrl::toPercentEncoding(page));
		}
		Json json;
		if (!Request(url, "GET", nullptr, json, true, nullptr)) {
			liveOk = false;
			break;
		}
		for (const Json &item : json["data"].array_items()) {
			MergeBroadcast(out, ParseBroadcast(item, false));
		}
		page = JsonAsString(json["meta"]["next_token"]);
		if (page.isEmpty()) {
			break;
		}
	}
	return scheduledOk || liveOk;
}

void XApiWrappers::PrefillKeyFromBroadcasts()
{
	QVector<XBroadcast> items;
	if (!ListBroadcasts(items)) {
		return;
	}
	QString fallback;
	for (const XBroadcast &item : items) {
		if (item.sourceId.isEmpty()) {
			continue;
		}
		if (XBroadcastIsLive(item.state)) {
			key_ = item.sourceId.toStdString();
			return;
		}
		if (fallback.isEmpty()) {
			fallback = item.sourceId;
		}
	}
	if (!fallback.isEmpty()) {
		key_ = fallback.toStdString();
	}
}

bool XApiWrappers::CreateScheduled(const QString &title, const QString &description, const QString &sourceId,
				   qint64 startMs, qint64 endMs, XBroadcast &created)
{
	Json::object payload = {
		{"source_id", sourceId.toStdString()},
		{"scheduled_start_ms", std::to_string(startMs)},
		{"scheduled_end_ms", std::to_string(endMs)},
		{"title", title.toStdString()},
		{"manual_publish", true},
	};
	if (!description.isEmpty()) {
		payload.emplace("description", description.toStdString());
	}
	const std::string body = Json(payload).dump();
	Json json;
	const QString url = QStringLiteral("%1/2/broadcasts/scheduled").arg(XApiHost);
	if (!Request(url, "POST", body.c_str(), json, true, nullptr)) {
		return false;
	}
	const Json data = json["data"].is_object() ? json["data"] : json;
	created = ParseBroadcast(data, true);
	created.manualPublish = true;
	created.scheduled = true;
	if (created.sourceId.isEmpty()) {
		created.sourceId = sourceId;
	}
	if (created.id.isEmpty()) {
		lastError = QTStr("X.Actions.Error.Api").arg(QStringLiteral("missing broadcast id"));
		return false;
	}
	return true;
}

bool XApiWrappers::GoLive(const QString &broadcastId)
{
	static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9]{1,13}$"));
	if (!re.match(broadcastId).hasMatch()) {
		lastError = QTStr("X.Actions.Error.Api").arg(QStringLiteral("invalid broadcast id"));
		return false;
	}
	long status = 0;
	Json json;
	const QString url = QStringLiteral("%1/2/broadcasts/scheduled/%2/live").arg(XApiHost, broadcastId);
	const bool ok = Request(url, "POST", "{}", json, true, &status);
	if (status == 409) {
		return true;
	}
	return ok;
}

bool XApiWrappers::PublishPendingBroadcast()
{
	if (pendingGoLiveId.isEmpty()) {
		return true;
	}
	if (!GoLive(pendingGoLiveId)) {
		return false;
	}
	pendingGoLiveId.clear();
	return true;
}
