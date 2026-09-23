#include "OBSXBroadcastActions.hpp"

#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "moc_OBSXBroadcastActions.cpp"

namespace {

const char *LiveStudioUrl = "https://x.com/i/live-studio";

void ShowError(QWidget *parent, const QString &text)
{
	QMessageBox box(parent);
	box.setWindowTitle(QTStr("X.Actions.Error.Title"));
	box.setText(text);
	box.setTextFormat(Qt::RichText);
	box.setIcon(QMessageBox::Warning);
	box.exec();
}

} // namespace

OBSXBroadcastActions::OBSXBroadcastActions(QWidget *parent, Auth *auth) : QDialog(parent)
{
	api = dynamic_cast<XApiWrappers *>(auth);
	if (!api) {
		return;
	}
	valid = true;
	setWindowTitle(QTStr("X.Actions.WindowTitle"));
	resize(520, 640);
	BuildUi();

	obs_service_t *service = OBSBasic::Get()->GetService();
	OBSDataAutoRelease settings = obs_service_get_settings(service);
	const char *savedKey = obs_data_get_string(settings, "key");
	if (savedKey && *savedKey) {
		streamKey->setText(QString::fromUtf8(savedKey));
	} else if (!api->key().empty()) {
		streamKey->setText(QString::fromStdString(api->key()));
	}
	ReloadBroadcasts();
}

void OBSXBroadcastActions::BuildUi()
{
	auto *layout = new QVBoxLayout(this);

	auto *hint = new QLabel(QTStr("X.Actions.StreamKeyHint"), this);
	hint->setWordWrap(true);
	layout->addWidget(hint);

	titleEdit = new QLineEdit(this);
	titleEdit->setPlaceholderText(QTStr("X.Actions.Title"));
	layout->addWidget(titleEdit);

	descriptionEdit = new QPlainTextEdit(this);
	descriptionEdit->setPlaceholderText(QTStr("X.Actions.Description"));
	descriptionEdit->setFixedHeight(72);
	layout->addWidget(descriptionEdit);

	layout->addWidget(new QLabel(QTStr("X.Actions.Existing"), this));
	list = new QListWidget(this);
	layout->addWidget(list, 1);

	auto *listButtons = new QHBoxLayout();
	auto *refresh = new QPushButton(QTStr("X.Actions.Refresh"), this);
	auto *studio = new QPushButton(QTStr("X.Actions.OpenLiveStudio"), this);
	listButtons->addWidget(refresh);
	listButtons->addWidget(studio);
	listButtons->addStretch(1);
	layout->addLayout(listButtons);

	layout->addWidget(new QLabel(QTStr("X.Actions.StreamKey"), this));
	auto *keyRow = new QHBoxLayout();
	streamKey = new QLineEdit(this);
	streamKey->setEchoMode(QLineEdit::Password);
	auto *showKey = new QPushButton(QTStr("Show"), this);
	keyRow->addWidget(streamKey, 1);
	keyRow->addWidget(showKey);
	layout->addLayout(keyRow);

	scheduleLater = new QCheckBox(QTStr("X.Actions.ScheduleLater"), this);
	layout->addWidget(scheduleLater);

	auto *whenRow = new QHBoxLayout();
	whenEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
	whenEdit->setCalendarPopup(true);
	whenEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
	whenEdit->setEnabled(false);
	duration = new QComboBox(this);
	for (int hours : {1, 2, 4, 8}) {
		duration->addItem(QTStr("X.Actions.Duration.Hours").arg(hours), hours);
	}
	duration->setCurrentIndex(2);
	whenRow->addWidget(whenEdit, 1);
	whenRow->addWidget(duration);
	layout->addLayout(whenRow);

	status = new QLabel(this);
	status->setWordWrap(true);
	layout->addWidget(status);

	auto *actions = new QHBoxLayout();
	createButton = new QPushButton(QTStr("X.Actions.CreateGoLive"), this);
	auto *selectButton = new QPushButton(QTStr("X.Actions.SelectGoLive"), this);
	auto *cancel = new QPushButton(QTStr("Cancel"), this);
	actions->addWidget(createButton);
	actions->addWidget(selectButton);
	actions->addStretch(1);
	actions->addWidget(cancel);
	layout->addLayout(actions);

	connect(refresh, &QPushButton::clicked, this, &OBSXBroadcastActions::ReloadBroadcasts);
	connect(studio, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl(LiveStudioUrl)); });
	connect(showKey, &QPushButton::clicked, this, [this, showKey]() {
		const bool hidden = streamKey->echoMode() == QLineEdit::Password;
		streamKey->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
		showKey->setText(hidden ? QTStr("Hide") : QTStr("Show"));
	});
	connect(scheduleLater, &QCheckBox::toggled, this, &OBSXBroadcastActions::ScheduleToggled);
	connect(createButton, &QPushButton::clicked, this, &OBSXBroadcastActions::CreateBroadcast);
	connect(selectButton, &QPushButton::clicked, this, &OBSXBroadcastActions::UseSelected);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void OBSXBroadcastActions::ScheduleToggled(bool checked)
{
	whenEdit->setEnabled(checked);
	createButton->setText(checked ? QTStr("X.Actions.Schedule") : QTStr("X.Actions.CreateGoLive"));
}

