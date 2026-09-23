#pragma once

#include <utility/XApiWrappers.hpp>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

class OBSXBroadcastActions : public QDialog {
	Q_OBJECT

	XApiWrappers *api = nullptr;
	QVector<XBroadcast> broadcasts;
	bool valid = false;

	QLineEdit *titleEdit = nullptr;
	QPlainTextEdit *descriptionEdit = nullptr;
	QListWidget *list = nullptr;
	QLineEdit *streamKey = nullptr;
	QLabel *status = nullptr;
	QCheckBox *scheduleLater = nullptr;
	QDateTimeEdit *whenEdit = nullptr;
	QComboBox *duration = nullptr;
	QPushButton *createButton = nullptr;

	void BuildUi();
	void ReloadBroadcasts();
	void CreateBroadcast();
	void UseSelected();
	void ScheduleToggled(bool checked);
	const XBroadcast *FindBroadcast(const QString &id) const;
	QString ChosenKey() const;

signals:
	void ready(const std::string &broadcastId, const std::string &sourceId);

public:
	explicit OBSXBroadcastActions(QWidget *parent, Auth *auth);

	bool Valid() const { return valid; }
};
