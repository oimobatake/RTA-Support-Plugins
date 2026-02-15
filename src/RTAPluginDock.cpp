#include "RTAPluginDock.h"
#include "ui_RTAPluginDock.h" // .uiファイルから自動生成されるヘッダー"

#include <obs.h>
#include <obs-frontend-api.h>

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include <QString>
#include <QStringList>
#include <QFileDialog>
#include <QColorDialog>
#include <QFontDialog>

using json = nlohmann::json; // JSONライブラリのエイリアス

// テキスト名の定数リスト
std::vector<const char*> source_text_names = {
	"GameTitle",
	"RunnerName_1",
	"RunnerName_2", 
	"RunnerName_3", 
	"RunnerName_4", 
	"CountDownTimer",
	"CountUpTimer",
	"RunnerTime_1", 
	"RunnerTime_2",
	"RunnerTime_3",
	"RunnerTime_4", 
	"Category", 
	"EstimateTime",
	"Hardware",
	"Commentary"
};

// ヘルパー関数：指定されたキーワードリストに一致するカラムのインデックスを探す
int findColumnIndexByKeywords(const std::vector<std::string> &columns, const std::vector<QString> &keywords)
{
	// 1. カラム名の一覧をループ処理
	for (int i = 0; i < columns.size(); ++i) {
		// 2. 現在のカラム名を小文字のQStringに変換（大文字・小文字を区別しないため）
		QString lowerColumnName = QString::fromStdString(columns[i]).toLower();

		// 3. キーワードのリストをループ処理
		for (const QString &keyword : keywords) {
			// 4. カラム名にキーワードが含まれているかチェック
			if (lowerColumnName.contains(keyword)) {
				return i; // 見つかったら、そのカラムのインデックス(番号)を返す
			}
		}
	}
	return -1; // 見つからなかった場合は -1 を返す
}

//　指定された名前のテキストソースを取得、存在しない場合は新規作成して現在のシーンに追加する関数
//	取得したソースの参照カウントは1増えているので、使い終わったらobs_source_releaseで解放すること
obs_source_t *get_or_create_text_source(const char* source_name)
{
	obs_source_t *source = obs_get_source_by_name(source_name);
	if (!source) {
		// ソースが存在しない場合、新規作成
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "text", ""); // 初期テキストを空に設定
		source = obs_source_create("text_gdiplus_v3", source_name, settings, nullptr);
		if (!source) {
			// v3 が失敗した（古いOBS環境の）場合、v2 で再試行
			blog(LOG_INFO, "[RTA Plugin] text_gdiplus_v3 not supported. Falling back to v2...");
			source = obs_source_create("text_gdiplus_v2", source_name, settings, nullptr);
		}
		obs_data_release(settings);
	}

	// 現在のシーンに該当ソースがないなら追加する
	obs_source_t *current_scene = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(current_scene);

	// シーン内から名前でソースアイテムを検索
	obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, source_name);

	// nullptrならシーンに存在しないので追加
	if (item == nullptr) {
		auto item = obs_scene_add(scene, source);
	}

	// 解放
	obs_source_release(current_scene);

	return source;
}

