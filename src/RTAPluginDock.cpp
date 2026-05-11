#include "RTAPluginDock.h"
#include "ui_RTAPluginDock.h"
#include "OverlayEditor.h"

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
#include <QInputDialog>

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
	{"解説者", "Commentator"},

	{"次ゲーム1: タイトル", "NextGameTitle_1"},
	{"次ゲーム1: 走者", "NextRunnerName_1"},
	{"次ゲーム1: カテゴリ", "NextCategory_1"},
	{"次ゲーム1: ハード", "NextHardware_1"},
	{"次ゲーム1: 予定時間", "NextEstimate_1"},

	{"次ゲーム2: タイトル", "NextGameTitle_2"},
	{"次ゲーム2: 走者", "NextRunnerName_2"},
	{"次ゲーム2: カテゴリ", "NextCategory_2"},
	{"次ゲーム2: ハード", "NextHardware_2"},
	{"次ゲーム2: 予定時間", "NextEstimate_2"},

	{"次ゲーム3: タイトル", "NextGameTitle_3"},
	{"次ゲーム3: 走者", "NextRunnerName_3"},
	{"次ゲーム3: カテゴリ", "NextCategory_3"},
	{"次ゲーム3: ハード", "NextHardware_3"},
	{"次ゲーム3: 予定時間", "NextEstimate_3"}
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
	  posUpdateTimer(nullptr)
{
	ui->setupUi(this);

	// 非同期更新用タイマー
	posUpdateTimer = new QTimer(this);
	posUpdateTimer->setSingleShot(true);
	posUpdateTimer->setInterval(100);

	// タブ別初期化の実行
	InitCommon();
	InitMainTab();
	InitScheduleTab();
	InitDesignTab();
	InitGlobalTab();

	// 永続化データのロード
	LoadOverlayData();

	// 初回UI同期
	emit ui->textSelectBox->currentTextChanged(ui->textSelectBox->currentText());
}

RTAPluginDock::~RTAPluginDock()
{
	delete ui;
}

void RTAPluginDock::InitCommon()
{
	timer = new QTimer(this);
	currentFont = QFont("Arial", 48);

	// 共通ボタン（タブ外）
	connect(ui->ScreenShotBtn, &QPushButton::clicked, this, []() { obs_frontend_take_screenshot(); });
}

