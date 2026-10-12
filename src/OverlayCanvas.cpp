#include "OverlayCanvas.h"

#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QTextLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QScrollBar>
#include <cmath>
#include <limits>

// ============================================================
// ElementItem
// ============================================================

ElementItem::ElementItem(const QString &id, const QString &name) : elementId(id), displayName(name)
{
	setFlag(QGraphicsItem::ItemIsSelectable, true);
	setAcceptHoverEvents(true);
}

void ElementItem::setContent(const ElementData &data, const QString &text)
{
	prepareGeometryChange();
	ed = data;

	// index.html と同じく、保存されている文字サイズの数値を px として扱う
	int px = data.font.pointSize() > 0 ? data.font.pointSize()
					   : (data.font.pixelSize() > 0 ? data.font.pixelSize() : 48);
	renderFont = data.font;
	renderFont.setPixelSize(std::max(1, px));

	QString shown = text;
	shown.replace('\n', QChar::LineSeparator);

	lines.clear();
	lineWidths.clear();
	double width = 0;

	if (data.maxWidth > 0 && data.wrapMode == "wrap") {
		// 自動改行: 最大幅で折り返す（枠の幅は最大幅で固定）
		QTextLayout layout(shown, renderFont);
		QTextOption opt;
		opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		layout.setTextOption(opt);
		layout.beginLayout();
		for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
			line.setLineWidth(data.maxWidth);
			lines << shown.mid(line.textStart(), line.textLength()).trimmed();
		}
		layout.endLayout();
		if (lines.isEmpty())
			lines << QString();
		width = data.maxWidth;
	} else {
		QFontMetricsF fm(renderFont);
		double natural = fm.horizontalAdvance(shown);
		if (data.maxWidth > 0 && data.wrapMode == "shrink" && natural > data.maxWidth) {
			// 自動縮小: 収まるように文字サイズを下げる（index.html と同じ計算）
			int shrunk = std::max(1, (int)std::floor(px * (double)data.maxWidth / natural));
			renderFont.setPixelSize(shrunk);
			natural = QFontMetricsF(renderFont).horizontalAdvance(shown);
		}
		lines << shown;
		width = natural;
	}

	QFontMetricsF fm(renderFont);
	for (const QString &l : lines)
		lineWidths << fm.horizontalAdvance(l);

	// CSS の line-height: 1.1 に合わせる
	lineHeight = renderFont.pixelSize() * 1.1;
	baselineOffset = (lineHeight - (fm.ascent() + fm.descent())) / 2.0 + fm.ascent();
	double height = lineHeight * lines.size();

	double left = 0;
	if (data.align == "center")
		left = -width / 2.0;
	else if (data.align == "right")
		left = -width;
	box = QRectF(left, 0, std::max(width, 1.0), height);

	update();
}

QRectF ElementItem::boundingRect() const
{
	QRectF r = box;
	if (ed.maxWidth > 0) {
		double left = ed.align == "center" ? -ed.maxWidth / 2.0 : (ed.align == "right" ? -ed.maxWidth : 0.0);
		r = r.united(QRectF(left, 0, ed.maxWidth, box.height()));
	}
	double m = (ed.outlineEnabled ? ed.outlineSize : 0) + 4;
	return r.adjusted(-m, -m, m, m);
}

void ElementItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setRenderHint(QPainter::TextAntialiasing, true);

	// 「配置したまま非表示」のパーツは薄く表示する
	if (!ed.isVisible)
		painter->setOpacity(0.35);

	QPainterPath path;
	for (int i = 0; i < lines.size(); ++i) {
		double x = box.left();
		if (ed.align == "center")
			x += (box.width() - lineWidths[i]) / 2.0;
		else if (ed.align == "right")
			x += box.width() - lineWidths[i];
		double y = box.top() + i * lineHeight + baselineOffset;
		path.addText(x, y, renderFont, lines[i]);
	}

	if (ed.outlineEnabled && ed.outlineSize > 0) {
		QPen outlinePen(QColor(ed.outlineColor), ed.outlineSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		painter->strokePath(path, outlinePen);
	}
	painter->fillPath(path, QColor(ed.color));

	painter->setOpacity(1.0);
	double scale = painter->worldTransform().m11();
	if (scale <= 0)
		scale = 1;

	// 枠線（選択中は青、ホバー中は白、それ以外は薄い点線）
	QPen framePen;
	framePen.setCosmetic(true);
	if (isSelected()) {
		framePen.setColor(QColor(80, 170, 255));
		framePen.setWidthF(2);
	} else if (hovered) {
		framePen.setColor(QColor(255, 255, 255, 160));
		framePen.setWidthF(1);
	} else {
		framePen.setColor(QColor(255, 255, 255, 60));
		framePen.setStyle(Qt::DotLine);
	}
	painter->setPen(framePen);
	painter->setBrush(Qt::NoBrush);
	painter->drawRect(box);

	if (isSelected()) {
		// 最大幅の範囲
		if (ed.maxWidth > 0) {
			double left = ed.align == "center" ? -ed.maxWidth / 2.0
							   : (ed.align == "right" ? -ed.maxWidth : 0.0);
			QPen maxPen(QColor(255, 170, 60));
			maxPen.setCosmetic(true);
			maxPen.setStyle(Qt::DashLine);
			painter->setPen(maxPen);
			painter->drawRect(QRectF(left, 0, ed.maxWidth, box.height()));
		}
		// 揃えの基準点（X/Y 座標が指している位置）
		double len = 6.0 / scale;
		QPen anchorPen(QColor(255, 80, 80));
		anchorPen.setCosmetic(true);
		anchorPen.setWidthF(2);
		painter->setPen(anchorPen);
		painter->drawLine(QPointF(-len, 0), QPointF(len, 0));
		painter->drawLine(QPointF(0, -len), QPointF(0, len));
	}

	painter->restore();
}

void ElementItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
	hovered = true;
	update();
	QGraphicsItem::hoverEnterEvent(event);
}

void ElementItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
	hovered = false;
	update();
	QGraphicsItem::hoverLeaveEvent(event);
}

// ============================================================
// CanvasView
// ============================================================

CanvasView::CanvasView(QGraphicsScene *scene, QWidget *parent) : QGraphicsView(scene, parent)
{
	setRenderHint(QPainter::Antialiasing, true);
	setRenderHint(QPainter::SmoothPixmapTransform, true);
	setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
	setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	setResizeAnchor(QGraphicsView::AnchorViewCenter);
	setDragMode(QGraphicsView::RubberBandDrag);
	setRubberBandSelectionMode(Qt::IntersectsItemShape);
	setMouseTracking(true);
	setAcceptDrops(true);
	setFocusPolicy(Qt::StrongFocus);
	setBackgroundBrush(QColor(32, 32, 32));

	connect(scene, &QGraphicsScene::selectionChanged, this, [this]() { viewport()->update(); });
}

void CanvasView::setBackgroundImage(const QPixmap &pixmap)
{
	background = pixmap;
	resetCachedContent();
	viewport()->update();
}

void CanvasView::setGridVisible(bool on)
{
	gridVisible = on;
	resetCachedContent();
	viewport()->update();
}

void CanvasView::setGridSize(int size)
{
	gridSize = std::max(2, size);
	resetCachedContent();
	viewport()->update();
}

void CanvasView::setSafeAreaVisible(bool on)
{
	safeAreaVisible = on;
	viewport()->update();
}

void CanvasView::setFitMode()
{
	fitMode = true;
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	fitInView(QRectF(-20, -20, kCanvasWidth + 40, kCanvasHeight + 40), Qt::KeepAspectRatio);
	emit zoomChanged(zoom());
}

void CanvasView::setZoom(double z)
{
	fitMode = false;
	setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	z = std::clamp(z, 0.1, 4.0);
	resetTransform();
	scale(z, z);
	emit zoomChanged(zoom());
}

double CanvasView::zoom() const
{
	return transform().m11();
}

void CanvasView::setPlacingItem(ElementItem *item)
{
	if (ghost && ghost != item)
		ghost->setVisible(false);
	ghost = item;
	if (ghost) {
		setCursor(Qt::CrossCursor);
		setDragMode(QGraphicsView::NoDrag);
		QPoint p = mapFromGlobal(QCursor::pos());
		if (viewport()->rect().contains(p)) {
			ghost->setPos(anchorForCenter(ghost, mapToScene(p)));
			ghost->setVisible(true);
		} else {
			ghost->setVisible(false);
		}
	} else {
		unsetCursor();
		setDragMode(QGraphicsView::RubberBandDrag);
	}
}

double CanvasView::snapToGrid(double v) const
{
	return std::round(v / gridSize) * gridSize;
}

