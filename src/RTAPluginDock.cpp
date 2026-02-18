#include "RTAPluginDock.h"
#include "ui_RTAPluginDock.h"

#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>

#include <string>
#include <vector>
#include <algorithm>

#include <QString>
#include <QStringList>
#include <QFileDialog>
#include <QColorDialog>
#include <QFontDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QCheckBox>

// GameTitle～Commentaryの日本語訳と対応するものをmapで追加
// <日本語, 英語> のペア
std::map<QString, QString> textElementMap = {
	{"ゲームタイトル", "GameTitle"},
	{"カテゴリ", "Category"},
	{"ハードウェア", "Hardware"},
	{"予想時間", "EstimateTime"},
	{"カウントアップタイマー", "CountUpTimer"},
	{"カウントダウンタイマー", "CountDownTimer"},
	{"走者タイマー1", "RunnerTimer_1"},
	{"走者タイマー2", "RunnerTimer_2"},
	{"走者タイマー3", "RunnerTimer_3"},
	{"走者タイマー4", "RunnerTimer_4"},
	{"走者名1", "RunnerName_1"},
	{"走者名2", "RunnerName_2"},
	{"走者名3", "RunnerName_3"},
	{"走者名4", "RunnerName_4"},
	{"解説コメント", "Commentary"}
};

// ヘルパー関数：キーワードに一致するカラムインデックスを探す
int findColumnIndexByKeywords(const std::vector<std::string> &columns, const std::vector<QString> &keywords)
{
	for (int i = 0; i < (int)columns.size(); ++i) {
		QString lowerColumnName = QString::fromStdString(columns[i]).toLower();
		for (const QString &keyword : keywords) {
			if (lowerColumnName.contains(keyword.toLower())) {
				return i;
			}
		}
	}
	return -1;
}