RTAPluginDock::RTAPluginDock(QWidget *parent) : QWidget(parent), ui(new Ui::RTAPluginDock)
{
	// =============================== UIの初期化 ===============================
	// この1行が、.uiファイルの内容を元にUI部品をインスタンス化してくれる
	ui->setupUi(this);

	// タイマーの初期化
	timer = new QTimer(this); // 初期値は00:00:00

	// startしてないときはstopとresetボタンを無効化
	ui->timerStopButton->setEnabled(false);

	// はじめはタイマーオンリーを選択
	ui->TimerOnlyBtn->setChecked(true);

	// =============================== UIの接続設定 ===============================

	// Doneボタンがクリックされたら、onUpdateDoneButtonClicked関数を呼び出す
	connect(ui->textDone, &QPushButton::clicked, this, &RTAPluginDock::onUpdateDoneButtonClicked);

	// タイマーの合図(timeoutシグナル)と時間を更新する関数を接続
	connect(timer, &QTimer::timeout, this, [this]() {
		// カウントダウンオンリーと両方が選択されている場合のみカウントダウンタイマーを更新
		if (ui->CowntDownOnlyBtn->isChecked() || ui->BothBtn->isChecked()) {
			this->onUpdateCountDownTimer(); // カウントダウンタイマーの更新
		}
		// カウントアップオンリーと両方が選択されている場合のみカウントアップタイマーを更新
		if (ui->TimerOnlyBtn->isChecked() || ui->BothBtn->isChecked()) {
			this->onUpdateCountUpTimer(); // カウントアップタイマーの更新
		}
	});

	// startボタンがクリックされたらタイマー開始
	connect(ui->timerStartButton, &QPushButton::clicked, this, [this]() {
		timer->start(1000);                      // 1000ミリ秒（1秒）ごとにタイマーを開始
		ui->timerStartButton->setEnabled(false); // startボタンを無効化
		ui->timerStopButton->setEnabled(true);   // stopボタンを有効化
		ui->timeResetButton->setEnabled(false);  // resetボタンを無効化
		ui->textDone->setEnabled(false);         // doneボタンを無効化

		ui->TimerOnlyBtn->setEnabled(false);		// タイマーオンリーボタンを無効化
		ui->CowntDownOnlyBtn->setEnabled(false);	// カウントダウンオンリーボタンを無効化
		ui->BothBtn->setEnabled(false);				// 両方ボタンを無効化

		this->ChangeTextColor("CountUpTimer", this->timerColor);
	});

	// stopボタンがクリックされたらタイマー停止
	connect(ui->timerStopButton, &QPushButton::clicked, this, [this]() {
		// タイマーを停止
		timer->stop();
		ui->timerStartButton->setEnabled(true); // 開始ボタンを有効化
		ui->timeResetButton->setEnabled(true);  // resetボタンを有効化

		this->ChangeTextColor("CountUpTimer", this->timerStopColor);
	});

	// resetボタンがクリックされたら、タイマーをリセット
	connect(ui->timeResetButton, &QPushButton::clicked, [this]() {
		timer->stop();											// タイマーを停止
		ui->CountUpTimer->setTime(QTime(0, 0, 0));				// カウントアップタイマーをリセット
		ui->CountDonwTimer->setTime(this->initCountDownTimer);	// カウントダウンタイマーをリセット
		ui->timerStartButton->setEnabled(true);					// 開始ボタンを有効化
		ui->timerStopButton->setEnabled(false);					// 停止ボタンを無効化
		ui->textDone->setEnabled(true);                         // doneボタンを無効化

		this->ChangeText("CountUpTimer", "00:00:00");			// カウントアップタイマーのテキストを更新
		this->ChangeText("CountDownTimer", this->initCountDownTimer.toString("HH:mm:ss").toStdString().c_str()); // カウントダウンタイマーのテキストを更新

		// ラジオボタンを有効化
		ui->TimerOnlyBtn->setEnabled(true); // タイマーオンリーボタンを有効化
		ui->CowntDownOnlyBtn->setEnabled(true); // カウントダウンオンリーボタンを有効化
		ui->BothBtn->setEnabled(true);          // 両方ボタンを有効化

		// 走者テキストの初期化
		this->ChangeText("RunnerTime_1", "");
		this->ChangeText("RunnerTime_2", "");
		this->ChangeText("RunnerTime_3", "");
		this->ChangeText("RunnerTime_4", "");

		this->ChangeTextColor("CountUpTimer", this->timerColor); // カウントアップタイマーのテキストカラーを白に変更
	});

	// 走者のタイマーストップボタンがクリックされたら、走者のタイマーを更新
	connect(ui->timerStopButton_P1, &QPushButton::clicked, this, [this]() {
		this->ChangeText("RunnerTime_1", ui->CountUpTimer->time().toString("HH:mm:ss").toStdString().c_str());
	});
	connect(ui->timerStopButton_P2, &QPushButton::clicked, this, [this]() {
		this->ChangeText("RunnerTime_2", ui->CountUpTimer->time().toString("HH:mm:ss").toStdString().c_str());
	});
	connect(ui->timerStopButton_P3, &QPushButton::clicked, this, [this]() {
		this->ChangeText("RunnerTime_3", ui->CountUpTimer->time().toString("HH:mm:ss").toStdString().c_str());
	});
	connect(ui->timerStopButton_P4, &QPushButton::clicked, this, [this]() {
		this->ChangeText("RunnerTime_4", ui->CountUpTimer->time().toString("HH:mm:ss").toStdString().c_str());
	});

	// ラジオボタンの設定

	// jsonファイル選択ボタンがクリックされたら、ファイル選択ダイアログを開く
	connect(ui->scheduleFileSelect, &QPushButton::clicked, this, [this]() {
		this->loadAndParseJsonFile();
	});

	// applyボタンがクリックされたら、選択されたフォーマットを適用して一覧を作成
	connect(ui->applyButton, &QPushButton::clicked, this, [this]() {
		// 選択されたフォーマットを取得
		QString gameTitleFormat = ui->FormatGameTitleBox->currentText();
		QString runnerFormat = ui->FormatRunnerBox->currentText();
		QString categoryFormat = ui->FormatCategoryBox->currentText();
		QString hardwareFormat = ui->FormatHardwareBox->currentText();

		int gameIndex = ui->FormatGameTitleBox->currentIndex();
		int runnerIndex = ui->FormatRunnerBox->currentIndex();
		int categoryIndex = ui->FormatCategoryBox->currentIndex();
		int hardwareIndex = ui->FormatHardwareBox->currentIndex();

		this->currentGameData.clear(); // 現在のゲームデータをクリア

		if (this->scheduleData.contains("schedule")) {
			auto scheduleObj = this->scheduleData["schedule"].toObject();
			if (scheduleObj.contains("items")) {
				auto itemsArray = scheduleObj["items"].toArray();
				for (const QJsonValue &value : itemsArray) {
					QJsonObject itemObj = value.toObject();
					auto dataArray = itemObj["data"].toArray();
					GameData data;
					data.gameTitle = QString::fromStdString(dataArray[gameIndex].toString().toStdString());
					data.runnerName = QString::fromStdString(dataArray[runnerIndex].toString().toStdString());
					data.category = QString::fromStdString(dataArray[categoryIndex].toString().toStdString());
					data.estimateTime = itemObj["length_t"].toInt(); //QString::fromStdString(itemObj["length_t"].toString().toStdString());
					data.hardware = QString::fromStdString(dataArray[hardwareIndex].toString().toStdString());
					
					this->currentGameData.push_back(data);
					ui->gameSelectBox->addItem(data.gameTitle);
				}
			}
		} else {
			blog(LOG_WARNING, "[RTA Support Plugin] JSON does not contain 'schedule' or 'games'");
			return;
		}
	});

	// gameSelectBoxの選択が変更された時の処理
	connect(ui->gameSelectBox, &QComboBox::currentTextChanged, this, [this](const QString &text) {
		// 選択されたゲームタイトルに基づいて、対応するデータを表示
		for (const auto &data : this->currentGameData) {
			if (data.gameTitle == text) {
				ui->GameTitleText->setText(data.gameTitle);

				QStringList parts = data.runnerName.split(",");

				ui->RunnerText_1->clear();
				ui->RunnerText_2->clear();
				ui->RunnerText_3->clear();
				ui->RunnerText_4->clear();

				if (parts.size() > 0)
					ui->RunnerText_1->setText(parts[0]);
				if (parts.size() > 1)
					ui->RunnerText_2->setText(parts[1]);
				if (parts.size() > 2)
					ui->RunnerText_3->setText(parts[2]);
				if (parts.size() > 3)
					ui->RunnerText_4->setText(parts[3]);


				auto est = QTime::fromMSecsSinceStartOfDay(data.estimateTime * 1000); // 秒をミリ秒に変換
				ui->EstimateTime->setTime(est);

				ui->CategoryText->setText(data.category);

				ui->HardwareText->setText(data.hardware);
				break;
			}
		}
	});

	// タイマーオンリーボタンがクリックされたら、カウントアップタイマーを有効化、カウントダウンタイマーを無効化
	connect(ui->TimerOnlyBtn, &QRadioButton::clicked, this, [this]() {
		ui->CountUpTimer->setEnabled(true);
		ui->CountDonwTimer->setEnabled(false);
		ui->CowntDownOnlyBtn->setChecked(false);
		ui->BothBtn->setChecked(false);
	});

	// カウントダウンオンリーボタンがクリックされたら、カウントアップタイマーを無効化、カウントダウンタイマーを有効化
	connect(ui->CowntDownOnlyBtn, &QRadioButton::clicked, this, [this]() {
		ui->CountUpTimer->setEnabled(false);
		ui->CountDonwTimer->setEnabled(true);
		ui->TimerOnlyBtn->setChecked(false);
		ui->BothBtn->setChecked(false);
	});

	// 両方ボタンがクリックされたら、カウントアップタイマーとカウントダウンタイマーを両方有効化
	connect(ui->BothBtn, &QRadioButton::clicked, this, [this]() {
		ui->CountUpTimer->setEnabled(true);
		ui->CountDonwTimer->setEnabled(true);
		ui->TimerOnlyBtn->setChecked(false);
		ui->CowntDownOnlyBtn->setChecked(false);
	});

	//=============================== setting ===============================

	// テキストベースボタンのテキストカラー変更ボタンがクリックされたら、色選択ダイアログを開く
	connect(ui->TextBaseColorChangeBtn, &QPushButton::clicked, this, [this]() {
		// 色選択ダイアログを開く
		QColor selectedColor = QColorDialog::getColor(Qt::white, this, "Select Text Base Color");
		if (selectedColor.isValid()) {
			// 選択された色が有効な場合、テキストカラーを変更
			int colorValue = (selectedColor.blue() << 16) | (selectedColor.green() << 8) | selectedColor.red();
			this->ChangeTextColor("GameTitle", colorValue);
			this->ChangeTextColor("RunnerName_1", colorValue);
			this->ChangeTextColor("RunnerName_2", colorValue);
			this->ChangeTextColor("RunnerName_3", colorValue);
			this->ChangeTextColor("RunnerName_4", colorValue);
			this->ChangeTextColor("Category", colorValue);
			this->ChangeTextColor("EstimateTime", colorValue);
			this->ChangeTextColor("Hardware", colorValue);
			this->ChangeTextColor("Commentary", colorValue);
			// 選択された色をフレームに反映
			QString style = QString("background-color: %1").arg(selectedColor.name());
			ui->TextBaseColorFrame->setStyleSheet(style);
		}
	});

	// タイマーボタンのテキストカラー変更ボタンがクリックされたら、色選択ダイアログを開く
	connect(ui->TimerColorChangeBtn, &QPushButton::clicked, this, [this]() {
		// 色選択ダイアログを開く
		QColor selectedColor = QColorDialog::getColor(Qt::white, this, "Select Timer Color");
		if (selectedColor.isValid()) {
			// 選択された色が有効な場合、テキストカラーを変更
			this->timerColor = (selectedColor.blue() << 16) | (selectedColor.green() << 8) | selectedColor.red();
			this->ChangeTextColor("CountUpTimer", this->timerColor);
			// 選択された色をフレームに反映
			QString style = QString("background-color: %1").arg(selectedColor.name());
			ui->TimerColorFrame->setStyleSheet(style);
		}
	});

	// タイマーストップボタンのテキストカラー変更ボタンがクリックされたら、色選択ダイアログを開く
	connect(ui->TimerStopColorChangeBtn, &QPushButton::clicked, this, [this]() {
		// 色選択ダイアログを開く
		QColor selectedColor = QColorDialog::getColor(Qt::white, this, "Select Timer Stop Color");
		if (selectedColor.isValid()) {
			// 選択された色が有効な場合、テキストカラーを変更
			this->timerStopColor = (selectedColor.blue() << 16) | (selectedColor.green() << 8) | selectedColor.red();
			//this->ChangeTextColor("CountUpTimer", colorValue);
			// 選択された色をフレームに反映
			QString style = QString("background-color: %1").arg(selectedColor.name());
			ui->TimerStopColorFrame->setStyleSheet(style);
		}
	});

	// フォント変更ボタン選択
	connect(ui->fontSetting, &QPushButton::clicked, this, &RTAPluginDock::onFontChangeButtonClicked);

	// アウトライン色変更ボタン選択
	connect(ui->outlineColorButton, &QPushButton::clicked, this, [this]() {
		// 色選択ダイアログを開く
		QColor selectedColor = QColorDialog::getColor(Qt::white, this, "Select Text Base Color");
		if (selectedColor.isValid()) {
			// 選択された色が有効な場合、テキストカラーを変更
			this->currentOutlineColor = (selectedColor.blue() << 16) | (selectedColor.green() << 8) | selectedColor.red();
			// 選択された色をフレームに反映
			QString style = QString("background-color: %1").arg(selectedColor.name());
			ui->outlineColorButton->setStyleSheet(style);
		}

		if (ui->outlineCheckBox->isChecked()) {
			this->onOutlineChangeSetting(Qt::Checked);
		}
	});

	// アウトラインチェックボックスの状態が変化したときの処理
	connect(ui->outlineCheckBox, &QCheckBox::checkStateChanged, this, &RTAPluginDock::onOutlineChangeSetting);

	// アウトラインサイズの値が変化したときの処理
	connect(ui->outlineSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
		if (ui->outlineCheckBox->isChecked()) {
			this->onOutlineChangeSetting(Qt::Checked);
		}
	});
}

