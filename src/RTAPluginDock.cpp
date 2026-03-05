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
	{"解説コメント", "Commentator"},

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

		// 両方を有効化
		ui->CountUpTimer->setEnabled(true);
		ui->CountDonwTimer->setEnabled(true);

		// 他のボタンの状態
		ui->TimerOnlyBtn->setChecked(false);
		ui->CowntDownOnlyBtn->setChecked(false);

		this->SaveOverlayData();
	});

	// タイマーを使わない(None)：両方を非表示・無効化
	connect(ui->TimerNoneButton, &QRadioButton::clicked, this, [this]() {
		// 両方を非表示・無効化
		ui->CountUpTimer->setEnabled(false);
		ui->CountDonwTimer->setEnabled(false);
		// 他のボタンの状態
		ui->TimerOnlyBtn->setChecked(false);
		ui->CowntDownOnlyBtn->setChecked(false);
		ui->BothBtn->setChecked(false);
		this->SaveOverlayData();
	});


	// タイマー Start
	connect(ui->timerStartButton, &QPushButton::clicked, this, [this]() {
		timer->start(1000);
		ui->timerStartButton->setEnabled(false);
		ui->timerStopButton->setEnabled(true);
		ui->timeResetButton->setEnabled(false);
		ui->textDone->setEnabled(false);
		this->timerState = "Running"; // タイマー状態を更新
		this->SaveOverlayData();

		if (this->autoScreenShotOnStart) {
			obs_frontend_take_screenshot();
		}
	});

	// タイマー Stop
	connect(ui->timerStopButton, &QPushButton::clicked, this, [this]() {
		timer->stop();
		ui->timerStartButton->setEnabled(true);
		ui->timeResetButton->setEnabled(true);
		this->timerState = "Stopped"; // タイマー状態を更新
		this->SaveOverlayData();

		if (this->autoScreenShotOnStop) {
			obs_frontend_take_screenshot();
		}
	});

	// タイマー Reset
	connect(ui->timeResetButton, &QPushButton::clicked, this, [this]() {
		ui->CountUpTimer->setTime(QTime(0, 0, 0));
		ui->CountDonwTimer->setTime(this->initCountDownTimer);
		ui->timerStopButton->setEnabled(false);
		ui->timerStartButton->setEnabled(true);
		ui->textDone->setEnabled(true);
		this->timerState = "Reset"; // タイマー状態を更新
		
		// 各走者のタイマーをクリア
		for (int i = 0; i < 4; i++) {
			this->runnerTimers[i] = "";
		}

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
				ui->CommentatorText->setText(data.commentator);
				break;
			}
		}
	});

	// 走者タイマーストップボタン
	connect(ui->timerStopButton_P1, &QPushButton::clicked, this, [this]() {
		this->runnerTimers[0] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
		this->SaveOverlayData(); // 即時反映
	});
	connect(ui->timerStopButton_P2, &QPushButton::clicked, this, [this]() {
		this->runnerTimers[1] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
		this->SaveOverlayData(); // 即時反映
	});
	connect(ui->timerStopButton_P3, &QPushButton::clicked, this, [this]() {
		this->runnerTimers[2] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
		this->SaveOverlayData(); // 即時反映
	});
	connect(ui->timerStopButton_P4, &QPushButton::clicked, this, [this]() {
		this->runnerTimers[3] = ui->TimerOnlyBtn->isChecked() ? ui->CountUpTimer->time().toString("HH:mm:ss") : "";
		this->SaveOverlayData(); // 即時反映
	});

	

	// 配置情報のシグナルを一括で接続
	SetupPositionSignals();

	// スタイル情報のシグナルを一括で接続
	SetupStyleSignals();


	// スクショボタン
	connect(ui->ScreenShotBtn, &QPushButton::clicked, this, []() {
		obs_frontend_take_screenshot();
	});

	// チェックボックスの状態変化をキャッチしてフラグを更新
	connect(ui->TimerStartScreenShotCheck, &QCheckBox::checkStateChanged, this, [this](int state) {
			this->autoScreenShotOnStart = (state == Qt::Checked); 
	});
	connect(ui->TimerStopScreenShotCheck, &QCheckBox::checkStateChanged, this, [this](int state) {
			this->autoScreenShotOnStop = (state == Qt::Checked); 
	});

	// タイマー開始時にスクショを撮る
	connect(ui->timerStartButton, &QPushButton::clicked, this, [this]() {
		if (this->autoScreenShotOnStart) {
			obs_frontend_take_screenshot();
		}
	});
	// タイマー停止時にスクショを撮る
	connect(ui->timerStopButton, &QPushButton::clicked, this, [this]() {
		if (this->autoScreenShotOnStop) {
			obs_frontend_take_screenshot();
		}
	});

	// レイアウトの切り替え
	connect(ui->layoutSelectBox, &QComboBox::currentTextChanged, this, [this](const QString &text) {
		if (text.isEmpty() || !layoutData.count(text))
			return;
		this->currentLayoutName = text;

		if (ui->syncSceneCheckBox && ui->syncSceneCheckBox->isChecked()) {
			// OBSから同名のソースを検索
			obs_source_t *sceneSource = obs_get_source_by_name(text.toUtf8().constData());
			if (sceneSource) {
				// 取得したソースが「シーン」であるか確認して切り替え
				obs_scene_t *scene = obs_scene_from_source(sceneSource);
				if (scene) {
					obs_frontend_set_current_scene(sceneSource);
				}
				// 取得したソースは必ず解放する（メモリリーク防止）
				obs_source_release(sceneSource);
			}
		}

		// 選択中のテキスト要素のUI（座標や色など）を現在のレイアウトの数値に更新させる
		emit ui->textSelectBox->currentTextChanged(ui->textSelectBox->currentText());
		this->SaveOverlayData();
	});

	// レイアウトの追加（現在のレイアウトをコピーして新規作成）
	connect(ui->addLayoutBtn, &QPushButton::clicked, this, [this]() {
		bool ok;
		QString newName = QInputDialog::getText(this, "レイアウト追加", "新しいレイアウト名 (英数字推奨):", QLineEdit::Normal, "", &ok);
		if (ok && !newName.isEmpty() && !layoutData.count(newName)) {
			// 現在のレイアウト設定を丸ごとコピー
			layoutData[newName] = layoutData[this->currentLayoutName];
			ui->layoutSelectBox->addItem(newName);
			ui->layoutSelectBox->setCurrentText(newName); // 自動的に切り替わる
		}
	});

	// レイアウトの削除
	connect(ui->removeLayoutBtn, &QPushButton::clicked, this, [this]() {
		if (layoutData.size() <= 1) {
			QMessageBox::warning(this, "エラー", "最後のレイアウトは削除できません。");
			return;
		}
		int ret = QMessageBox::question(
			this, "確認", QString("レイアウト '%1' を削除しますか？").arg(this->currentLayoutName));
		if (ret == QMessageBox::Yes) {
			layoutData.erase(this->currentLayoutName);
			ui->layoutSelectBox->removeItem(ui->layoutSelectBox->currentIndex());
			// removeItemによりcurrentTextChangedが発火し、自動的に別のレイアウトに切り替わる
		}
	});

	SetupSecheduleSignals();

	// 起動時に配置情報をロードしてUIに反映させる
	this->LoadOverlayData();

	// ロードした情報に合わせて、UIの座標SpinBox等の表示を更新させる
	emit ui->textSelectBox->currentTextChanged(ui->textSelectBox->currentText());
}

