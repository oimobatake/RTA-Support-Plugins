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
	void SetupStyleSignals();
	void UpdateObsSourceStyle();

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
	QString runnerTimers[4]; // 走者1〜4のストップしたタイマー時間
	QString timerState = "Reset"; // タイマーの状態（例: "Running", "Stopped"）

	QFont currentFont;
	QTime initCountDownTimer;
	QTimer* posUpdateTimer;

	bool showIcons[5] = {false,false,false,false,false}; // アイコン表示のフラグ（5要素に拡張）

	bool autoScreenShotOnStart = false; // 自動スクショのフラグ
	bool autoScreenShotOnStop = false;  // 自動スクショのフラグ

	std::map<QString, bool> outlineEnabledList;
	std::map<QString, int> outlineSizeList;
	std::map<QString, QString> outlineColorList;
	QString timerStopColor = "0xFFFF00";

	std::map<QString, QFont> fontList;
	std::map<QString, QString> colorList;
	std::map<QString, QString> alignList; // 追加：アライメント情報
	std::map<QString, QPointF> posList;

	QJsonObject scheduleData;
	std::vector<GameData> currentGameData;
};