RTAPluginDock::~RTAPluginDock()
{
	delete ui;
}

// Doneボタンが押された時の処理
void RTAPluginDock::onUpdateDoneButtonClicked(){

	// 必要なソースを追加
	for (auto name : source_text_names) {
		obs_source_release(get_or_create_text_source(name));
	}
	//QMessageBox::information(this, "成功", "テキストソースの作成が完了しました。OBSの現在のシーンを確認してください。");

	// 1.「Done」ボタンが押されたら、テキストを更新する関数を呼び出す
	this->onUpdateTitle();

	// 走者名変更(1〜4)を呼び出す
	// memo:RunnerTextを配列で管理したい
	this->onUpdateRunnerName(1);
	this->onUpdateRunnerName(2);
	this->onUpdateRunnerName(3);
	this->onUpdateRunnerName(4);

	// カウントダウンタイマーの初期値を設定
	this->initCountDownTimer = ui->CountDonwTimer->time();
	this->ChangeText("CountDownTimer", ui->CountDonwTimer->time().toString("HH:mm:ss").toStdString().c_str());

	// EST(予定時間)を更新
	this->ChangeText("EstimateTime", ui->EstimateTime->time().toString("HH:mm:ss").toStdString().c_str());

	// カテゴリを更新
	this->ChangeText("Category", ui->CategoryText->text().toStdString().c_str());

	// 機種を更新
	this->ChangeText("Hardware", ui->HardwareText->text().toStdString().c_str());

	// 解説を更新
	this->ChangeText("Commentary", ui->CommentaryText->text().toStdString().c_str());
}

