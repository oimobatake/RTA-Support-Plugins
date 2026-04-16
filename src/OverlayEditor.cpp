#include "OverlayEditor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStyle>

OverlayEditor::OverlayEditor(const std::map<QString, ElementData> &data, QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Overlay Visual Editor (1920x1080 Canvas)");
	setModal(true);

	// ウィンドウサイズをスケーリングに合わせて固定 (キャンバス + ボタンエリア)
	setFixedSize(1920 * scale + 40, 1080 * scale + 100);
	setStyleSheet("background-color: #2b2b2b; color: #eee;");

	// --- メインレイアウト ---
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	// --- キャンバス（配信画面を模した黒い領域） ---
	QWidget *canvas = new QWidget(this);
	canvas->setFixedSize(1920 * scale, 1080 * scale);
	canvas->setStyleSheet("background-color: #000; border: 2px solid #555;");

	// 各要素（表示設定がONのもの）を配置
	for (auto const &[id, ed] : data) {
		if (!ed.isVisible) {
			// 非表示設定のものは初期位置だけ保持してラベルは作らない
			resultCoords[id] = ed.pos;
			continue;
		}

		// ラベルの作成
		DraggableLabel *label = new DraggableLabel(id, id, canvas);

		// スケーリングして配置 (1920 -> 960)
		label->move(ed.pos.x() * scale, ed.pos.y() * scale);

		// 移動イベントをキャッチして結果用マップを更新
		connect(label, &DraggableLabel::moved, [this](const QString &id, const QPoint &pos) {
			// 元の解像度(1920)に戻して保存
			resultCoords[id] = QPointF(pos.x() / scale, pos.y() / scale);
		});

		// 現在の座標を結果マップに初期登録
		resultCoords[id] = ed.pos;
	}

	// --- 下部操作エリア ---
	QHBoxLayout *btnLayout = new QHBoxLayout();

	QLabel *hintLabel = new QLabel("ドラッグで移動 / Applyで反映", this);
	hintLabel->setStyleSheet("color: #aaa; font-size: 11px;");

	QPushButton *btnApply = new QPushButton(" Apply Layout", this);
	btnApply->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
	btnApply->setFixedWidth(150);
	btnApply->setStyleSheet(
		"QPushButton { background-color: #deff9a; color: #000; font-weight: bold; border-radius: 4px; height: 35px; }"
		"QPushButton:hover { background-color: #c0ff60; }");

	QPushButton *btnCancel = new QPushButton(" Cancel", this);
	btnCancel->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
	btnCancel->setFixedWidth(100);
	btnCancel->setStyleSheet("height: 35px;");

	connect(btnApply, &QPushButton::clicked, this, &QDialog::accept);
	connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

	btnLayout->addWidget(hintLabel);
	btnLayout->addStretch();
	btnLayout->addWidget(btnCancel);
	btnLayout->addWidget(btnApply);

	mainLayout->addWidget(canvas, 0, Qt::AlignCenter);
	mainLayout->addLayout(btnLayout);
}