RTAPluginDock::~RTAPluginDock()
{
	// posUpdateTimerは親(this)を持っているので自動削除されますが、明示的なdeleteも安全です
	delete ui;
}

void RTAPluginDock::SetupPositionSignals()
{
	// 2. 編集用SpinBoxの設定
	ui->posX_text->setRange(-10000.0, 10000.0);
	ui->posY_text->setRange(-10000.0, 10000.0);
	ui->posX_text->setDecimals(2);
	ui->posY_text->setDecimals(2);

	// 3. 編集用SpinBoxの値が変わった時、現在のレイアウトのデータを更新して保存する
	auto onPosEdited = [this](double) {
		QString displayName = ui->textSelectBox->currentText();
		if (textElementMap.count(displayName)) {
			QString id = textElementMap.at(displayName);
			layoutData[this->currentLayoutName][id].pos =
				QPointF(ui->posX_text->value(), ui->posY_text->value());

			if (posUpdateTimer)
				posUpdateTimer->start();
			this->SaveOverlayData();
		}
	};
	connect(ui->posX_text, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onPosEdited);
	connect(ui->posY_text, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onPosEdited);

	// 配置情報の保存
	connect(ui->SavePosSettingBtn, &QPushButton::clicked, this, &RTAPluginDock::onSavePosSettingClicked);
	// 配置情報の読み込み
	connect(ui->LoadPosSettingBtn, &QPushButton::clicked, this, &RTAPluginDock::onLoadPosSettingClicked);
}