// 走者名変更
void RTAPluginDock::onUpdateRunnerName(int index)
{
	QString runnerName;
	switch (index) {
	case 1:
		runnerName = ui->RunnerText_1->text();
		break;
	case 2:
		runnerName = ui->RunnerText_2->text();
		break;
	case 3:
		runnerName = ui->RunnerText_3->text();
		break;
	case 4:
		runnerName = ui->RunnerText_4->text();
		break;
	default:
		return;
	}

	// 2.OBSのソース名を取得
	const std::string sourceNameStr = "RunnerName_" + std::to_string(index);
	const char *sourceName = sourceNameStr.c_str();
	this->ChangeText(sourceName, runnerName.toStdString().c_str());
}

// ゲームタイトル変更
void RTAPluginDock::onUpdateTitle()
{
	// 1.UIのテキストボックスから現在の文字列を取得する
	QString newTitle = ui->GameTitleText->text();
	this->ChangeText("GameTitle", newTitle.toStdString().c_str());
}

// カウントダウン処理
void RTAPluginDock::onUpdateCountDownTimer()
{
	// タイマーの現在の時間を取得
	QTime csountDownTimer = ui->CountDonwTimer->time();

	// タイマーが0になってたら処理しない
	if (csountDownTimer == QTime(0, 0, 0))
		return;

	// タイマーの時間を更新
	ui->CountDonwTimer->setTime(csountDownTimer.addSecs(-1));

	// タイマーをテキストに反映
	this->ChangeText("CountDownTimer", ui->CountDonwTimer->time().toString("HH:mm:ss").toStdString().c_str());
}