const XBroadcast *OBSXBroadcastActions::FindBroadcast(const QString &id) const
{
	for (const XBroadcast &item : broadcasts) {
		if (item.id == id) {
			return &item;
		}
	}
	return nullptr;
}

QString OBSXBroadcastActions::ChosenKey() const
{
	return streamKey->text().trimmed();
}

void OBSXBroadcastActions::ReloadBroadcasts()
{
	QVector<XBroadcast> loaded;
	bool ok = false;
	QString error;
	auto work = [&]() {
		ok = api->ListBroadcasts(loaded);
		if (!ok) {
			error = api->LastError();
		}
	};
	ExecThreadedWithoutBlocking(work, QTStr("Auth.LoadingChannel.Title"),
				    QTStr("Auth.LoadingChannel.Text").arg(QStringLiteral("X")));

	broadcasts = loaded;
	list->clear();
	for (const XBroadcast &item : broadcasts) {
		QString label = item.title.isEmpty() ? item.id : item.title;
		if (!item.state.isEmpty()) {
			label += QStringLiteral(" - ") + item.state;
		}
		if (item.sourceId.isEmpty()) {
			label += QStringLiteral(" - ") + QTStr("X.Actions.NoKey");
		}
		auto *row = new QListWidgetItem(label, list);
		row->setData(Qt::UserRole, item.id);
	}

	if (!ok) {
		status->setText(error);
	} else if (broadcasts.isEmpty()) {
		status->setText(QTStr("X.Actions.None"));
	} else {
		status->clear();
	}

	if (streamKey->text().isEmpty()) {
		for (const XBroadcast &item : broadcasts) {
			if (!item.sourceId.isEmpty()) {
				streamKey->setText(item.sourceId);
				break;
			}
		}
	}
}

void OBSXBroadcastActions::CreateBroadcast()
{
	const QString key = ChosenKey();
	if (key.isEmpty()) {
		ShowError(this, QTStr("X.Actions.Error.NeedKey"));
		return;
	}

	QString title = titleEdit->text().trimmed();
	if (title.isEmpty()) {
		title = QStringLiteral("OBS Studio");
	}
	const bool later = scheduleLater->isChecked();
	const qint64 start = later ? whenEdit->dateTime().toMSecsSinceEpoch() : QDateTime::currentMSecsSinceEpoch();
	const int hours = duration->currentData().toInt();
	const qint64 end = start + static_cast<qint64>(hours) * 3600 * 1000;

	XBroadcast created;
	bool ok = false;
	QString error;
	auto work = [&]() {
		ok = api->CreateScheduled(title, descriptionEdit->toPlainText().trimmed(), key, start, end, created);
		if (!ok) {
			error = api->LastError();
		}
	};
	ExecThreadedWithoutBlocking(work, QTStr("Auth.Authing.Title"), QTStr("Auth.Authing.Text").arg(QStringLiteral("X")));
	if (!ok) {
		ShowError(this, error.isEmpty() ? QTStr("X.Actions.Error.Api").arg(QStringLiteral("create")) : error);
		return;
	}

	if (later) {
		api->SetStreamKey(key);
		obs_service_t *service = OBSBasic::Get()->GetService();
		OBSDataAutoRelease settings = obs_service_get_settings(service);
		obs_data_set_string(settings, "key", QT_TO_UTF8(key));
		obs_data_set_string(settings, "broadcast_id", QT_TO_UTF8(created.id));
		obs_service_update(service, settings);
		QMessageBox::information(this, QTStr("X.Actions.WindowTitle"), QTStr("X.Actions.Scheduled"));
		accept();
		return;
	}

	api->SetPendingGoLive(created.id);
	api->SetStreamKey(key);
	emit ready(created.id.toStdString(), key.toStdString());
	accept();
}

void OBSXBroadcastActions::UseSelected()
{
	QListWidgetItem *row = list->currentItem();
	if (!row) {
		ShowError(this, QTStr("X.Actions.Error.NeedSelection"));
		return;
	}
	const XBroadcast *broadcast = FindBroadcast(row->data(Qt::UserRole).toString());
	if (!broadcast) {
		ShowError(this, QTStr("X.Actions.Error.NeedSelection"));
		return;
	}

	QString key = broadcast->sourceId;
	if (key.isEmpty()) {
		key = ChosenKey();
	}
	if (key.isEmpty()) {
		ShowError(this, QTStr("X.Actions.Error.NeedKey"));
		return;
	}

	if (broadcast->scheduled && broadcast->manualPublish && !XBroadcastIsLive(broadcast->state)) {
		api->SetPendingGoLive(broadcast->id);
	} else {
		api->SetPendingGoLive({});
	}
	api->SetStreamKey(key);
	emit ready(broadcast->id.toStdString(), key.toStdString());
	accept();
}