void RTAPluginDock::SetupStyleSignals()
{
	// 対象選択ComboBoxの初期化
	ui->textSelectBox->clear();
	for (auto const &[name, id] : textElementMap)
		ui->textSelectBox->addItem(name);

	// アライメント選択ComboBoxの初期化
	ui->textAlignBox->clear();
	ui->textAlignBox->addItem("左揃え", "left");
	ui->textAlignBox->addItem("中央揃え", "center");
	ui->textAlignBox->addItem("右揃え", "right");

	ui->wrapModeBox->clear();
	ui->wrapModeBox->addItem("制限なし", "none");
	ui->wrapModeBox->addItem("自動縮小 (はみ出さない)", "shrink");
	ui->wrapModeBox->addItem("自動改行 (折り返す)", "wrap");

	// 対象要素またはレイアウトが切り替わった時のUI同期
	connect(ui->textSelectBox, &QComboBox::currentTextChanged, this, [this](const QString &name) {
		if (name.isEmpty() || !textElementMap.count(name))
			return;
		QString id = textElementMap.at(name);
		ElementData &ed = layoutData[this->currentLayoutName][id];

		// シグナルのループを防ぐため一時的にブロック
		ui->posX_text->blockSignals(true);
		ui->posY_text->blockSignals(true);
		ui->outlineCheckBox->blockSignals(true);
		ui->visibleCheckBox->blockSignals(true);
		ui->maxWidthSpinBox->blockSignals(true);
		ui->wrapModeBox->blockSignals(true);

		// 値をUIにセット
		ui->posX_text->setValue(ed.pos.x());
		ui->posY_text->setValue(ed.pos.y());
		ui->outlineCheckBox->setChecked(ed.outlineEnabled);
		ui->visibleCheckBox->setChecked(ed.isVisible);

		int alignIdx = ui->textAlignBox->findData(ed.align);
		ui->textAlignBox->setCurrentIndex(alignIdx);

		ui->maxWidthSpinBox->setValue(ed.maxWidth);
		int wrapIdx = ui->wrapModeBox->findData(ed.wrapMode);
		if (wrapIdx >= 0) ui->wrapModeBox->setCurrentIndex(wrapIdx);

		// ※outlineSizeなどのスピンボックスがあればここでセット
		ui->outlineSize->blockSignals(true);
		ui->outlineSize->setValue(ed.outlineSize);
		ui->outlineSize->blockSignals(false);

		ui->posX_text->blockSignals(false);
		ui->posY_text->blockSignals(false);
		ui->outlineCheckBox->blockSignals(false);
		ui->visibleCheckBox->blockSignals(false);
		ui->maxWidthSpinBox->blockSignals(false);
		ui->wrapModeBox->blockSignals(false);
	});

	// アライメント変更
	connect(ui->textAlignBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		layoutData[this->currentLayoutName][id].align = ui->textAlignBox->itemData(index).toString();
		this->SaveOverlayData();
	});

	// フォント変更
	connect(ui->fontSetting, &QPushButton::clicked, this, [this]() {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		bool ok;
		QFont font =
			QFontDialog::getFont(&ok, layoutData[this->currentLayoutName][id].font, this, "フォント選択");
		if (ok) {
			layoutData[this->currentLayoutName][id].font = font;
			this->SaveOverlayData();
		}
	});

	// 文字色変更
	connect(ui->StyleColorBtn, &QPushButton::clicked, this, [this]() {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		QColor color =
			QColorDialog::getColor(QColor(layoutData[this->currentLayoutName][id].color), this, "色選択");
		if (color.isValid()) {
			layoutData[this->currentLayoutName][id].color = color.name();
			this->SaveOverlayData();
		}
	});

	// タイマーストップカラー
	connect(ui->TimerStopColorChangeBtn, &QPushButton::clicked, this, [this]() {
		QColor color = QColorDialog::getColor(QColor(timerStopColor), this, "停止時カラー選択");
		if (color.isValid()) {
			this->timerStopColor = color.name();
			this->SaveOverlayData();
		}
	});

	// アウトライン有効化
	connect(ui->outlineCheckBox, &QCheckBox::checkStateChanged, this, [this](int state) {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		layoutData[this->currentLayoutName][id].outlineEnabled = (state == Qt::Checked);
		this->SaveOverlayData();
	});

	// アウトライン色変更ボタン
	connect(ui->outlineColorButton, &QPushButton::clicked, this, [this]() {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		QColor color = QColorDialog::getColor(QColor(layoutData[this->currentLayoutName][id].outlineColor), this,
						      "アウトライン色選択");
		if (color.isValid()) {
			layoutData[this->currentLayoutName][id].outlineColor = color.name();
			this->SaveOverlayData();
		}
	});
	
	// アウトライン太さ変更
	connect(ui->outlineSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		layoutData[this->currentLayoutName][id].outlineSize = value;
		this->SaveOverlayData();
	});

	// 名前の前にアイコンを表示するフラグ
	connect(ui->showRunner1IconCheckBox, &QCheckBox::checkStateChanged, this, [this](int state) {
		this->showIcons[0] = (state == Qt::Checked);
		this->SaveOverlayData();
	});
	// 名前の前にアイコンを表示するフラグ
	connect(ui->showRunner2IconCheckBox, &QCheckBox::checkStateChanged, this, [this](int state) {
		this->showIcons[1] = (state == Qt::Checked);
		this->SaveOverlayData();
	});
	// 名前の前にアイコンを表示するフラグ
	connect(ui->showRunner3IconCheckBox, &QCheckBox::checkStateChanged, this, [this](int state) {
		this->showIcons[2] = (state == Qt::Checked);
		this->SaveOverlayData();
	});
	// 名前の前にアイコンを表示するフラグ
	connect(ui->showRunner4IconCheckBox, &QCheckBox::checkStateChanged, this, [this](int state) {
		this->showIcons[3] = (state == Qt::Checked);
		this->SaveOverlayData();
	});
	// 名前の前にアイコンを表示するフラグ
	connect(ui->showCommentatorIconCheckBox, &QCheckBox::checkStateChanged, this, [this](int state) {
		this->showIcons[4] = (state == Qt::Checked);
		this->SaveOverlayData();
	});

	// 表示ON/OFF切り替え
	connect(ui->visibleCheckBox, &QCheckBox::checkStateChanged, this, [this](int state) {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		layoutData[this->currentLayoutName][id].isVisible = (state == Qt::Checked);
		this->SaveOverlayData();
	});

	// 最大幅とモードの変更
	connect(ui->maxWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		layoutData[this->currentLayoutName][id].maxWidth = value;
		this->SaveOverlayData();
	});

	connect(ui->wrapModeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		QString id = textElementMap.at(ui->textSelectBox->currentText());
		layoutData[this->currentLayoutName][id].wrapMode = ui->wrapModeBox->itemData(index).toString();
		this->SaveOverlayData();
	});
}

