#pragma once

#include <QWidget>
#include <QTimer>
#include <QTime>
#include <QMap>
#include <QFont>
#include <QJsonObject>
#include <vector>

namespace Ui {
class RTAPluginDock;
}

struct GameData {
	QString gameTitle;
	QString runnerName;
	QString category;
	QString hardware;
	int estimateTime;
};

class RTAPluginDock : public QWidget {
	Q_OBJECT

public:
	explicit RTAPluginDock(QWidget *parent = nullptr);
	~RTAPluginDock();

	// 初期化とシグナル接続
	void SetupPositionSignals();

private slots:

	// UI操作
	void onUpdateDoneButtonClicked();
	void onFontChangeButtonClicked();

	// スケジュール管理
	void loadAndParseJsonFile();
	void onApplyScheduleClicked();

	// 配置情報の保存
	void onSavePosSettingClicked();
	void onLoadPosSettingClicked();

	// オーバーレイ更新のコア
	void SaveOverlayData();

private:
	Ui::RTAPluginDock *ui;
	QTimer *timer;

	// 内部データ保持用
	QMap<QString, QString> overlayValues; // ID -> テキスト
	QMap<QString, int> overlayColors;     // ID -> BGR色

	QFont currentFont;
	QTime initCountDownTimer;
	QTimer* posUpdateTimer;

	bool outlineEnabled = false;
	int outlineSize = 2;
	int currentOutlineColor = 0x000000;
	int timerColor = 0xFFFFFF;
	int timerStopColor = 0xFFFF00;

	QJsonObject scheduleData;
	std::vector<GameData> currentGameData;
};