void RTAPluginDock::InitMainTab()
{
	ui->timerStopButton->setEnabled(false);
	ui->TimerOnlyBtn->setChecked(true);
	ui->CountUpTimer->setTime(QTime(0, 0, 0));

	// 次へボタンにアイコン設定
	//ui->nextGameBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));

	// タイマー定期実行
	connect(timer, &QTimer::timeout, this, [this]() {
		if (ui->CowntDownOnlyBtn->isChecked() || ui->BothBtn->isChecked()) {
			QTime t = ui->CountDonwTimer->time();
			if (t != QTime(0, 0, 0))
				ui->CountDonwTimer->setTime(t.addSecs(-1));
		}
		if (ui->TimerOnlyBtn->isChecked() || ui->BothBtn->isChecked()) {
			ui->CountUpTimer->setTime(ui->CountUpTimer->time().addSecs(1));
		}
		this->SaveOverlayData();
	});

	// 表示モード切替
	auto onModeChanged = [this]() {
		ui->CountUpTimer->setEnabled(ui->TimerOnlyBtn->isChecked() || ui->BothBtn->isChecked());
		ui->CountDonwTimer->setEnabled(ui->CowntDownOnlyBtn->isChecked() || ui->BothBtn->isChecked());
		this->SaveOverlayData();
	};
	connect(ui->TimerOnlyBtn, &QRadioButton::clicked, this, onModeChanged);
	connect(ui->CowntDownOnlyBtn, &QRadioButton::clicked, this, onModeChanged);
	connect(ui->BothBtn, &QRadioButton::clicked, this, onModeChanged);
	connect(ui->TimerNoneButton, &QRadioButton::clicked, this, onModeChanged);

	// タイマー制御
	connect(ui->timerStartButton, &QPushButton::clicked, this, [this]() {
		timer->start(1000);
		ui->timerStartButton->setEnabled(false);
		ui->timerStopButton->setEnabled(true);
		ui->timeResetButton->setEnabled(false);
		ui->textDone->setEnabled(false);
		ui->nextGameBtn->setEnabled(false);
		ui->prevGameBtn->setEnabled(false);
		this->timerState = "Running";
		this->SaveOverlayData();
		if (this->autoScreenShotOnStart)
			obs_frontend_take_screenshot();
	});

	connect(ui->timerStopButton, &QPushButton::clicked, this, [this]() {
		timer->stop();
		ui->timerStartButton->setEnabled(true);
		ui->timeResetButton->setEnabled(true);
		this->timerState = "Stopped";
		this->SaveOverlayData();
		if (this->autoScreenShotOnStop)
			obs_frontend_take_screenshot();
	});

	connect(ui->timeResetButton, &QPushButton::clicked, this, [this]() {
		ui->CountUpTimer->setTime(QTime(0, 0, 0));
		ui->CountDonwTimer->setTime(this->initCountDownTimer);
		ui->timerStopButton->setEnabled(false);
		ui->timerStartButton->setEnabled(true);
		ui->textDone->setEnabled(true);
		ui->nextGameBtn->setEnabled(true);
		ui->prevGameBtn->setEnabled(true);
		this->timerState = "Reset";
		for (int i = 0; i < 4; i++)
			this->runnerTimers[i] = "";
		this->SaveOverlayData();
	});

	// 走者個別ストップ
	connect(ui->timerStopButton_P1, &QPushButton::clicked, this, [this]() {
		this->runnerTimers[0] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss")
								      : "";
		this->SaveOverlayData();
	});
	connect(ui->timerStopButton_P2, &QPushButton::clicked, this, [this]() {
		this->runnerTimers[1] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss")
								      : "";
		this->SaveOverlayData();
	});
	connect(ui->timerStopButton_P3, &QPushButton::clicked, this, [this]() {
		this->runnerTimers[2] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss")
								      : "";
		this->SaveOverlayData();
	});
	connect(ui->timerStopButton_P4, &QPushButton::clicked, this, [this]() {
		this->runnerTimers[3] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss")
								      : "";
		this->SaveOverlayData();
	});

	// 設定確定
	connect(ui->textDone, &QPushButton::clicked, this, &RTAPluginDock::onUpdateDoneButtonClicked);

	// ゲーム選択プルダウン
	connect(ui->gameSelectBox, &QComboBox::currentTextChanged, this, [this](const QString &text) {
		for (const auto &data : this->currentGameData) {
			if (data.gameTitle == text) {
				ui->GameTitleText->setText(data.gameTitle);
				QStringList runners = data.runnerName.split(",");
				ui->RunnerText_1->setText(runners.size() > 0 ? runners[0].trimmed() : "");
				ui->RunnerText_2->setText(runners.size() > 1 ? runners[1].trimmed() : "");
				ui->RunnerText_3->setText(runners.size() > 2 ? runners[2].trimmed() : "");
				ui->RunnerText_4->setText(runners.size() > 3 ? runners[3].trimmed() : "");
				ui->EstimateTime->setTime(QTime::fromMSecsSinceStartOfDay(data.estimateTime * 1000));
				ui->CategoryText->setText(data.category);
				ui->HardwareText->setText(data.hardware);
				ui->CommentatorText->setText(data.commentator);
				break;
			}
		}
	});

	// 次ゲーム選択ボタン
	connect(ui->nextGameBtn, &QToolButton::clicked, this, [this]() {
		int idx = ui->gameSelectBox->currentIndex();
		if (idx >= 0 && idx < ui->gameSelectBox->count() - 1) {
			ui->gameSelectBox->setCurrentIndex(idx + 1);
			onUpdateDoneButtonClicked();
		}
	});

	// 前ゲーム選択ボタン
	connect(ui->prevGameBtn, &QToolButton::clicked, this, [this]() {
		int idx = ui->gameSelectBox->currentIndex();
		if (idx > 0) {
			ui->gameSelectBox->setCurrentIndex(idx - 1);
			onUpdateDoneButtonClicked();
		}
	});
}