RTAPluginDock::RTAPluginDock(QWidget *parent) : QWidget(parent), ui(new Ui::RTAPluginDock)
{
	ui->setupUi(this);

	// タイマー初期化
	timer = new QTimer(this);
	ui->timerStopButton->setEnabled(false);
	ui->TimerOnlyBtn->setChecked(true);

	// デフォルト設定
	currentFont = QFont("Arial", 48);
	overlayColors["TextBase"] = 0xFFFFFF; // 白 (BGR: 0xFFFFFF)
	overlayColors["CountUpTimer"] = 0xFFFFFF;

	// --- シグナル/スロット接続 ---

	// Doneボタン: 現在のUI入力を確定させ、JSONに保存
	connect(ui->textDone, &QPushButton::clicked, this, &RTAPluginDock::onUpdateDoneButtonClicked);

	// タイマー定期実行 (1秒ごと)
	connect(timer, &QTimer::timeout, this, [this]() {
		if (ui->CowntDownOnlyBtn->isChecked() || ui->BothBtn->isChecked()) {
			QTime t = ui->CountDonwTimer->time();
			if (t != QTime(0, 0, 0))
				ui->CountDonwTimer->setTime(t.addSecs(-1));
		}
		if (ui->TimerOnlyBtn->isChecked() || ui->BothBtn->isChecked()) {
			ui->CountUpTimer->setTime(ui->CountUpTimer->time().addSecs(1));
		}
		// 更新のたびにJSONファイルを保存（CSS書き換えはしないためチカチカしない）
		this->SaveOverlayData();
	});

	// タイマーオンリー：カウントアップを表示・有効化、カウントダウンを非表示・無効化
	connect(ui->TimerOnlyBtn, &QRadioButton::clicked, this, [this]() {
		// 表示の切り替え
		//ui->CountUpTimer->setEnabled(true);
		//ui->CountDonwTimer->setEnabled(false);

		// 有効状態の切り替え
		ui->CountUpTimer->setEnabled(true);
		ui->CountDonwTimer->setEnabled(false);

		// 他のボタンの状態（念のため）
		ui->CowntDownOnlyBtn->setChecked(false);
		ui->BothBtn->setChecked(false);

		// ブラウザオーバーレイに反映（JSON出力を利用している場合）
		this->SaveOverlayData();
	});

	// カウントダウンオンリー：カウントアップを非表示・無効化、カウントダウンを表示・有効化
	connect(ui->CowntDownOnlyBtn, &QRadioButton::clicked, this, [this]() {
		// 表示の切り替え
		//ui->CountUpTimer->setEnabled(false);
		//ui->CountDonwTimer->setEnabled(true);

		// 有効状態の切り替え
		ui->CountUpTimer->setEnabled(false);
		ui->CountDonwTimer->setEnabled(true);

		// 他のボタンの状態
		ui->TimerOnlyBtn->setChecked(false);
		ui->BothBtn->setChecked(false);

		this->SaveOverlayData();
	});

	// 両方：両方を表示・有効化
	connect(ui->BothBtn, &QRadioButton::clicked, this, [this]() {
		// 両方を表示
		//ui->CountUpTimer->setVisible(true);
		//ui->CountDonwTimer->setVisible(true);

		// 両方を有効化
		ui->CountUpTimer->setEnabled(true);
		ui->CountDonwTimer->setEnabled(true);

		// 他のボタンの状態
		ui->TimerOnlyBtn->setChecked(false);
		ui->CowntDownOnlyBtn->setChecked(false);

		this->SaveOverlayData();
	});

	// タイマー Start
	connect(ui->timerStartButton, &QPushButton::clicked, this, [this]() {
		timer->start(1000);
		ui->timerStartButton->setEnabled(false);
		ui->timerStopButton->setEnabled(true);
		ui->timeResetButton->setEnabled(false);
		ui->textDone->setEnabled(false);
		this->overlayColors["CountUpTimer"] = this->timerColor;
		this->SaveOverlayData();
	});

	// タイマー Stop
	connect(ui->timerStopButton, &QPushButton::clicked, this, [this]() {
		timer->stop();
		ui->timerStartButton->setEnabled(true);
		ui->timeResetButton->setEnabled(true);
		this->overlayColors["CountUpTimer"] = this->timerStopColor;
		this->SaveOverlayData();
	});

	// タイマー Reset
	connect(ui->timeResetButton, &QPushButton::clicked, this, [this]() {
		ui->CountUpTimer->setTime(QTime(0, 0, 0));
		ui->CountDonwTimer->setTime(this->initCountDownTimer);
		ui->timerStopButton->setEnabled(false);
		ui->timerStartButton->setEnabled(true);
		ui->textDone->setEnabled(true);
		this->overlayColors["CountUpTimer"] = this->timerColor;
		this->SaveOverlayData();
	});

	// スケジュールJSONファイル選択
	connect(ui->scheduleFileSelect, &QPushButton::clicked, this, &RTAPluginDock::loadAndParseJsonFile);

	// スケジュール適用ボタン
	connect(ui->applyButton, &QPushButton::clicked, this, &RTAPluginDock::onApplyScheduleClicked);

	// スケジュールリスト選択変更
	connect(ui->gameSelectBox, &QComboBox::currentTextChanged, this, [this](const QString &text) {
		for (const auto &data : this->currentGameData) {
			if (data.gameTitle == text) {
				ui->GameTitleText->setText(data.gameTitle);
				QStringList runners = data.runnerName.split(",");
				ui->RunnerText_1->setText(runners.size() > 0 ? runners[0] : "");
				ui->RunnerText_2->setText(runners.size() > 1 ? runners[1] : "");
				ui->RunnerText_3->setText(runners.size() > 2 ? runners[2] : "");
				ui->RunnerText_4->setText(runners.size() > 3 ? runners[3] : "");
				ui->EstimateTime->setTime(QTime::fromMSecsSinceStartOfDay(data.estimateTime * 1000));
				ui->CategoryText->setText(data.category);
				ui->HardwareText->setText(data.hardware);
				break;
			}
		}
	});

	// 設定：フォント
	connect(ui->fontSetting, &QPushButton::clicked, this, &RTAPluginDock::onFontChangeButtonClicked);

	// 設定：テキストベースカラー
	connect(ui->TextBaseColorChangeBtn, &QPushButton::clicked, this, [this]() {
		QColor color = QColorDialog::getColor(Qt::white, this, "ベースカラーの選択");
		if (color.isValid()) {
			this->overlayColors["TextBase"] = (color.blue() << 16) | (color.green() << 8) | color.red();
			ui->TextBaseColorFrame->setStyleSheet("background-color: " + color.name());
			this->SaveOverlayData();
		}
	});

	// 設定：タイマーカラー
	connect(ui->TimerColorChangeBtn, &QPushButton::clicked, this, [this]() {
		QColor color = QColorDialog::getColor(Qt::white, this, "タイマー稼働色の選択");
		if (color.isValid()) {
			this->timerColor = (color.blue() << 16) | (color.green() << 8) | color.red();
			ui->TimerColorFrame->setStyleSheet("background-color: " + color.name());
			this->SaveOverlayData();
		}
	});

	// 設定：タイマーストップカラー
	connect(ui->TimerStopColorChangeBtn, &QPushButton::clicked, this, [this]() {
		QColor color = QColorDialog::getColor(Qt::white, this, "タイマーストップ色の選択");
		if (color.isValid()) {
			this->timerStopColor = (color.blue() << 16) | (color.green() << 8) | color.red();
			ui->TimerStopColorFrame->setStyleSheet("background-color: " + color.name());
			this->SaveOverlayData();
		}
	});

	// 設定：アウトライン
	connect(ui->outlineCheckBox, &QCheckBox::checkStateChanged, this, [this](int state) {
		this->outlineEnabled = (state == Qt::Checked);
		this->SaveOverlayData();
	});

	// 走者タイマーストップボタン
	connect(ui->timerStopButton_P1, &QPushButton::clicked, this, [this]() {
		overlayValues["RunnerTimer_1"] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	});
	connect(ui->timerStopButton_P2, &QPushButton::clicked, this, [this]() {
		overlayValues["RunnerTimer_2"] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	});
	connect(ui->timerStopButton_P3, &QPushButton::clicked, this, [this]() {
		overlayValues["RunnerTimer_3"] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	});
	connect(ui->timerStopButton_P4, &QPushButton::clicked, this, [this]() {
		overlayValues["RunnerTimer_4"] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	});

	// 配置情報の保存
	connect(ui->SavePosSettingBtn, &QPushButton::clicked, this, &RTAPluginDock::onSavePosSettingClicked);
	// 配置情報の読み込み
	connect(ui->LoadPosSettingBtn, &QPushButton::clicked, this, &RTAPluginDock::onLoadPosSettingClicked);

	// 配置情報のシグナルを一括で接続
	SetupPositionSignals();

	// TextComboxの初期化
	ui->textSelectBox->clear();

	// textSelectBoxにtextElementMapのキーを全て追加
	for (const auto &pair : textElementMap) {
		ui->textSelectBox->addItem(pair.first);
	}
}