void RTAPluginDock::SetupSecheduleSignals() {
	// テーブルの初期設定
	ui->scheduleTable->setColumnCount(6);
	ui->scheduleTable->setHorizontalHeaderLabels({"GameTitle", "Runner", "Category", "Platform", "Estimate", "Commentator"});
	// 1. 列幅をユーザーがマウスでドラッグして変更できるようにする
	ui->scheduleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

	// 2. 各列のデフォルトの幅（ピクセル）を設定
	ui->scheduleTable->horizontalHeader()->setDefaultSectionSize(120);

	// 3. これ以上狭くならない「最小幅」を設定（これが横スクロールバー発生のトリガーになります）
	ui->scheduleTable->horizontalHeader()->setMinimumSectionSize(80);

	// 4. (任意) 一番右の列だけは、余白があれば右端まで伸ばす
	ui->scheduleTable->horizontalHeader()->setStretchLastSection(true);

	// 行の追加
	connect(ui->addScheduleBtn, &QPushButton::clicked, this, [this]() {
		int row = ui->scheduleTable->currentRow() + 1;
		if (row < 0)
			row = ui->scheduleTable->rowCount(); // 未選択なら一番下
		ui->scheduleTable->insertRow(row);
	});

	// 行の削除
	connect(ui->removeScheduleBtn, &QPushButton::clicked, this, [this]() {
		int row = ui->scheduleTable->currentRow();
		if (row >= 0)
			ui->scheduleTable->removeRow(row);
	});

	// テーブルの編集内容をシステムに適用
	connect(ui->updateScheduleBtn, &QPushButton::clicked, this, [this]() {
		this->currentGameData.clear();
		ui->gameSelectBox->blockSignals(true);
		ui->gameSelectBox->clear();

		for (int i = 0; i < ui->scheduleTable->rowCount(); ++i) {
			GameData data;
			data.gameTitle = ui->scheduleTable->item(i, 0) ? ui->scheduleTable->item(i, 0)->text() : "";
			data.runnerName = ui->scheduleTable->item(i, 1) ? ui->scheduleTable->item(i, 1)->text() : "";
			data.category = ui->scheduleTable->item(i, 2) ? ui->scheduleTable->item(i, 2)->text() : "";
			data.hardware = ui->scheduleTable->item(i, 3) ? ui->scheduleTable->item(i, 3)->text() : "";

			// 予定時間の処理（文字列から秒への変換など、用途に合わせて調整）
			QString estStr = ui->scheduleTable->item(i, 4) ? ui->scheduleTable->item(i, 4)->text() : "00:00:00";
			QTime t = QTime::fromString(estStr, "HH:mm:ss");
			data.estimateTime = t.isValid() ? (t.hour() * 3600 + t.minute() * 60 + t.second()) : 0;

			data.commentator = ui->scheduleTable->item(i, 5) ? ui->scheduleTable->item(i, 5)->text() : "";

			this->currentGameData.push_back(data);
			ui->gameSelectBox->addItem(data.gameTitle);
		}

		ui->gameSelectBox->blockSignals(false);
		if (ui->gameSelectBox->count() > 0) {
			emit ui->gameSelectBox->currentTextChanged(ui->gameSelectBox->currentText());
		}
		this->SaveOverlayData();
		QMessageBox::information(this, "更新", "スケジュールを更新・適用しました。");
	});

	// スケジュールのエクスポート(JSON出力)
	connect(ui->exportScheduleBtn, &QPushButton::clicked, this, [this]() {
		QString fileName = QFileDialog::getSaveFileName(this, "スケジュールを出力", ".", "JSON (*.json)");
		if (fileName.isEmpty())
			return;

		QJsonArray itemsArray;
		for (const auto &data : this->currentGameData) {
			QJsonObject item;
			QJsonArray dataArray = {data.gameTitle, data.runnerName, data.category, data.hardware, data.commentator};
			item.insert("data", dataArray);
			item.insert("length_t", data.estimateTime);
			itemsArray.append(item);
		}
		QJsonObject scheduleObj;
		QJsonArray colArr = {"GameTitle", "Runner", "Category", "Platform", "Commentator"};
		scheduleObj.insert("columns", colArr);
		scheduleObj.insert("items", itemsArray);
		QJsonObject root;
		root.insert("schedule", scheduleObj);

		QFile file(fileName);
		if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			file.write(QJsonDocument(root).toJson());
			file.close();
			QMessageBox::information(this, "出力成功", "スケジュールを保存しました。");
		}
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