void RTAPluginDock::InitScheduleTab()
{
	// テーブル設定
	ui->scheduleTable->setColumnCount(6);
	ui->scheduleTable->setHorizontalHeaderLabels(
		{"GameTitle", "Runner", "Category", "Platform", "Estimate", "Commentator"});
	ui->scheduleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	ui->scheduleTable->horizontalHeader()->setDefaultSectionSize(120);
	ui->scheduleTable->horizontalHeader()->setMinimumSectionSize(80);
	ui->scheduleTable->horizontalHeader()->setStretchLastSection(true);

	connect(ui->scheduleFileSelect, &QPushButton::clicked, this, &RTAPluginDock::loadAndParseJsonFile);
	connect(ui->applyButton, &QPushButton::clicked, this, &RTAPluginDock::onApplyScheduleClicked);

	// テーブル編集
	connect(ui->addScheduleBtn, &QPushButton::clicked, this, [this]() {
		int row = std::max(0, ui->scheduleTable->currentRow() + 1);
		ui->scheduleTable->insertRow(row);
	});
	connect(ui->removeScheduleBtn, &QPushButton::clicked, this, [this]() {
		int row = ui->scheduleTable->currentRow();
		if (row >= 0)
			ui->scheduleTable->removeRow(row);
	});
	connect(ui->updateScheduleBtn, &QPushButton::clicked, this, [this]() {
		this->currentGameData.clear();
		ui->gameSelectBox->blockSignals(true);
		ui->gameSelectBox->clear();
		for (int i = 0; i < ui->scheduleTable->rowCount(); ++i) {
			GameData d;
			d.gameTitle = ui->scheduleTable->item(i, 0) ? ui->scheduleTable->item(i, 0)->text() : "";
			d.runnerName = ui->scheduleTable->item(i, 1) ? ui->scheduleTable->item(i, 1)->text() : "";
			d.category = ui->scheduleTable->item(i, 2) ? ui->scheduleTable->item(i, 2)->text() : "";
			d.hardware = ui->scheduleTable->item(i, 3) ? ui->scheduleTable->item(i, 3)->text() : "";
			QTime t = QTime::fromString(
				ui->scheduleTable->item(i, 4) ? ui->scheduleTable->item(i, 4)->text() : "00:00:00",
				"HH:mm:ss");
			d.estimateTime = t.isValid() ? (t.hour() * 3600 + t.minute() * 60 + t.second()) : 0;
			d.commentator = ui->scheduleTable->item(i, 5) ? ui->scheduleTable->item(i, 5)->text() : "";
			this->currentGameData.push_back(d);
			ui->gameSelectBox->addItem(d.gameTitle);
		}
		ui->gameSelectBox->blockSignals(false);
		if (ui->gameSelectBox->count() > 0)
			emit ui->gameSelectBox->currentTextChanged(ui->gameSelectBox->currentText());
		this->SaveOverlayData();
		QMessageBox::information(this, "成功", "スケジュールを反映しました。");
	});

	// エクスポート
	connect(ui->exportScheduleBtn, &QPushButton::clicked, this, [this]() {
		QString path = QFileDialog::getSaveFileName(this, "スケジュール保存", ".", "JSON (*.json)");
		if (path.isEmpty())
			return;
		QJsonObject root;
		QJsonObject sch;
		QJsonArray items;
		for (const auto &d : this->currentGameData) {
			QJsonObject item;
			item.insert("data",
				    QJsonArray({d.gameTitle, d.runnerName, d.category, d.hardware, d.commentator}));
			item.insert("length_t", d.estimateTime);
			items.append(item);
		}
		sch.insert("columns", QJsonArray({"GameTitle", "Runner", "Category", "Platform", "Commentator"}));
		sch.insert("items", items);
		root.insert("schedule", sch);
		QFile f(path);
		if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
			f.write(QJsonDocument(root).toJson());
			f.close();
		}
	});
}