// カウントアップ処理
void RTAPluginDock::onUpdateCountUpTimer()
{
	// タイマーの現在の時間を取得
	QTime countUpTimer = ui->CountUpTimer->time();
	ui->CountUpTimer->setTime(countUpTimer.addSecs(1));

	countUpTimer = ui->CountUpTimer->time(); // 更新後の時間を再取得
	//countUpTimer = countUpTimer.addSecs(-1); // 動いた瞬間に1になるので-1した数値を表示する

	// タイマーをテキストに反映
	this->ChangeText("CountUpTimer", countUpTimer.toString("HH:mm:ss").toStdString().c_str());
}

// 指定ソースのテキスト変更
void RTAPluginDock::ChangeText(const char *sourceName, const char *text)
{
	// 1.指定した名前のソースをOBSから探す
	obs_source_t *source = obs_get_source_by_name(sourceName);

	if (!source) {
		// ソースが存在しない場合、新規作成して現在のシーンに追加
		source = get_or_create_text_source(sourceName);
	}

	// ちゃんとソースが取得できた場合のみ更新
	if (source) {
		// 2.OBSのテキストソースを更新
		obs_data_t *settings = obs_source_get_settings(source);
		obs_data_set_string(settings, "text", text);
		obs_source_update(source, settings);
		obs_data_release(settings);
		obs_source_release(source);
	} else {
		blog(LOG_WARNING, "[RTA Support Plugin] Source not found: %s", sourceName);
	}
}

