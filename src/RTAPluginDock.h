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

// スケジュール一件分のデータ構造
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
	QString wrapMode = "none"; // "none", "shrink", "wrap"
};

class RTAPluginDock : public QWidget {
	Q_OBJECT

public:
	explicit RTAPluginDock(QWidget *parent = nullptr);
	~RTAPluginDock();

private slots:
	// UI操作・イベント
	void onUpdateDoneButtonClicked();
	void loadAndParseJsonFile();
	void onApplyScheduleClicked();
	void onSavePosSettingClicked();
	void onLoadPosSettingClicked();

	// オーバーレイ更新のコア
	void SaveOverlayData();
	void LoadOverlayData();

private:
	// --- タブ別初期化関数 (リファクタリング) ---
	void InitMainTab();     // メイン進行タブ
	void InitScheduleTab(); // スケジュール管理タブ
	void InitDesignTab();   // デザイン・レイアウト編集タブ
	void InitGlobalTab();   // グローバル設定タブ
	void InitCommon();      // 共通・初期化処理

	Ui::RTAPluginDock *ui;
	QTimer *timer;          // メインタイマー
	QTimer *posUpdateTimer; // 座標更新の負荷軽減用

	// 内部データ保持用
	QString runnerTimers[4];
	QString timerState = "Reset";
	QFont currentFont;
	QTime initCountDownTimer;

	bool showIcons[5] = {false, false, false, false, false};
	bool autoScreenShotOnStart = false;
	bool autoScreenShotOnStop = false;

	// [レイアウト名] -> [要素ID] -> [設定データ] の2次元マップ
	std::map<QString, std::map<QString, ElementData>> layoutData;

	QString currentLayoutName = "main";
	QString timerStopColor = "#FFFF00";
	QJsonObject scheduleData;
	std::vector<GameData> currentGameData;
};