void RTAPluginDock::InitDesignTab()
{
	// ComboBox初期化
	ui->textSelectBox->clear();
	for (auto const &[name, id] : textElementMap)
		ui->textSelectBox->addItem(name);
	ui->textAlignBox->clear();
	ui->textAlignBox->addItem("左揃え", "left");
	ui->textAlignBox->addItem("中央揃え", "center");
	ui->textAlignBox->addItem("右揃え", "right");
	ui->wrapModeBox->clear();
	ui->wrapModeBox->addItem("制限なし", "none");
	ui->wrapModeBox->addItem("自動縮小", "shrink");
	ui->wrapModeBox->addItem("自動改行", "wrap");

	ui->posX_text->setRange(-10000, 10000);
	ui->posY_text->setRange(-10000, 10000);

	// シーン管理
	connect(ui->layoutSelectBox, &QComboBox::currentTextChanged, this, [this](const QString &text) {
		if (text.isEmpty() || !layoutData.count(text))
			return;
		this->currentLayoutName = text;
		if (ui->syncSceneCheckBox && ui->syncSceneCheckBox->isChecked()) {
			obs_source_t *src = obs_get_source_by_name(text.toUtf8().constData());
			if (src) {
				if (obs_scene_from_source(src))
					obs_frontend_set_current_scene(src);
				obs_source_release(src);
			}
		}
		emit ui->textSelectBox->currentTextChanged(ui->textSelectBox->currentText());
		this->SaveOverlayData();
	});

	connect(ui->addLayoutBtn, &QPushButton::clicked, this, [this]() {
		bool ok;
		QString name = QInputDialog::getText(this, "追加", "シーン名:", QLineEdit::Normal, "", &ok);
		if (ok && !name.isEmpty() && !layoutData.count(name)) {
			layoutData[name] = layoutData[this->currentLayoutName];
			ui->layoutSelectBox->addItem(name);
			ui->layoutSelectBox->setCurrentText(name);
		}
	});

	connect(ui->removeLayoutBtn, &QPushButton::clicked, this, [this]() {
		if (layoutData.size() <= 1)
			return;
		if (QMessageBox::Yes == QMessageBox::question(this, "削除", "このレイアウトを削除しますか？")) {
			layoutData.erase(this->currentLayoutName);
			ui->layoutSelectBox->removeItem(ui->layoutSelectBox->currentIndex());
		}
	});

	// UI同期
	connect(ui->textSelectBox, &QComboBox::currentTextChanged, this, [this](const QString &name) {
		if (name.isEmpty() || !textElementMap.count(name))
			return;
		QString id = textElementMap.at(name);
		ElementData &ed = layoutData[this->currentLayoutName][id];

		ui->posX_text->blockSignals(true);
		ui->posY_text->blockSignals(true);
		ui->outlineCheckBox->blockSignals(true);
		ui->visibleCheckBox->blockSignals(true);
		ui->maxWidthSpinBox->blockSignals(true);
		ui->wrapModeBox->blockSignals(true);
		ui->outlineSize->blockSignals(true);

		ui->posX_text->setValue(ed.pos.x());
		ui->posY_text->setValue(ed.pos.y());
		ui->outlineCheckBox->setChecked(ed.outlineEnabled);
		ui->visibleCheckBox->setChecked(ed.isVisible);
		ui->textAlignBox->setCurrentIndex(ui->textAlignBox->findData(ed.align));
		ui->maxWidthSpinBox->setValue(ed.maxWidth);
		ui->wrapModeBox->setCurrentIndex(ui->wrapModeBox->findData(ed.wrapMode));
		ui->outlineSize->setValue(ed.outlineSize);

		ui->posX_text->blockSignals(false);
		ui->posY_text->blockSignals(false);
		ui->outlineCheckBox->blockSignals(false);
		ui->visibleCheckBox->blockSignals(false);
		ui->maxWidthSpinBox->blockSignals(false);
		ui->wrapModeBox->blockSignals(false);
		ui->outlineSize->blockSignals(false);
	});

	// 値変更
	auto save = [this]() {
		this->SaveOverlayData();
	};
	connect(ui->posX_text, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
		layoutData[this->currentLayoutName][textElementMap.at(ui->textSelectBox->currentText())].pos.setX(v);
		this->SaveOverlayData();
	});
	connect(ui->posY_text, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
		layoutData[this->currentLayoutName][textElementMap.at(ui->textSelectBox->currentText())].pos.setY(v);
		this->SaveOverlayData();
	});
	connect(ui->textAlignBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		layoutData[this->currentLayoutName][textElementMap.at(ui->textSelectBox->currentText())].align =
			ui->textAlignBox->itemData(i).toString();
		this->SaveOverlayData();
	});
	connect(ui->visibleCheckBox, &QCheckBox::stateChanged, this, [this](int s) {
		layoutData[this->currentLayoutName][textElementMap.at(ui->textSelectBox->currentText())].isVisible =
			(s == Qt::Checked);
		this->SaveOverlayData();
	});
	connect(ui->maxWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
		layoutData[this->currentLayoutName][textElementMap.at(ui->textSelectBox->currentText())].maxWidth = v;
		this->SaveOverlayData();
	});
	connect(ui->wrapModeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		layoutData[this->currentLayoutName][textElementMap.at(ui->textSelectBox->currentText())].wrapMode =
			ui->wrapModeBox->itemData(i).toString();
		this->SaveOverlayData();
	});
	connect(ui->outlineCheckBox, &QCheckBox::stateChanged, this, [this](int s) {
		layoutData[this->currentLayoutName][textElementMap.at(ui->textSelectBox->currentText())].outlineEnabled =
			(s == Qt::Checked);
		this->SaveOverlayData();
	});
	connect(ui->outlineSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
		layoutData[this->currentLayoutName][textElementMap.at(ui->textSelectBox->currentText())].outlineSize =
			v;
		this->SaveOverlayData();
	});

	// ダイアログ系
	connect(ui->fontSetting, &QPushButton::clicked, this, [this]() {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		bool ok;
		QFont f = QFontDialog::getFont(&ok, layoutData[this->currentLayoutName][id].font, this);
		if (ok) {
			layoutData[this->currentLayoutName][id].font = f;
			this->SaveOverlayData();
		}
	});
	connect(ui->StyleColorBtn, &QPushButton::clicked, this, [this]() {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		QColor c = QColorDialog::getColor(QColor(layoutData[this->currentLayoutName][id].color), this);
		if (c.isValid()) {
			layoutData[this->currentLayoutName][id].color = c.name();
			this->SaveOverlayData();
		}
	});
	connect(ui->outlineColorButton, &QPushButton::clicked, this, [this]() {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		QColor c = QColorDialog::getColor(QColor(layoutData[this->currentLayoutName][id].outlineColor), this);
		if (c.isValid()) {
			layoutData[this->currentLayoutName][id].outlineColor = c.name();
			this->SaveOverlayData();
		}
	});

	connect(ui->SavePosSettingBtn, &QPushButton::clicked, this, &RTAPluginDock::onSavePosSettingClicked);
	connect(ui->LoadPosSettingBtn, &QPushButton::clicked, this, &RTAPluginDock::onLoadPosSettingClicked);

	connect(ui->visualEditorBtn, &QPushButton::clicked, this, [this]() {
		// 現在選択中のレイアウト（main, setup等）のデータを渡してエディタを開く
		OverlayEditor editor(layoutData[currentLayoutName], this);

		if (editor.exec() == QDialog::Accepted) {
			// エディタからの編集結果（全要素の座標）を取得
			auto results = editor.getResult();

			// プラグイン内部のデータを更新
			for (auto const &[id, pos] : results) {
				layoutData[currentLayoutName][id].pos = pos;
			}

			// UIの表示（座標SpinBoxなど）を、今選択されている要素の値に同期させる
			emit ui->textSelectBox->currentTextChanged(ui->textSelectBox->currentText());

			// JSONに保存してOBS（ブラウザソース）に即時反映
			this->SaveOverlayData();

			// ステータスログ等（任意）
			// blog(LOG_INFO, "Visual Editor: Applied coordinates to layout '%s'", currentLayoutName.toUtf8().constData());
		}
	});
}