// 指定ソースのテキストカラー変更
void RTAPluginDock::ChangeTextColor(const char *sourceName, int color)
{
	// 1.指定した名前のソースをOBSから探す
	obs_source_t *source = obs_get_source_by_name(sourceName);
	if (!source) {
		// ソースが存在しない場合、新規作成して現在のシーンに追加
		source = get_or_create_text_source(sourceName);
	}
	// ちゃんとソースが取得できた場合のみ更新
	if (source) {
		// 2.OBSのテキストソースを更新
		obs_data_t *settings = obs_source_get_settings(source);
		obs_data_set_int(settings, "color", color); // color1がテキストカラー
		obs_source_update(source, settings);
		obs_data_release(settings);
		obs_source_release(source);
		blog(LOG_INFO, "[RTA Support Plugin] Changed text color of %s to 0x%X", sourceName, color);
	} else {
		blog(LOG_WARNING, "[RTA Support Plugin] Source not found: %s", sourceName);
	}
}

// フォント変更ボタンクリック
void RTAPluginDock::onFontChangeButtonClicked() {
	bool ok;
	QFont font = QFontDialog::getFont(&ok, currentFont, this, "フォントを選択");

	if (ok) {
		currentFont = font;
		
		// 選択したフォントを表示
		ui->FontLabel->setText(currentFont.family());
		ui->FontSizeLabel->setText(QString::number(currentFont.pointSize()));

		// 各ソースのフォントとアウトラインを変更
		for (auto name : source_text_names) {
			this->onChangeTextFont(name, currentFont);
		}
	}
}

