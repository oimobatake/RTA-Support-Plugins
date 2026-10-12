#pragma once

#include <QGraphicsItem>
#include <QGraphicsView>
#include <QPixmap>
#include <QFont>
#include <QMap>
#include <QStringList>
#include "RTAPluginDock.h"

// オーバーレイのキャンバスサイズ（index.html の body と同じ）
constexpr double kCanvasWidth = 1920.0;
constexpr double kCanvasHeight = 1080.0;

// --- キャンバス上のパーツ1つ分 ---
// 位置 (pos) は index.html と同じ「揃えの基準点」。左揃えなら左上、中央揃えなら上辺の中央、右揃えなら右上。
class ElementItem : public QGraphicsItem {
public:
	ElementItem(const QString &id, const QString &name);

	const QString &id() const { return elementId; }
	const QString &name() const { return displayName; }

	// 表示内容と見た目を更新する（位置は呼び出し側で setPos する）
	void setContent(const ElementData &data, const QString &text);

	// 基準点からの相対座標で表した文字の領域
	QRectF textRect() const { return box; }
	// シーン座標での文字の領域（整列・吸着の計算用）
	QRectF sceneTextRect() const { return box.translated(pos()); }

	QRectF boundingRect() const override;
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

	enum { Type = UserType + 1 };
	int type() const override { return Type; }

protected:
	void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
	void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
	QString elementId;
	QString displayName;
	ElementData ed;

	// レイアウト計算結果
	QFont renderFont;
	QStringList lines;
	QList<double> lineWidths;
	double lineHeight = 0;
	double baselineOffset = 0;
	QRectF box;
	bool hovered = false;
};

// --- キャンバス（配信画面）を表示するビュー ---
class CanvasView : public QGraphicsView {
	Q_OBJECT
public:
	explicit CanvasView(QGraphicsScene *scene, QWidget *parent = nullptr);

	// 表示設定
	void setBackgroundImage(const QPixmap &pixmap);
	void setGridVisible(bool on);
	void setSnapEnabled(bool on) { snapEnabled = on; }
	void setGridSize(int size);
	void setSafeAreaVisible(bool on);
	void setFitMode();
	void setZoom(double zoom);
	double zoom() const;
	bool isFitMode() const { return fitMode; }

	// 配置モード: ghost をマウスに追従させ、クリックで placeRequested を出す
	void setPlacingItem(ElementItem *ghost);
	ElementItem *placingItem() const { return ghost; }

	// 中心を center に合わせたときの基準点（吸着込み）
	QPointF anchorForCenter(const ElementItem *item, const QPointF &center) const;
	// 選択中の要素の文字領域をまとめた矩形
	QRectF selectionRect() const;
	// 移動量 delta をキャンバス内に収まるよう補正する
	QPointF clampDelta(const QRectF &rect, QPointF delta) const;

signals:
	void placeRequested(const QString &id, const QPointF &anchor);
	void dropRequested(const QString &id, const QPointF &center);
	void itemsMoved(const QMap<QString, QPointF> &before);
	void nudgeRequested(int dx, int dy);
	void deleteRequested();
	void escapePressed();
	void cursorMoved(const QPointF &scenePos);
	void contextMenuRequested(const QPoint &globalPos);
	void zoomChanged(double zoom);

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	void drawBackground(QPainter *painter, const QRectF &rect) override;
	void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
	ElementItem *elementAt(const QPoint &viewPos) const;
	double snapToGrid(double v) const;
	QPointF snapDelta(const QRectF &rect, QPointF delta, bool allowSnap);

	QPixmap background;
	bool gridVisible = true;
	bool snapEnabled = true;
	int gridSize = 20;
	bool safeAreaVisible = false;
	bool fitMode = true;

	ElementItem *ghost = nullptr;

	// ドラッグ移動中の状態
	bool dragging = false;
	bool dragMoved = false;
	QPointF dragStartScene;
	QMap<QString, QPointF> dragStartPos;
	QMap<QString, ElementItem *> dragItems;
	QRectF dragStartRect;

	// スマートガイド（シーン座標の縦線 x / 横線 y）
	QList<double> guideXs;
	QList<double> guideYs;
};

// --- パーツ一覧からキャンバスへのドラッグ用 MIME タイプ ---
inline constexpr char kElementMimeType[] = "application/x-rta-element";