QPointF CanvasView::clampDelta(const QRectF &rect, QPointF delta) const
{
	QRectF moved = rect.translated(delta);
	if (rect.width() <= kCanvasWidth) {
		if (moved.left() < 0)
			delta.rx() -= moved.left();
		else if (moved.right() > kCanvasWidth)
			delta.rx() -= moved.right() - kCanvasWidth;
	}
	if (rect.height() <= kCanvasHeight) {
		if (moved.top() < 0)
			delta.ry() -= moved.top();
		else if (moved.bottom() > kCanvasHeight)
			delta.ry() -= moved.bottom() - kCanvasHeight;
	}
	return delta;
}

QPointF CanvasView::anchorForCenter(const ElementItem *item, const QPointF &center) const
{
	QRectF r = item->textRect();
	QPointF anchor = center - r.center();
	if (snapEnabled) {
		double left = anchor.x() + r.left();
		double top = anchor.y() + r.top();
		anchor.rx() += snapToGrid(left) - left;
		anchor.ry() += snapToGrid(top) - top;
	}
	anchor += clampDelta(r.translated(anchor), QPointF(0, 0));
	return anchor;
}

QRectF CanvasView::selectionRect() const
{
	QRectF r;
	for (QGraphicsItem *gi : scene()->selectedItems()) {
		if (auto *item = qgraphicsitem_cast<ElementItem *>(gi))
			r = r.isNull() ? item->sceneTextRect() : r.united(item->sceneTextRect());
	}
	return r;
}

QPointF CanvasView::snapDelta(const QRectF &rect, QPointF delta, bool allowSnap)
{
	guideXs.clear();
	guideYs.clear();

	if (allowSnap) {
		QRectF moved = rect.translated(delta);
		double threshold = 8.0 / std::max(zoom(), 0.01);

		// 吸着先: 画面の端と中央、他のパーツの端と中央
		QList<double> targetXs = {0, kCanvasWidth / 2, kCanvasWidth};
		QList<double> targetYs = {0, kCanvasHeight / 2, kCanvasHeight};
		for (QGraphicsItem *gi : scene()->items()) {
			auto *item = qgraphicsitem_cast<ElementItem *>(gi);
			if (!item || !item->isVisible() || item == ghost || dragItems.contains(item->id()))
				continue;
			QRectF r = item->sceneTextRect();
			targetXs << r.left() << r.center().x() << r.right();
			targetYs << r.top() << r.center().y() << r.bottom();
		}

		auto findSnap = [threshold](const QList<double> &targets, const QList<double> &edges, double &adjust,
					    double &guide) {
			double best = std::numeric_limits<double>::max();
			for (double t : targets) {
				for (double e : edges) {
					double d = t - e;
					if (std::abs(d) < threshold && std::abs(d) < std::abs(best)) {
						best = d;
						guide = t;
					}
				}
			}
			if (best == std::numeric_limits<double>::max())
				return false;
			adjust = best;
			return true;
		};

		double adjust = 0, guide = 0;
		if (findSnap(targetXs, {moved.left(), moved.center().x(), moved.right()}, adjust, guide)) {
			delta.rx() += adjust;
			guideXs << guide;
		} else if (snapEnabled) {
			delta.rx() += snapToGrid(moved.left()) - moved.left();
		}
		if (findSnap(targetYs, {moved.top(), moved.center().y(), moved.bottom()}, adjust, guide)) {
			delta.ry() += adjust;
			guideYs << guide;
		} else if (snapEnabled) {
			delta.ry() += snapToGrid(moved.top()) - moved.top();
		}
	}

	return clampDelta(rect, delta);
}