RTAPluginDock::~RTAPluginDock()
{
	delete this->posUpdateTimer;
	delete ui;
}

/**
 * @brief 配置情報の信号を一括で接続します。
 */
void RTAPluginDock::SetupPositionSignals()
{
	// タイマーの初期化（未作成の場合）
	if (!this->posUpdateTimer) {
		this->posUpdateTimer = new QTimer(this);
		this->posUpdateTimer->setSingleShot(true);
		this->posUpdateTimer->setInterval(30);
		connect(this->posUpdateTimer, &QTimer::timeout, this, &RTAPluginDock::SaveOverlayData);
	}

	// すべての QDoubleSpinBox を自動接続
	QList<QDoubleSpinBox *> allDoubleSpinBoxes = this->findChildren<QDoubleSpinBox *>();
	for (QDoubleSpinBox *sb : allDoubleSpinBoxes) {
		QString name = sb->objectName();
		if (name.startsWith("posX_") || name.startsWith("posY_")) {

			// --- 数値の上限・下限と精度をコード側で一括設定 ---
			sb->setRange(-10000.0, 10000.0); // 座標として十分な範囲を設定
			sb->setDecimals(2);              // 小数点以下2桁を表示
			sb->setSingleStep(1.0);          // 上下ボタンでの変化量を1.0に設定

			// 引数の型を double に合わせる（またはラムダの引数を省略する）
			connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { this->posUpdateTimer->start(); });
		}
	}
}

/**
 * @brief データをJSON出力し、同時にOBSのブラウザソースのCSSも更新します。
 */
