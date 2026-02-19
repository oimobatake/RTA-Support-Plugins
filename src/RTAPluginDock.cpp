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
#include <QDoubleSpinBox>

// UI上の表示名（日本語）とプログラム内部のID（英語）を紐付けるマップ
static const std::map<QString, QString> textElementMap =
{
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

// スケジュール解析用ヘルパー
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

RTAPluginDock::RTAPluginDock(QWidget *parent)
	: QWidget(parent),
	  ui(new Ui::RTAPluginDock),
	  timer(nullptr),
	  posUpdateTimer(nullptr) // 必ずnullptrで初期化してクラッシュを防ぐ
{
	ui->setupUi(this);

	// タイマー初期化
	timer = new QTimer(this);
	ui->timerStopButton->setEnabled(false);
	ui->TimerOnlyBtn->setChecked(true);

	// デフォルトフォント
	currentFont = QFont("Arial", 48);

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

		overlayValues["RunnerTimer_1"] = "";
		overlayValues["RunnerTimer_2"] = "";
		overlayValues["RunnerTimer_3"] = "";
		overlayValues["RunnerTimer_4"] = "";

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

	// 走者タイマーストップボタン
	connect(ui->timerStopButton_P1, &QPushButton::clicked, this, [this]() {
		overlayValues["RunnerTimer_1"] =
			ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	});
	connect(ui->timerStopButton_P2, &QPushButton::clicked, this, [this]() {
		overlayValues["RunnerTimer_2"] =
			ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	});
	connect(ui->timerStopButton_P3, &QPushButton::clicked, this, [this]() {
		overlayValues["RunnerTimer_3"] =
			ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	});
	connect(ui->timerStopButton_P4, &QPushButton::clicked, this, [this]() {
		overlayValues["RunnerTimer_4"] =
			ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
	});

	// 配置情報の保存
	connect(ui->SavePosSettingBtn, &QPushButton::clicked, this, &RTAPluginDock::onSavePosSettingClicked);
	// 配置情報の読み込み
	connect(ui->LoadPosSettingBtn, &QPushButton::clicked, this, &RTAPluginDock::onLoadPosSettingClicked);

	// 配置情報のシグナルを一括で接続
	SetupPositionSignals();

	SetupStyleSignals();
}

RTAPluginDock::~RTAPluginDock()
{
	// posUpdateTimerは親(this)を持っているので自動削除されますが、明示的なdeleteも安全です
	delete ui;
}

void RTAPluginDock::SetupPositionSignals()
{
	// 1. 全要素の初期座標を 0, 0 で初期化
	for (auto const &[name, id] : textElementMap) {
		if (posList.find(id) == posList.end()) {
			posList[id] = QPointF(0.0, 0.0);
		}
	}

	// 2. 編集用SpinBoxの設定
	ui->posX_text->setRange(-10000.0, 10000.0);
	ui->posY_text->setRange(-10000.0, 10000.0);
	ui->posX_text->setDecimals(2);
	ui->posY_text->setDecimals(2);

	// 3. コンボボックスの選択が切り替わった時、SpinBoxに値を反映させる
	connect(ui->textSelectBox, &QComboBox::currentTextChanged, this, [this](const QString &name) {
		QString id = textElementMap.at(name);
		QPointF currentPos = posList[id];

		// 信号のループを防ぐため一時的にブロック
		ui->posX_text->blockSignals(true);
		ui->posY_text->blockSignals(true);

		ui->posX_text->setValue(currentPos.x());
		ui->posY_text->setValue(currentPos.y());

		ui->posX_text->blockSignals(false);
		ui->posY_text->blockSignals(false);
	});

	// 4. 編集用SpinBoxの値が変わった時、mapの値を更新して保存する
	auto onPosEdited = [this](double) {
        QString displayName = ui->textSelectBox->currentText();
        if (textElementMap.count(displayName)) {
            QString id = textElementMap.at(displayName);
            posList[id] = QPointF(ui->posX_text->value(), ui->posY_text->value());
            if (posUpdateTimer) posUpdateTimer->start();

			this->SaveOverlayData();
        }
    };

	connect(ui->posX_text, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onPosEdited);
	connect(ui->posY_text, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onPosEdited);
}

void RTAPluginDock::SetupStyleSignals()
{
	// 初期化
	QFont defaultFont("Arial", 48);
	for (auto const &[name, id] : textElementMap) {
		fontList[id] = defaultFont;
		colorList[id] = "#FFFFFF";
		alignList[id] = "center"; // デフォルトは中央揃え
	}

	// 対象選択ComboBox
	ui->textSelectBox->clear();
	for (auto const &[name, id] : textElementMap)
		ui->textSelectBox->addItem(name);

	// アライメント選択ComboBox (UI側に textAlignBox がある想定)
	ui->textAlignBox->clear();
	ui->textAlignBox->addItem("左揃え", "left");
	ui->textAlignBox->addItem("中央揃え", "center");
	ui->textAlignBox->addItem("右揃え", "right");

	// 対象要素が切り替わったら、現在のアライメント値をComboBoxに反映
	connect(ui->textSelectBox, &QComboBox::currentTextChanged, this, [this](const QString &name) {
		QString id = textElementMap.at(name);
		int idx = ui->textAlignBox->findData(alignList[id]);
		ui->textAlignBox->setCurrentIndex(idx);
	});

	// アライメント変更時の接続
	connect(ui->textAlignBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		alignList[id] = ui->textAlignBox->itemData(index).toString();
		this->SaveOverlayData();
	});

	connect(ui->fontSetting, &QPushButton::clicked, this, &RTAPluginDock::onFontChangeButtonClicked);

	connect(ui->StyleColorBtn, &QPushButton::clicked, this, [this]() {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		QColor color = QColorDialog::getColor(QColor(colorList[id]), this, "色選択");
		if (color.isValid()) {
			colorList[id] = color.name();
			this->SaveOverlayData();
		}
	});

	// 設定：タイマーストップカラー
	connect(ui->TimerStopColorChangeBtn, &QPushButton::clicked, this, [this]() {
		QColor color = QColorDialog::getColor(QColor::fromRgb(timerStopColor), this, "停止時カラー選択");
		if (color.isValid()) {
			this->timerStopColor = (color.blue() << 16) | (color.green() << 8) | color.red();
			this->SaveOverlayData();
		}
	});

	// 設定：アウトライン
	connect(ui->outlineCheckBox, &QCheckBox::checkStateChanged, this, [this](int state) {
		this->outlineEnabled = (state == Qt::Checked);
		this->SaveOverlayData();
	});
}