ElementItem *CanvasView::elementAt(const QPoint &viewPos) const
{
	for (QGraphicsItem *gi : items(viewPos)) {
		auto *item = qgraphicsitem_cast<ElementItem *>(gi);
		if (item && item != ghost && item->isVisible())
			return item;
	}
	return nullptr;
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
	QPoint vp = event->position().toPoint();

	if (ghost) {
		if (event->button() == Qt::LeftButton && ghost->isVisible()) {
			// 受け取った側で ghost が破棄されるので、値をコピーしてから渡す
			const QString id = ghost->id();
			const QPointF anchor = ghost->pos();
			emit placeRequested(id, anchor);
		}
		else if (event->button() == Qt::RightButton)
			emit escapePressed();
		return;
	}

	ElementItem *item = elementAt(vp);

	if (event->button() == Qt::RightButton) {
		if (item && !item->isSelected()) {
			scene()->clearSelection();
			item->setSelected(true);
		}
		if (item)
			emit contextMenuRequested(event->globalPosition().toPoint());
		return;
	}

	if (event->button() == Qt::LeftButton && item) {
		setFocus();
		if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
			item->setSelected(!item->isSelected());
			return;
		}
		if (!item->isSelected()) {
			scene()->clearSelection();
			item->setSelected(true);
		}
		dragging = true;
		dragMoved = false;
		dragStartScene = mapToScene(vp);
		dragStartPos.clear();
		dragItems.clear();
		for (QGraphicsItem *gi : scene()->selectedItems()) {
			if (auto *sel = qgraphicsitem_cast<ElementItem *>(gi)) {
				dragStartPos[sel->id()] = sel->pos();
				dragItems[sel->id()] = sel;
			}
		}
		dragStartRect = selectionRect();
		return;
	}

	QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
	QPoint vp = event->position().toPoint();
	QPointF sp = mapToScene(vp);
	emit cursorMoved(sp);

	if (ghost) {
		ghost->setPos(anchorForCenter(ghost, sp));
		ghost->setVisible(true);
		viewport()->update();
		return;
	}

	if (dragging && (event->buttons() & Qt::LeftButton)) {
		QPointF delta = sp - dragStartScene;
		if (!dragMoved) {
			QPoint startView = mapFromScene(dragStartScene);
			if ((vp - startView).manhattanLength() < 3)
				return;
			dragMoved = true;
		}
		bool allowSnap = snapEnabled && !(event->modifiers() & Qt::AltModifier);
		delta = snapDelta(dragStartRect, delta, allowSnap);
		for (auto it = dragItems.begin(); it != dragItems.end(); ++it)
			it.value()->setPos(dragStartPos[it.key()] + delta);
		viewport()->update();
		return;
	}

	QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
	if (dragging && event->button() == Qt::LeftButton) {
		dragging = false;
		guideXs.clear();
		guideYs.clear();
		if (dragMoved)
			emit itemsMoved(dragStartPos);
		dragItems.clear();
		viewport()->update();
		return;
	}
	QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
	if (event->modifiers() & Qt::ControlModifier) {
		double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
		fitMode = false;
		setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		double z = std::clamp(zoom() * factor, 0.1, 4.0);
		scale(z / zoom(), z / zoom());
		emit zoomChanged(zoom());
		event->accept();
		return;
	}
	QGraphicsView::wheelEvent(event);
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
	int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
	switch (event->key()) {
	case Qt::Key_Left:
		emit nudgeRequested(-step, 0);
		return;
	case Qt::Key_Right:
		emit nudgeRequested(step, 0);
		return;
	case Qt::Key_Up:
		emit nudgeRequested(0, -step);
		return;
	case Qt::Key_Down:
		emit nudgeRequested(0, step);
		return;
	case Qt::Key_Delete:
	case Qt::Key_Backspace:
		emit deleteRequested();
		return;
	case Qt::Key_Escape:
		emit escapePressed();
		return;
	default:
		break;
	}
	QGraphicsView::keyPressEvent(event);
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
	QGraphicsView::resizeEvent(event);
	if (fitMode)
		setFitMode();
}

void CanvasView::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasFormat(kElementMimeType))
		event->acceptProposedAction();
	else
		event->ignore();
}

void CanvasView::dragMoveEvent(QDragMoveEvent *event)
{
	if (event->mimeData()->hasFormat(kElementMimeType))
		event->acceptProposedAction();
	else
		event->ignore();
}

void CanvasView::dragLeaveEvent(QDragLeaveEvent *event)
{
	event->accept();
}

void CanvasView::dropEvent(QDropEvent *event)
{
	if (!event->mimeData()->hasFormat(kElementMimeType)) {
		event->ignore();
		return;
	}
	QString id = QString::fromUtf8(event->mimeData()->data(kElementMimeType));
	event->acceptProposedAction();
	emit dropRequested(id, mapToScene(event->position().toPoint()));
}