void RTAPluginDock::InitGlobalTab()
{
	connect(ui->TimerStopColorChangeBtn, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(QColor(timerStopColor), this);
		if (c.isValid()) {
			this->timerStopColor = c.name();
			this->SaveOverlayData();
		}
	});

	auto onIconToggled = [this](int) {
		this->showIcons[0] = ui->showRunner1IconCheckBox->isChecked();
		this->showIcons[1] = ui->showRunner2IconCheckBox->isChecked();
		this->showIcons[2] = ui->showRunner3IconCheckBox->isChecked();
		this->showIcons[3] = ui->showRunner4IconCheckBox->isChecked();
		this->showIcons[4] = ui->showCommentatorIconCheckBox->isChecked();
		this->SaveOverlayData();
	};
	connect(ui->showRunner1IconCheckBox, &QCheckBox::stateChanged, this, onIconToggled);
	connect(ui->showRunner2IconCheckBox, &QCheckBox::stateChanged, this, onIconToggled);
	connect(ui->showRunner3IconCheckBox, &QCheckBox::stateChanged, this, onIconToggled);
	connect(ui->showRunner4IconCheckBox, &QCheckBox::stateChanged, this, onIconToggled);
	connect(ui->showCommentatorIconCheckBox, &QCheckBox::stateChanged, this, onIconToggled);
	connect(ui->TimerStartScreenShotCheck, &QCheckBox::stateChanged, this,
		[this](int s) { this->autoScreenShotOnStart = (s == Qt::Checked); });
	connect(ui->TimerStopScreenShotCheck, &QCheckBox::stateChanged, this,
		[this](int s) { this->autoScreenShotOnStop = (s == Qt::Checked); });
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
	valObj.insert("RunnerTimer_1", this->runnerTimers[0]);
	valObj.insert("RunnerTimer_2", this->runnerTimers[1]);
	valObj.insert("RunnerTimer_3", this->runnerTimers[2]);
	valObj.insert("RunnerTimer_4", this->runnerTimers[3]);
	valObj.insert("Category", ui->CategoryText->text());
	valObj.insert("Hardware", ui->HardwareText->text());
	valObj.insert("Commentator", ui->CommentatorText->text());
	valObj.insert("EstimateTime", ui->EstimateTime->time().toString("HH:mm:ss"));
	valObj.insert("CountUpTimer", ui->TimerOnlyBtn->isChecked() || ui->BothBtn->isChecked() ?  ui->CountUpTimer->time().toString("HH:mm:ss") : "");
	valObj.insert("CountDownTimer", ui->CowntDownOnlyBtn->isChecked() || ui->BothBtn->isChecked() ?ui->CountDonwTimer->time().toString("HH:mm:ss") : "");

	// 次のゲームの情報 (3個先まで)
	int currentIndex = ui->gameSelectBox->currentIndex();
	for (int i = 0; i < 3; ++i) {
		QString suffix = QString::number(i);
		if (currentIndex >= 0 && currentIndex + i < (int)this->currentGameData.size()) {
			const GameData &nextData = this->currentGameData[currentIndex + i];
			valObj.insert("NextGameTitle_" + suffix, nextData.gameTitle);
			valObj.insert("NextRunnerName_" + suffix, nextData.runnerName);
			valObj.insert("NextCategory_" + suffix, nextData.category);
			valObj.insert("NextHardware_" + suffix, nextData.hardware);

			QTime est = QTime::fromMSecsSinceStartOfDay(nextData.estimateTime * 1000);
			valObj.insert("NextEstimate_" + suffix, est.toString("HH:mm:ss"));
		} else {
			// 次のゲームが存在しない（最後尾）の場合は空欄にする
			valObj.insert("NextGameTitle_" + suffix, "");
			valObj.insert("NextRunnerName_" + suffix, "");
			valObj.insert("NextCategory_" + suffix, "");
			valObj.insert("NextHardware_" + suffix, "");
			valObj.insert("NextEstimate_" + suffix, "");
		}
	}

	root.insert("values", valObj);

	// layouts (レイアウトごとの座標とスタイル)
	QJsonObject layoutsObj;
	bool running = (timer && timer->isActive());

	for (auto const &[layoutName, elements] : layoutData) {
		QJsonObject singleLayoutObj;
		QJsonObject transObj;
		QJsonObject styleObj;

		for (auto const &[id, elData] : elements) {
			// 座標
			transObj.insert("posX_" + id, elData.pos.x());
			transObj.insert("posY_" + id, elData.pos.y());

			// スタイル
			QJsonObject s;
			s.insert("fontFamily", elData.font.family());
			s.insert("fontSize", elData.font.pointSize());
			s.insert("isBold", elData.font.bold());
			s.insert("isItalic", elData.font.italic());
			s.insert("color", elData.color);
			s.insert("textAlign", elData.align);
			s.insert("outlineEnabled", elData.outlineEnabled);
			s.insert("outlineSize", elData.outlineSize);
			s.insert("outlineColor", elData.outlineColor);
			s.insert("isVisible", elData.isVisible);
			s.insert("maxWidth", elData.maxWidth);
			s.insert("wrapMode", elData.wrapMode);

			if (id.contains("Timer")) {
				s.insert("timerState", this->timerState); // 既存コードから維持
				s.insert("isRunning", running);
			}
			styleObj.insert(id, s);
		}

		singleLayoutObj.insert("transforms", transObj);
		singleLayoutObj.insert("styles", styleObj);
		singleLayoutObj.insert("stopColor", QColor(timerStopColor).name()); // 既存コードから維持
		layoutsObj.insert(layoutName, singleLayoutObj);
	}
	root.insert("layouts", layoutsObj);

	// settings (全体設定用)
	QJsonObject settingsObj;
	settingsObj.insert("showIcon_1", this->showIcons[0]);
	settingsObj.insert("showIcon_2", this->showIcons[1]);
	settingsObj.insert("showIcon_3", this->showIcons[2]);
	settingsObj.insert("showIcon_4", this->showIcons[3]);
	settingsObj.insert("showIcon_5", this->showIcons[4]);
	
	root.insert("settings", settingsObj);

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