void RTAPluginDock::SaveOverlayData()
{
	// 1. テキストデータの収集
	overlayValues["GameTitle"] = ui->GameTitleText->text();
	overlayValues["RunnerName_1"] = ui->RunnerText_1->text();
	overlayValues["RunnerName_2"] = ui->RunnerText_2->text();
	overlayValues["RunnerName_3"] = ui->RunnerText_3->text();
	overlayValues["RunnerName_4"] = ui->RunnerText_4->text();
	overlayValues["Category"] = ui->CategoryText->text();
	overlayValues["Hardware"] = ui->HardwareText->text();
	overlayValues["Commentary"] = ui->CommentaryText->text();
	overlayValues["EstimateTime"] = ui->EstimateTime->time().toString("HH:mm:ss");
	overlayValues["CountUpTimer"] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	overlayValues["CountDownTimer"] = ui->CowntDownOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	QJsonObject root;
	QJsonObject valuesObj;
	for (auto it = overlayValues.begin(); it != overlayValues.end(); ++it) {
		valuesObj.insert(it.key(), it.value());
	}
	root.insert("values", valuesObj);

	// 2. スタイル・座標情報の収集
	QJsonObject styleObj;
	styleObj.insert("fontFamily", currentFont.family());
	styleObj.insert("fontSize", currentFont.pointSize());
	styleObj.insert("baseColor", QColor(overlayColors["TextBase"]).name());
	styleObj.insert("timerColor", QColor(overlayColors["CountUpTimer"]).name());
	root.insert("style", styleObj);

	QJsonObject transforms;
	auto createPosObj = [](double x, double y) {
		QJsonObject obj;
		obj.insert("x", x);
		obj.insert("y", y);
		return obj;
	};

	// UIからの座標取得（DoubleSpinBoxに対応）
	transforms.insert("GameTitle", createPosObj(ui->posX_GameTitle->value(), ui->posY_GameTitle->value()));
	transforms.insert("Category", createPosObj(ui->posX_Category->value(), ui->posY_Category->value()));
	transforms.insert("Hardware", createPosObj(ui->posX_Hardware->value(), ui->posY_Hardware->value()));
	transforms.insert("EstimateTime", createPosObj(ui->posX_EstimateTime->value(), ui->posY_EstimateTime->value()));
	transforms.insert("CountUpTimer", createPosObj(ui->posX_CountUpTimer->value(), ui->posY_CountUpTimer->value()));
	transforms.insert("CountDownTimer",
			  createPosObj(ui->posX_CountDownTimer->value(), ui->posY_CountDownTimer->value()));

	transforms.insert("RunnerTimer_1",
			  createPosObj(ui->posX_RunnerTimer_1->value(), ui->posY_RunnerTimer_1->value()));
	transforms.insert("RunnerTimer_2",
			  createPosObj(ui->posX_RunnerTimer_2->value(), ui->posY_RunnerTimer_2->value()));
	transforms.insert("RunnerTimer_3",
			  createPosObj(ui->posX_RunnerTimer_3->value(), ui->posY_RunnerTimer_3->value()));
	transforms.insert("RunnerTimer_4",
			  createPosObj(ui->posX_RunnerTimer_4->value(), ui->posY_RunnerTimer_4->value()));

	transforms.insert("RunnerName_1", createPosObj(ui->posX_RunnerName_1->value(), ui->posY_RunnerName_1->value()));
	transforms.insert("RunnerName_2", createPosObj(ui->posX_RunnerName_2->value(), ui->posY_RunnerName_2->value()));
	transforms.insert("RunnerName_3", createPosObj(ui->posX_RunnerName_3->value(), ui->posY_RunnerName_3->value()));
	transforms.insert("RunnerName_4", createPosObj(ui->posX_RunnerName_4->value(), ui->posY_RunnerName_4->value()));

	transforms.insert("Commentary", createPosObj(ui->posX_Commentary->value(), ui->posY_Commentary->value()));

	//transforms.insert("CommentaryFlex", isFlex);


	root.insert("transforms", transforms);

	// 3. JSONファイルへの書き出し
	char *dataPathC = obs_module_get_config_path(obs_current_module(), "");
	QString dataPath = QString::fromUtf8(dataPathC);
	bfree(dataPathC);
	QDir().mkpath(dataPath);

	QFile file(dataPath + "/overlay_data.json");
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(QJsonDocument(root).toJson());
		file.close();
	}


	//// 5. OBSブラウザソースのCSS更新（Flex対応ロジックを追加）
	//obs_source_t *source = obs_get_source_by_name("RTA_Overlay");
	//if (source) {
	//	QString css = "/* Auto-generated by RTA Support Plugin */\n";
	//	css += "body { margin: 0; padding: 0; overflow: hidden; background-color: transparent; }\n";
	//	css += ".overlay-item { position: absolute; }\n"; // 基本は絶対配置

	//	// Flex用コンテナのスタイル（必要に応じてHTML構造に合わせてIDを変更してください）
	//	if (isFlex) {
	//		css += "#RunnerContainer { display: flex; gap: 10px; align-items: center; position: absolute; }\n";
	//	}

	//	for (auto it = transforms.begin(); it != transforms.end(); ++it) {
	//		if (it.value().isObject()) {
	//			QString id = it.key();
	//			QJsonObject pos = it.value().toObject();

	//			// Flex有効時の例外処理: Commentary は absolute を解除する
	//			if (isFlex && id == "Commentary") {
	//				css += QString("#%1 { position: static !important; }\n").arg(id);
	//			} else {
	//				css += QString("#%1 { left: %2px; top: %3px; position: absolute; }\n")
	//					       .arg(id)
	//					       .arg(pos["x"].toDouble())
	//					       .arg(pos["y"].toDouble());
	//			}
	//		}
	//	}

	//	obs_data_t *settings = obs_source_get_settings(source);
	//	obs_data_set_string(settings, "css", css.toUtf8().constData());
	//	obs_source_update(source, settings);
	//	obs_data_release(settings);
	//	obs_source_release(source);
	//}
}