void RTAPluginDock::SaveOverlayData()
{
	QJsonObject root;

	// values
	QJsonObject valObj;
	valObj.insert("GameTitle", ui->GameTitleText->text());
	valObj.insert("RunnerName_1", ui->RunnerText_1->text());
	valObj.insert("RunnerName_2", ui->RunnerText_2->text());
	valObj.insert("RunnerName_3", ui->RunnerText_3->text());
	valObj.insert("RunnerName_4", ui->RunnerText_4->text());
	valObj.insert("Category", ui->CategoryText->text());
	valObj.insert("Hardware", ui->HardwareText->text());
	valObj.insert("Commentary", ui->CommentaryText->text());
	valObj.insert("EstimateTime", ui->EstimateTime->time().toString("HH:mm:ss"));
	valObj.insert("CountUpTimer", ui->CountUpTimer->time().toString("HH:mm:ss"));
	valObj.insert("CountDownTimer", ui->CountDonwTimer->time().toString("HH:mm:ss"));
	root.insert("values", valObj);

	// styles
	bool running = (timer && timer->isActive());
	QJsonObject styleObj;
	for (auto const &[id, font] : fontList) {
		QJsonObject s;
		s.insert("fontFamily", font.family());
		s.insert("fontSize", font.pointSize());
		s.insert("isBold", font.bold());
		s.insert("isItalic", font.italic());
		s.insert("color", colorList[id]);
		s.insert("textAlign", alignList[id]);
		if (id.contains("Timer")) {
			s.insert("isRunning", running);
			s.insert("stopColor", QColor(QRgb(timerStopColor)).name());
		}
		styleObj.insert(id, s);
	}
	root.insert("styles", styleObj);

	// transforms (posListから生成、正しいID形式にする)
	QJsonObject transObj;
	for (auto const &[id, pos] : posList) {
		transObj.insert("posX_" + id, pos.x());
		transObj.insert("posY_" + id, pos.y());
	}
	root.insert("transforms", transObj);

	// 保存実行
	char *pathC = obs_module_get_config_path(obs_current_module(), "");
	QString path = QString::fromUtf8(pathC);
	bfree(pathC);
	QDir().mkpath(path);
	QFile file(path + "/overlay_data.json");
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(QJsonDocument(root).toJson());
		file.close();
	}
}

/**
 * @brief OBSソース「RTA_Overlay」のCSS設定を更新する。レイアウト変更時のみ実行。
 */