void RTAPluginDock::LoadOverlayData()
{
	char *pathC = obs_module_get_config_path(obs_current_module(), "");
	QString path = QString::fromUtf8(pathC);
	bfree(pathC);
	QFile file(path + "/overlay_data.json");

	// 1. ファイルが存在しない、または開けない場合は初期設定(デフォルト)を生成
	if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		ElementData defaultData;
		for (auto const &[name, id] : textElementMap) {
			layoutData["main"][id] = defaultData;
			layoutData["setup"][id] = defaultData;
		}
		this->currentLayoutName = "main";

		ui->layoutSelectBox->clear();
		ui->layoutSelectBox->addItem("main");
		ui->layoutSelectBox->addItem("setup");
		ui->layoutSelectBox->setCurrentText("main");

		this->SaveOverlayData(); // ひな形ファイルを作成
		return;
	}

	// 2. ファイルが存在する場合は読み込んでパース
	QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	file.close();

	// 現在のレイアウトデータを一旦リセット
	ui->layoutSelectBox->blockSignals(true); // 余計な保存処理が走らないようにブロック
	ui->layoutSelectBox->clear();
	layoutData.clear();

	// === layouts (シーンと座標・スタイル) の復元 ===
	if (root.contains("layouts")) {
		QJsonObject layoutsObj = root["layouts"].toObject();
		for (auto it = layoutsObj.begin(); it != layoutsObj.end(); ++it) {
			QString layoutName = it.key();
			ui->layoutSelectBox->addItem(layoutName);

			QJsonObject singleLayoutObj = it.value().toObject();
			QJsonObject transObj = singleLayoutObj["transforms"].toObject();
			QJsonObject styleObj = singleLayoutObj["styles"].toObject();
			this->timerStopColor = singleLayoutObj["stopColor"].toString("#FF00FF"); // デフォルトはマゼンタ（わかりやすいように）

			for (auto const &[name, id] : textElementMap) {
				ElementData elData; // デフォルト値で初期化

				// 座標の復元
				if (transObj.contains("posX_" + id) && transObj.contains("posY_" + id)) {
					elData.pos = QPointF(transObj["posX_" + id].toDouble(),
							     transObj["posY_" + id].toDouble());
				}

				// スタイルの復元
				if (styleObj.contains(id)) {
					QJsonObject s = styleObj[id].toObject();
					QFont f(s["fontFamily"].toString());
					f.setPointSize(s["fontSize"].toInt());
					f.setBold(s["isBold"].toBool());
					f.setItalic(s["isItalic"].toBool());
					elData.font = f;

					elData.color = s["color"].toString();
					elData.align = s["textAlign"].toString();
					elData.outlineEnabled = s["outlineEnabled"].toBool();
					elData.outlineSize = s["outlineSize"].toInt();
					elData.outlineColor = s["outlineColor"].toString();
					elData.isVisible = s.contains("isVisible") ? s["isVisible"].toBool() : true;
					elData.maxWidth = s.contains("maxWidth") ? s["maxWidth"].toInt() : 0;
					elData.wrapMode = s.contains("wrapMode") ? s["wrapMode"].toString()
										 : "none"; // ← 追加
				}
				layoutData[layoutName][id] = elData;
			}
		}
	}

	// フェイルセーフ: 万が一layoutsが空だった場合はデフォルトを生成
	if (layoutData.empty()) {
		ElementData defaultData;
		for (auto const &[name, id] : textElementMap) {
			layoutData["main"][id] = defaultData;
			layoutData["setup"][id] = defaultData;
		}
		ui->layoutSelectBox->addItem("main");
		ui->layoutSelectBox->addItem("setup");
	}

	// 最初のレイアウトを選択状態にする
	if (ui->layoutSelectBox->count() > 0) {
		this->currentLayoutName = ui->layoutSelectBox->itemText(0);
		ui->layoutSelectBox->setCurrentText(this->currentLayoutName);
	}
	ui->layoutSelectBox->blockSignals(false);

	// === values (テキストボックス等の中身) の復元 ===
	if (root.contains("values")) {
		QJsonObject valObj = root["values"].toObject();
		ui->GameTitleText->setText(valObj["GameTitle"].toString());
		ui->RunnerText_1->setText(valObj["RunnerName_1"].toString());
		ui->RunnerText_2->setText(valObj["RunnerName_2"].toString());
		ui->RunnerText_3->setText(valObj["RunnerName_3"].toString());
		ui->RunnerText_4->setText(valObj["RunnerName_4"].toString());
		ui->CategoryText->setText(valObj["Category"].toString());
		ui->HardwareText->setText(valObj["Hardware"].toString());
		ui->CommentatorText->setText(valObj["Commentator"].toString());

		this->runnerTimers[0] = valObj["RunnerTimer_1"].toString();
		this->runnerTimers[1] = valObj["RunnerTimer_2"].toString();
		this->runnerTimers[2] = valObj["RunnerTimer_3"].toString();
		this->runnerTimers[3] = valObj["RunnerTimer_4"].toString();

		// EST時間などの復元
		QTime est = QTime::fromString(valObj["EstimateTime"].toString(), "HH:mm:ss");
		if (est.isValid())
			ui->EstimateTime->setTime(est);
	}

	// === settings (アイコンチェックボックス等) の復元 ===
	if (root.contains("settings")) {
		QJsonObject settingsObj = root["settings"].toObject();
		this->showIcons[0] = settingsObj["showIcon_1"].toBool();
		this->showIcons[1] = settingsObj["showIcon_2"].toBool();
		this->showIcons[2] = settingsObj["showIcon_3"].toBool();
		this->showIcons[3] = settingsObj["showIcon_4"].toBool();
		this->showIcons[4] = settingsObj["showIcon_5"].toBool();

		ui->showRunner1IconCheckBox->setChecked(this->showIcons[0]);
		ui->showRunner2IconCheckBox->setChecked(this->showIcons[1]);
		ui->showRunner3IconCheckBox->setChecked(this->showIcons[2]);
		ui->showRunner4IconCheckBox->setChecked(this->showIcons[3]);
		ui->showCommentatorIconCheckBox->setChecked(this->showIcons[4]);
	}
}