/**
 * @brief 名前を付けて保存
 */
void RTAPluginDock::onSavePosSettingClicked()
{
	// OBSのプラグイン設定フォルダを初期ディレクトリとして取得
	char *dataPathC = obs_module_get_config_path(obs_current_module(), "");
	QString dataPath = QString::fromUtf8(dataPathC);
	bfree(dataPathC);

	// フォルダが存在しない場合は作成
	QDir().mkpath(dataPath);

	QString fileName = QFileDialog::getSaveFileName(this, "座標設定を保存",
							dataPath, // 初期ディレクトリをプラグイン設定フォルダに変更
							"JSON Files (*.json)");

	if (fileName.isEmpty())
		return;

	QJsonObject transforms;
	auto allSpinBoxes = this->findChildren<QDoubleSpinBox *>();
	for (QDoubleSpinBox *sb : allSpinBoxes) {
		if (sb->objectName().contains("posX_") || sb->objectName().contains("posY_")) {
			transforms.insert(sb->objectName(), sb->value());
		}
	}

	QFile file(fileName);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(QJsonDocument(transforms).toJson());
		file.close();
		QMessageBox::information(this, "成功", "保存しました。");
	}
}

/**
 * @brief 設定の読み込み
 */
void RTAPluginDock::onLoadPosSettingClicked()
{
	// OBSのプラグイン設定フォルダを初期ディレクトリとして取得
	char *dataPathC = obs_module_get_config_path(obs_current_module(), "");
	QString dataPath = QString::fromUtf8(dataPathC);
	bfree(dataPathC);

	QString fileName = QFileDialog::getOpenFileName(this, "座標設定を読み込み",
							dataPath, // 初期ディレクトリをプラグイン設定フォルダに変更
							"JSON Files (*.json)");

	if (fileName.isEmpty())
		return;

	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;

	QJsonObject data = QJsonDocument::fromJson(file.readAll()).object();
	file.close();

	auto allSpinBoxes = this->findChildren<QDoubleSpinBox *>();
	for (QDoubleSpinBox *sb : allSpinBoxes) {
		if (data.contains(sb->objectName())) {
			sb->setValue(data[sb->objectName()].toDouble());
		}
	}

	this->SaveOverlayData();
}

