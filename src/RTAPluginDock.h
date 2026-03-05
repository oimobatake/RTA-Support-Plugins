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
	QString commentator;
	int estimateTime;
};

// 各テキスト要素のスタイルと座標をまとめた構造体
struct ElementData {
	QPointF pos = QPointF(0.0, 0.0);
	QFont font = QFont("Arial", 48);
	QString color = "#FFFFFF";
	QString align = "center";
	bool outlineEnabled = false;
	int outlineSize = 2;
	QString outlineColor = "#000000";

	bool isVisible = true;

	int maxWidth = 0;          // 0なら制限なし
	QString wrapMode = "none"; // "none", "shrink", "wrap" のいずれか
};

class RTAPluginDock : public QWidget {
	Q_OBJECT

public:
	explicit RTAPluginDock(QWidget *parent = nullptr);
	~RTAPluginDock();

	// 初期化とシグナル接続
	void SetupPositionSignals();
	void SetupStyleSignals();
	void SetupSecheduleSignals();

private slots:

	// UI操作
	void onUpdateDoneButtonClicked();

	// スケジュール管理
	void loadAndParseJsonFile();
	void onApplyScheduleClicked();

	// 配置情報の保存
	void onSavePosSettingClicked();
	void onLoadPosSettingClicked();

	// オーバーレイ更新のコア
	void SaveOverlayData();
	void LoadOverlayData();

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

	// [レイアウト名] -> [要素ID] -> [設定データ] の2次元マップ
	std::map<QString, std::map<QString, ElementData>> layoutData;

	// 現在編集中のレイアウト名 (初期値は "main")
	QString currentLayoutName = "main";

	QString timerStopColor = "0xFFFF00";
	QJsonObject scheduleData;
	std::vector<GameData> currentGameData;
};