void RTAPluginDock::onSavePosSettingClicked()
{
	char *pathC = obs_module_get_config_path(obs_current_module(), "");
	QString path = QString::fromUtf8(pathC);
	bfree(pathC);
	QString fileName = QFileDialog::getSaveFileName(this, "現在のレイアウトをエクスポート", path, "JSON (*.json)");
	if (fileName.isEmpty())
		return;

	QJsonObject exportObj;
	QJsonObject trans;
	QJsonObject styles;

	// 現在選択しているレイアウトのデータのみを書き出す
	for (auto const &[id, elData] : layoutData[this->currentLayoutName]) {
		trans.insert("posX_" + id, elData.pos.x());
		trans.insert("posY_" + id, elData.pos.y());

		QJsonObject s;
		s.insert("family", elData.font.family());
		s.insert("size", elData.font.pointSize());
		s.insert("bold", elData.font.bold());
		s.insert("italic", elData.font.italic());
		s.insert("color", elData.color);
		s.insert("align", elData.align);
		s.insert("outlineEnabled", elData.outlineEnabled);
		s.insert("outlineSize", elData.outlineSize);
		s.insert("outlineColor", elData.outlineColor);
		styles.insert(id, s);
	}
	exportObj.insert("transforms", trans);
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
	QString fileName = QFileDialog::getOpenFileName(this, "レイアウト設定のインポート", path, "JSON (*.json)");
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
				// 現在のレイアウトに座標を適用
				layoutData[this->currentLayoutName][id].pos =
					QPointF(trans["posX_" + id].toDouble(), trans["posY_" + id].toDouble());
			}
		}
	}

	if (data.contains("styles")) {
		QJsonObject styles = data["styles"].toObject();
		for (auto it = styles.begin(); it != styles.end(); ++it) {
			QJsonObject s = it.value().toObject();
			QString id = it.key();

			if (layoutData[this->currentLayoutName].count(id)) {
				QFont f(s["family"].toString());
				f.setPointSize(s["size"].toInt());
				f.setBold(s["bold"].toBool());
				f.setItalic(s["italic"].toBool());

				// 現在のレイアウトにスタイルを適用
				layoutData[this->currentLayoutName][id].font = f;
				layoutData[this->currentLayoutName][id].color = s["color"].toString();
				layoutData[this->currentLayoutName][id].align = s["align"].toString();
				layoutData[this->currentLayoutName][id].outlineEnabled = s["outlineEnabled"].toBool();
				layoutData[this->currentLayoutName][id].outlineSize = s["outlineSize"].toInt();
				layoutData[this->currentLayoutName][id].outlineColor = s["outlineColor"].toString();
			}
		}
	}

	// UI（座標のSpinBoxなど）の表示を更新させる
	emit ui->textSelectBox->currentTextChanged(ui->textSelectBox->currentText());

	// 更新された現在のレイアウト情報を、自動管理用の全体JSONにも保存する
	this->SaveOverlayData();
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
	columnNames.push_back("");
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
	ui->FormatCommentatorBox->clear();

	for (const auto &name : columnNames) {
		QString qName = QString::fromStdString(name);
		ui->FormatGameTitleBox->addItem(qName);
		ui->FormatRunnerBox->addItem(qName);
		ui->FormatCategoryBox->addItem(qName);
		ui->FormatHardwareBox->addItem(qName);
		ui->FormatCommentatorBox->addItem(qName);
	}

	// カラムの自動検出キーワード
	const std::vector<QString> gameKeywords = {"Game", "Title", "ゲーム", "タイトル"};
	const std::vector<QString> runnerKeywords = {"Runner", "Player", "走者", "プレイヤー"};
	const std::vector<QString> categoryKeywords = {"Category", "カテゴリ"};
	const std::vector<QString> hardwareKeywords = {"Hardware", "Hard", "機種", "機材", "Platform"};
	const std::vector<QString> commentatorKeywords = {"Commentator", "解説", "解説者"};

	int gi = findColumnIndexByKeywords(columnNames, gameKeywords);
	int ri = findColumnIndexByKeywords(columnNames, runnerKeywords);
	int ci = findColumnIndexByKeywords(columnNames, categoryKeywords);
	int hi = findColumnIndexByKeywords(columnNames, hardwareKeywords);
	int cmi = findColumnIndexByKeywords(columnNames, commentatorKeywords);

	if (gi != -1)
		ui->FormatGameTitleBox->setCurrentIndex(gi);
	if (ri != -1)
		ui->FormatRunnerBox->setCurrentIndex(ri);
	if (ci != -1)
		ui->FormatCategoryBox->setCurrentIndex(ci);
	if (hi != -1)
		ui->FormatHardwareBox->setCurrentIndex(hi);
	if (cmi != -1)
		ui->FormatCommentatorBox->setCurrentIndex(cmi);

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
	int cmi = ui->FormatCommentatorBox->currentIndex();

	for (const QJsonValue &value : itemsArray) {
		QJsonObject itemObj = value.toObject();
		auto dataArray = itemObj["data"].toArray();

		GameData data;
		data.gameTitle = dataArray[gi - 1].toString();
		data.runnerName = dataArray[ri - 1].toString();
		data.category = dataArray[ci - 1].toString();
		data.hardware = dataArray[hi - 1].toString();
		data.commentator = dataArray[cmi - 1].toString();
		data.estimateTime = itemObj["length_t"].toInt();

		this->currentGameData.push_back(data);
		ui->gameSelectBox->addItem(data.gameTitle);
	}

	// テーブルに反映
	ui->scheduleTable->setRowCount(0);
	for (int i = 0; i < (int)this->currentGameData.size(); ++i) {
		ui->scheduleTable->insertRow(i);
		ui->scheduleTable->setItem(i, 0, new QTableWidgetItem(this->currentGameData[i].gameTitle));
		ui->scheduleTable->setItem(i, 1, new QTableWidgetItem(this->currentGameData[i].runnerName));
		ui->scheduleTable->setItem(i, 2, new QTableWidgetItem(this->currentGameData[i].category));
		ui->scheduleTable->setItem(i, 3, new QTableWidgetItem(this->currentGameData[i].hardware));

		QTime est = QTime::fromMSecsSinceStartOfDay(this->currentGameData[i].estimateTime * 1000);
		ui->scheduleTable->setItem(i, 4, new QTableWidgetItem(est.toString("HH:mm:ss")));

		ui->scheduleTable->setItem(i, 5, new QTableWidgetItem(this->currentGameData[i].commentator));
	}

	SaveOverlayData();
}