// 指定ソースのフォントとアウトライン変更
void RTAPluginDock::onChangeTextFont(const char* sourceName, const QFont& font) {
	obs_source_t *source = obs_get_source_by_name(sourceName);
	if (!source) {
		return; // ソースが存在しない場合は何もしない
	}

	// 新しい設定オブジェクトを作成
	obs_data_t *settings = obs_data_create();

	// フォント関連の設定
	obs_data_t *fontSettings = obs_data_create();
	obs_data_set_string(fontSettings, "face", font.family().toUtf8().constData());
	obs_data_set_int(fontSettings, "size", font.pointSize());

	// フォントスタイル
	QString style;
	if (font.bold()) {
		style += "Bold ";
	}
	if (font.italic()) {
		style += "Italic ";
	}
	obs_data_set_string(fontSettings, "style", style.toUtf8().constData());

	// 作成したフォントオブジェクトをメイン設定の"font"キーにセット
	obs_data_set_obj(settings, "font", fontSettings);

	// ソース更新
	obs_source_update(source, settings);

	// リソースを解放
	obs_data_release(settings);
	obs_source_release(source);
}

void RTAPluginDock::onOutlineChangeSetting(int state) {
	// 新しい設定オブジェクトを作成
	obs_data_t *settings = obs_data_create();

	// アウトライン設定
	obs_data_set_bool(settings, "outline", state == Qt::Checked);
	obs_data_set_int(settings, "outline_size", ui->outlineSize->value());
	obs_data_set_int(settings, "outline_color", this->currentOutlineColor);

	for (auto sourceName : source_text_names) {
		obs_source_t *source = obs_get_source_by_name(sourceName);
		if (!source) {
			return; // ソースが存在しない場合は何もしない
		}

		// ソース更新
		obs_source_update(source, settings);
		obs_source_release(source);
	}

	// リソースを解放
	obs_data_release(settings);
}

