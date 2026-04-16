#pragma once

#include <QDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QMap>
#include <QPointF>
#include "RTAPluginDock.h"

// --- ドラッグ可能なラベルコンポーネント ---
class DraggableLabel : public QLabel {
	Q_OBJECT
public:
	DraggableLabel(const QString &id, const QString &text, QWidget *parent = nullptr)
		: QLabel(text, parent),
		  elementId(id)
	{

		// デザイン設定: 半透明の枠付きラベル
		setFrameStyle(QFrame::Panel | QFrame::Plain);
		setLineWidth(1);
		setAlignment(Qt::AlignCenter);
		setStyleSheet("background-color: rgba(222, 255, 154, 180);" // RTA風の黄緑色（半透明）
			      "color: #000;"
			      "font-weight: bold;"
			      "border: 1px solid #000;"
			      "padding: 4px;");
		setCursor(Qt::SizeAllCursor); // 移動用カーソル
		adjustSize();
	}

	QString elementId;

signals:
	void moved(const QString &id, const QPoint &newPos);

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton) {
			dragStartPosition = event->pos();
			raise(); // 最前面に持ってくる
		}
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (event->buttons() & Qt::LeftButton) {
			QPoint newPos = mapToParent(event->pos() - dragStartPosition);

			// キャンバス内からはみ出さないように制限（任意）
			move(newPos);
			emit moved(elementId, pos());
		}
	}

private:
	QPoint dragStartPosition;
};

// --- ビジュアルエディタダイアログ本体 ---
class OverlayEditor : public QDialog {
	Q_OBJECT
public:
	// 現在のレイアウトデータを受け取って初期化
	OverlayEditor(const std::map<QString, ElementData> &data, QWidget *parent = nullptr);

	// 編集結果の座標マップを返す
	std::map<QString, QPointF> getResult() const { return resultCoords; }

private:
	const float scale = 0.5f; // 1920x1080を半分(960x540)で表示する倍率
	std::map<QString, QPointF> resultCoords;
};