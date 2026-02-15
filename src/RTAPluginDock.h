#pragma once
#pragma once

#include <QWidget>
#include <QTimer>
#include <QTime>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

#include <QFileDialog>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QMessageBox>

#include "json.hpp"

// .uiファイルから自動生成されるクラスの先行宣言
namespace Ui {
class RTAPluginDock;
}

struct GameData {
	// ゲームデータを保持する構造体
	QString gameTitle;
	QString runnerName;
	QString category;
	QString hardware;
	int estimateTime;
};


class RTAPluginDock : public QWidget {
	// このマクロはQtのシグナル/スロット機能を使うために必須
	Q_OBJECT

public:
	explicit RTAPluginDock(QWidget *parent = nullptr);
	~RTAPluginDock();

private slots:
	// ここにスロット関数を定義する
	void onUpdateDoneButtonClicked();	// Doneが押された時の挙動
	void onUpdateTitle();				// タイトルテキスト更新
	void onUpdateRunnerName(int);
	void onUpdateCountDownTimer();		// カウントダウンタイマーの更新
	void onUpdateCountUpTimer();		// カウントアップタイマーの更新

	void ChangeText(const char *, const char *); // テキスト変更用の関数
	void ChangeTextColor(const char *, int);          // テキストカラー変更用の関数

	void onFontChangeButtonClicked();
	void onChangeTextFont(const char*, const QFont&); // フォントとアウトライン変更用の関数
	void onOutlineChangeSetting(int);


	// ドックにソースの一覧をリストに表示する
	//void getCurrentSceneTextSourceList();

	// JSON取得ボタンが押された時に呼ばれる

	//void onScheduleFetchFinished(QNetworkReply *reply); // JSON取得完了時の処理

	void loadAndParseJsonFile();

private:
	// .uiファイルのウィジェットへのポインタを保持するメンバー
	Ui::RTAPluginDock *ui;
	QTimer *timer;						// タイマー用のQTimeオブジェクト
	QTime initCountDownTimer;			// 初期カウントダウンタイマー
	QFont currentFont;
	int currentOutlineColor = 0x000000; // 現在のアウトラインカラー
	int timerColor = 0xFFFFFF;          // タイマーのデフォルトカラー
	int timerStopColor = 0xFFFF00;      // タイマーストップ時のデフォルトカラー

	// ネットワークアクセス用のマネージャー
	//QNetworkAccessManager *networkManager;

	QJsonObject scheduleData; // JSONデータを保持するための変数

	std::vector<GameData> currentGameData; // 現在のゲームデータを保持するための変数
};