void RTAPluginDock::UpdateObsSourceStyle()
{
	obs_source_t *src = obs_get_source_by_name("RTA_Overlay");
	if (!src)
		return;

	bool running = (timer && timer->isActive());
	QString css = "body { margin: 0; padding: 0; overflow: hidden; background-color: transparent; }\n";
	css += ".overlay-item { position: absolute; }\n";

	for (auto const &[id, font] : fontList) {
		double x = posList[id].x();
		double y = posList[id].y();
		QString align = alignList[id];
		QString transform = (align == "center") ? "translateX(-50%)"
							: (align == "right" ? "translateX(-100%)" : "none");

		QString color = colorList[id];
		if (id.contains("Timer") && !running)
			color = QColor(QRgb(timerStopColor)).name();

		css += QString("#%1 { left: %2px; top: %3px; transform: %4; text-align: %5; color: %6; ")
			       .arg(id)
			       .arg(x)
			       .arg(y)
			       .arg(transform)
			       .arg(align)
			       .arg(color);
		css += QString("font-family: '%1'; font-size: %2px; %3 %4 }\n")
			       .arg(font.family())
			       .arg(font.pointSize())
			       .arg(font.bold() ? "font-weight: bold;" : "")
			       .arg(font.italic() ? "font-style: italic;" : "");
	}

	obs_data_t *settings = obs_source_get_settings(src);
	obs_data_set_string(settings, "css", css.toUtf8().constData());
	obs_source_update(src, settings);
	obs_data_release(settings);
	obs_source_release(src);
}

void RTAPluginDock::onSavePosSettingClicked()
{
	char *pathC = obs_module_get_config_path(obs_current_module(), "");
	QString path = QString::fromUtf8(pathC);
	bfree(pathC);
	QString fileName = QFileDialog::getSaveFileName(this, "設定保存", path, "JSON (*.json)");
	if (fileName.isEmpty())
		return;

	QJsonObject exportObj;
	QJsonObject trans;
	for (auto const &[id, pos] : posList) {
		trans.insert("posX_" + id, pos.x());
		trans.insert("posY_" + id, pos.y());
	}
	exportObj.insert("transforms", trans);

	QJsonObject styles;
	for (auto const &[id, font] : fontList) {
		QJsonObject s;
		s.insert("family", font.family());
		s.insert("size", font.pointSize());
		s.insert("bold", font.bold());
		s.insert("italic", font.italic());
		s.insert("color", colorList[id]);
		s.insert("align", alignList[id]);
		styles.insert(id, s);
	}
	exportObj.insert("styles", styles);

	QFile file(fileName);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(QJsonDocument(exportObj).toJson());
		file.close();
	}
}

void RTAPluginDock::onLoadPosSettingClicked()
{
	char *pathC = obs_module_get_config_path(obs_current_module(), "");
	QString path = QString::fromUtf8(pathC);
	bfree(pathC);
	QString fileName = QFileDialog::getOpenFileName(this, "設定読込", path, "JSON (*.json)");
	if (fileName.isEmpty())
		return;

	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QJsonObject data = QJsonDocument::fromJson(file.readAll()).object();

	if (data.contains("transforms")) {
		QJsonObject trans = data["transforms"].toObject();
		for (auto const &[name, id] : textElementMap) {
			if (trans.contains("posX_" + id)) {
				posList[id] = QPointF(trans["posX_" + id].toDouble(), trans["posY_" + id].toDouble());
			}
		}
		emit ui->textSelectBox->currentTextChanged(ui->textSelectBox->currentText());
	}
	if (data.contains("styles")) {
		QJsonObject styles = data["styles"].toObject();
		for (auto it = styles.begin(); it != styles.end(); ++it) {
			QJsonObject s = it.value().toObject();
			QString id = it.key();
			QFont f(s["family"].toString());
			f.setPointSize(s["size"].toInt());
			f.setBold(s["bold"].toBool());
			f.setItalic(s["italic"].toBool());
			fontList[id] = f;
			colorList[id] = s["color"].toString();
			alignList[id] = s["align"].toString();
		}
	}
	this->SaveOverlayData();
	this->UpdateObsSourceStyle();
}

void RTAPluginDock::onFontChangeButtonClicked()
{
	QString id = textElementMap.at(ui->textSelectBox->currentText());
	bool ok;
	QFont font = QFontDialog::getFont(&ok, fontList[id], this, "フォント選択");
	if (ok) {
		fontList[id] = font;
		this->SaveOverlayData();
		this->UpdateObsSourceStyle();
	}
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

void RTAPluginDock::onUpdateDoneButtonClicked()
{
	this->initCountDownTimer = ui->CountDonwTimer->time();
	SaveOverlayData();
	//QMessageBox::information(this, "完了", "設定をオーバーレイに反映しました。");
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