void RTAPluginDock::onUpdateDoneButtonClicked()
{
	this->initCountDownTimer = ui->CountDonwTimer->time();
	SaveOverlayData();
	QMessageBox::information(this, "完了", "設定をオーバーレイに反映しました。");
}

void RTAPluginDock::loadAndParseJsonFile()
{
	QString filePath =
		QFileDialog::getOpenFileName(this, "JSONファイルを選択", ".", "JSON Files (*.json);;All Files (*)");
	if (filePath.isEmpty())
		return;

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QMessageBox::warning(this, "エラー", "ファイルを開けませんでした。");
		return;
	}

	QJsonDocument jsonDoc = QJsonDocument::fromJson(file.readAll());
	file.close();

	if (jsonDoc.isNull() || !jsonDoc.isObject()) {
		QMessageBox::warning(this, "エラー", "JSONのパースに失敗しました。");
		return;
	}

	this->scheduleData = jsonDoc.object();

	// カラム名の取得
	std::vector<std::string> columnNames;
	if (this->scheduleData.contains("schedule")) {
		auto scheduleObj = this->scheduleData["schedule"].toObject();
		if (scheduleObj.contains("columns")) {
			QJsonArray columns = scheduleObj["columns"].toArray();
			for (const QJsonValue &value : columns) {
				columnNames.push_back(value.toString().toStdString());
			}
		}
	}

	// UIのコンボボックスをリセット
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

	// カラムの自動検出キーワード
	const std::vector<QString> gameKeywords = {"Game", "Title", "ゲーム", "タイトル"};
	const std::vector<QString> runnerKeywords = {"Runner", "Player", "走者", "プレイヤー"};
	const std::vector<QString> categoryKeywords = {"Category", "カテゴリ"};
	const std::vector<QString> hardwareKeywords = {"Hardware", "Hard", "機種", "機材"};

	int gi = findColumnIndexByKeywords(columnNames, gameKeywords);
	int ri = findColumnIndexByKeywords(columnNames, runnerKeywords);
	int ci = findColumnIndexByKeywords(columnNames, categoryKeywords);
	int hi = findColumnIndexByKeywords(columnNames, hardwareKeywords);

	if (gi != -1)
		ui->FormatGameTitleBox->setCurrentIndex(gi);
	if (ri != -1)
		ui->FormatRunnerBox->setCurrentIndex(ri);
	if (ci != -1)
		ui->FormatCategoryBox->setCurrentIndex(ci);
	if (hi != -1)
		ui->FormatHardwareBox->setCurrentIndex(hi);

	QMessageBox::information(this, "成功", "スケジュールの読み込みが完了しました。");
}

void RTAPluginDock::onApplyScheduleClicked()
{
	ui->gameSelectBox->clear();
	this->currentGameData.clear();

	if (!this->scheduleData.contains("schedule"))
		return;
	auto scheduleObj = this->scheduleData["schedule"].toObject();
	if (!scheduleObj.contains("items"))
		return;
	auto itemsArray = scheduleObj["items"].toArray();

	int gi = ui->FormatGameTitleBox->currentIndex();
	int ri = ui->FormatRunnerBox->currentIndex();
	int ci = ui->FormatCategoryBox->currentIndex();
	int hi = ui->FormatHardwareBox->currentIndex();

	for (const QJsonValue &value : itemsArray) {
		QJsonObject itemObj = value.toObject();
		auto dataArray = itemObj["data"].toArray();

		GameData data;
		data.gameTitle = dataArray[gi].toString();
		data.runnerName = dataArray[ri].toString();
		data.category = dataArray[ci].toString();
		data.hardware = dataArray[hi].toString();
		data.estimateTime = itemObj["length_t"].toInt();

		this->currentGameData.push_back(data);
		ui->gameSelectBox->addItem(data.gameTitle);
	}

	SaveOverlayData();
}

void RTAPluginDock::onFontChangeButtonClicked()
{
	bool ok;
	QFont font = QFontDialog::getFont(&ok, currentFont, this, "フォントを選択");
	if (ok) {
		currentFont = font;
		ui->FontLabel->setText(currentFont.family());
		ui->FontSizeLabel->setText(QString::number(currentFont.pointSize()));
		SaveOverlayData();
	}
}