void CanvasView::drawBackground(QPainter *painter, const QRectF &rect)
{
	painter->fillRect(rect, QColor(32, 32, 32));

	const QRectF canvas(0, 0, kCanvasWidth, kCanvasHeight);
	QRectF area = rect & canvas;
	if (area.isEmpty())
		return;

	// 透過を表す市松模様
	const double cell = 40;
	const QColor c1(46, 46, 46), c2(56, 56, 56);
	for (double y = std::floor(area.top() / cell) * cell; y < area.bottom(); y += cell) {
		for (double x = std::floor(area.left() / cell) * cell; x < area.right(); x += cell) {
			bool odd = ((int)(x / cell) + (int)(y / cell)) % 2;
			painter->fillRect(QRectF(x, y, cell, cell) & canvas, odd ? c1 : c2);
		}
	}

	if (!background.isNull())
		painter->drawPixmap(canvas, background, QRectF(background.rect()));

	// グリッド（細かすぎて見えない倍率では描かない）
	if (gridVisible && gridSize * zoom() >= 4) {
		QPen gridPen(QColor(255, 255, 255, 25));
		gridPen.setCosmetic(true);
		painter->setPen(gridPen);
		for (double x = std::ceil(area.left() / gridSize) * gridSize; x <= area.right(); x += gridSize)
			painter->drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
		for (double y = std::ceil(area.top() / gridSize) * gridSize; y <= area.bottom(); y += gridSize)
			painter->drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
	}

	// 画面中央の線
	QPen centerPen(QColor(255, 255, 255, 50));
	centerPen.setCosmetic(true);
	centerPen.setStyle(Qt::DashLine);
	painter->setPen(centerPen);
	painter->drawLine(QPointF(kCanvasWidth / 2, 0), QPointF(kCanvasWidth / 2, kCanvasHeight));
	painter->drawLine(QPointF(0, kCanvasHeight / 2), QPointF(kCanvasWidth, kCanvasHeight / 2));

	QPen borderPen(QColor(120, 120, 120));
	borderPen.setCosmetic(true);
	painter->setPen(borderPen);
	painter->setBrush(Qt::NoBrush);
	painter->drawRect(canvas);
}

void CanvasView::drawForeground(QPainter *painter, const QRectF &)
{
	if (safeAreaVisible) {
		QPen safePen(QColor(255, 220, 80, 140));
		safePen.setCosmetic(true);
		safePen.setStyle(Qt::DashLine);
		painter->setPen(safePen);
		painter->setBrush(Qt::NoBrush);
		painter->drawRect(QRectF(kCanvasWidth * 0.05, kCanvasHeight * 0.05, kCanvasWidth * 0.9, kCanvasHeight * 0.9));
		painter->drawRect(QRectF(kCanvasWidth * 0.1, kCanvasHeight * 0.1, kCanvasWidth * 0.8, kCanvasHeight * 0.8));
	}

	// スマートガイド
	if (!guideXs.isEmpty() || !guideYs.isEmpty()) {
		QPen guidePen(QColor(255, 60, 200));
		guidePen.setCosmetic(true);
		painter->setPen(guidePen);
		for (double x : guideXs)
			painter->drawLine(QPointF(x, 0), QPointF(x, kCanvasHeight));
		for (double y : guideYs)
			painter->drawLine(QPointF(0, y), QPointF(kCanvasWidth, y));
	}

	// 選択中・配置中のパーツ名（倍率に関係なく同じ大きさで表示）
	QList<ElementItem *> tagged;
	for (QGraphicsItem *gi : scene()->selectedItems()) {
		if (auto *item = qgraphicsitem_cast<ElementItem *>(gi))
			tagged << item;
	}
	if (ghost && ghost->isVisible())
		tagged << ghost;
	if (tagged.isEmpty())
		return;

	painter->save();
	painter->resetTransform();
	QFont tagFont = font();
	tagFont.setPointSizeF(std::max(8.0, tagFont.pointSizeF() * 0.9));
	painter->setFont(tagFont);
	QFontMetrics fm(tagFont);
	for (ElementItem *item : tagged) {
		QRect vr = mapFromScene(item->sceneTextRect()).boundingRect();
		QString label = item->name();
		QRect tag(0, 0, fm.horizontalAdvance(label) + 10, fm.height() + 4);
		tag.moveBottomLeft(vr.topLeft() - QPoint(0, 2));
		if (tag.top() < 0)
			tag.moveTopLeft(vr.bottomLeft() + QPoint(0, 2));
		painter->fillRect(tag, item == ghost ? QColor(90, 90, 90, 220) : QColor(40, 120, 220, 230));
		painter->setPen(Qt::white);
		painter->drawText(tag, Qt::AlignCenter, label);
	}
	painter->restore();
}