// JSONファイルを読み込んで内容をデバッグ出力する関数
void RTAPluginDock::loadAndParseJsonFile()
{
	// 1. ファイル選択ダイアログを開く
	// getOpenFileNameは静的メソッドで、簡単にファイル選択ダイアログを呼び出せる
	// 第1引数: 親ウィジェット
	// 第2引数: ダイアログのタイトル
	// 第3引数: デフォルトで開くディレクトリ ( "." はカレントディレクトリ)
	// 第4引数: ファイルのフィルタ ("JSON Files (*.json)" の部分)
	QString filePath =
		QFileDialog::getOpenFileName(this, "JSONファイルを選択", ".", "JSON Files (*.json);;All Files (*)");

	// ファイルが選択されなかった場合（キャンセルされた場合など）は何もしない
	if (filePath.isEmpty()) {
		blog(LOG_WARNING, "[RTA Support Plugin] ファイル選択がキャンセルされました。");
		return;
	}

	// 選択されたファイルのパスをデバッグ出力
	blog(LOG_INFO, "[RTA Support Plugin] 選択されたファイル: %s", filePath.toStdString().c_str());

	// 2. ファイルを開く
	QFile file(filePath);
	// QIODevice::ReadOnly: 読み取り専用でファイルを開く
	// QIODevice::Text: テキストモードで開く（改行コードを自動変換）
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		// ファイルが開けなかった場合、エラーメッセージを表示
		QMessageBox::warning(this, "エラー", "ファイルを開けませんでした:\n" + file.errorString());
		return;
	}

	blog(LOG_INFO, "[RTA Support Plugin] ファイルを正常に開きました。");

	// 3. ファイルの全内容を読み取る
	QByteArray responseData = file.readAll();

	// 4. 読み取ったデータをJSONとして解析する
	QJsonParseError parseError;
	QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData, &parseError);

	// 解析に失敗した場合
	if (parseError.error != QJsonParseError::NoError) {
		QMessageBox::warning(this, "エラー", "JSONの解析に失敗しました:\n" + parseError.errorString());
		return;
	}

	try {

		// 5. JSONデータを扱う
		// ルートがJSONオブジェクトの場合
		std::vector<std::string> columnNames;
		if (jsonDoc.isObject()) {
			this->scheduleData = jsonDoc.object();
			blog(LOG_INFO, "[RTA Support Plugin] JSONオブジェクトを読み込みました。");

			// 例: 特定のキーの値を取得する
			if (this->scheduleData.contains("schedule")) {
				auto scheduleObj = this->scheduleData["schedule"].toObject();

				if (scheduleObj.contains("columns")) {
					QJsonArray columns = scheduleObj["columns"].toArray();
					// 各カラム名を表示
					for (const QJsonValue &value : columns) {
						columnNames.push_back(value.toString().toStdString());
					}
				} else {
					blog(LOG_WARNING,
					     "[RTA Support Plugin] JSON does not contain 'columns' in 'schedule'");
				}
			}
		}

		ui->FormatGameTitleBox->clear();
		ui->FormatRunnerBox->clear();
		ui->FormatCategoryBox->clear();
		ui->FormatHardwareBox->clear();

		for (const auto &name : columnNames) {
			QString qName = QString::fromStdString(name);
			ui->FormatGameTitleBox->addItem(qName);
			ui->FormatRunnerBox->addItem(qName);
			ui->FormatCategoryBox->addItem(qName);
			ui->FormatHardwareBox->addItem(qName);
		}

		blog(LOG_INFO, "[RTA Support Plugin] カラム名をUIに追加しました。");

		// --- ステップ2:自動検出キーワードを定義
		const std::vector<QString> gameKeywords = {"GameTitle", "Game Title", "Game", "Title", "ゲーム", "ゲームタイトル", "タイトル"};
		const std::vector<QString> runnerKeywords = {"Runner", "RunnerName", "Runner Name", "Player", "走者", "走者名", "プレイヤー"};
		const std::vector<QString> categoryKeywords = {"Category", "GameCategory", "Game Category", "ゲームカテゴリ", "ゲームカテゴリー", "カテゴリ", "カテゴリー"};
		const std::vector<QString> estimateKeywords = {"Estimate", "Estimated Time", "Estimated", "EST", "予定時間", "予定タイム"};

		// --- ステップ3:カラム名から自動検出キーワードを探す ---
		int gameTitleIndex = findColumnIndexByKeywords(columnNames, gameKeywords);
		int runnerNameIndex = findColumnIndexByKeywords(columnNames, runnerKeywords);
		int categoryIndex = findColumnIndexByKeywords(columnNames, categoryKeywords);
		int hardwareIndex = findColumnIndexByKeywords(columnNames, {"Hardware", "機材", "Hard", "機種"});

		blog(LOG_INFO, "[RTA Support Plugin] カラム名の自動検出を完了しました。");

		// --- ステップ4:見つかったカラムのインデックスをUIに反映 ---
		if (gameTitleIndex != -1) {
			ui->FormatGameTitleBox->setCurrentIndex(gameTitleIndex);
		} else {
			ui->FormatGameTitleBox->setCurrentIndex(0); // デフォルトのインデックス
		}
		if (runnerNameIndex != -1) {
			ui->FormatRunnerBox->setCurrentIndex(runnerNameIndex);
		} else {
			ui->FormatRunnerBox->setCurrentIndex(0); // デフォルトのインデックス
		}
		if (categoryIndex != -1) {
			ui->FormatCategoryBox->setCurrentIndex(categoryIndex);
		} else {
			ui->FormatCategoryBox->setCurrentIndex(0); // デフォルトのインデックス
		}
		if (hardwareIndex != -1) {
			ui->FormatHardwareBox->setCurrentIndex(hardwareIndex);
		} else {
			ui->FormatHardwareBox->setCurrentIndex(0); // デフォルトのインデックス
		}

		blog(LOG_INFO, "[RTA Support Plugin] カラムのインデックスをUIに反映しました。");

	} catch (const json::parse_error &e) {
		blog(LOG_WARNING, "[RTA Support Plugin] JSON parse error: %s", e.what());
	}

	file.close(); // 読み取りが終わったらファイルを閉じる

	QMessageBox::information(this, "成功", "JSONファイルの読み込みと解析が完了しました");
}