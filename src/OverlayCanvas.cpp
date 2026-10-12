#include "OverlayCanvas.h"

#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QTextLayout>
#include "RichTextMarkup.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QScrollBar>
#include <cmath>
#include <limits>
#include <memory>

// ============================================================
// ElementItem
// ============================================================

ElementItem::ElementItem(const QString &id, const QString &name) : elementId(id), displayName(name)
{
	setFlag(QGraphicsItem::ItemIsSelectable, true);
	setAcceptHoverEvents(true);
}

namespace {

// 書式付きの文字を、要素の文字サイズ px でレイアウトしたもの
struct RichLayout {
	std::unique_ptr<QTextLayout> layout;
	QList<QFont> runFonts; // RichText::runs と同じ並び
	double width = 0;      // 最も長い行の幅
};

QFont fontForRun(const QFont &base, int px, const RichTextRun &run)
{
	QFont f = base;
	f.setPixelSize(std::max(1, (int)std::lround(px * run.scale)));
	if (run.bold)
		f.setBold(true);
	if (run.italic)
		f.setItalic(true);
	return f;
}

// wrapWidth > 0 なら折り返す。0 なら改行（<br>）以外では折り返さない
void layoutRichText(const RichText &rt, const QString &shown, const QFont &baseFont, double wrapWidth, RichLayout &out)
{
	int px = baseFont.pixelSize();
	out.layout = std::make_unique<QTextLayout>(shown, baseFont);
	out.runFonts.clear();
	out.width = 0;

	QList<QTextLayout::FormatRange> formats;
	for (const RichTextRun &run : rt.runs) {
		QFont f = fontForRun(baseFont, px, run);
		out.runFonts << f;
		QTextLayout::FormatRange fr;
		fr.start = run.start;
		fr.length = run.length;
		fr.format.setFont(f);
		formats << fr;
	}
	out.layout->setFormats(formats);

	QTextOption opt;
	opt.setWrapMode(wrapWidth > 0 ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
	out.layout->setTextOption(opt);

	out.layout->beginLayout();
	double y = 0;
	for (QTextLine line = out.layout->createLine(); line.isValid(); line = out.layout->createLine()) {
		line.setLineWidth(wrapWidth > 0 ? wrapWidth : 1e7);
		line.setPosition(QPointF(0, y));
		y += line.height();
		out.width = std::max(out.width, line.naturalTextWidth());
	}
	out.layout->endLayout();
}

// CSS の line-height: 1.1 と同じ考え方で、フォントごとの上下の高さ（ベースラインから）を求める
void addLineBox(const QFont &font, double &above, double &below)
{
	QFontMetricsF fm(font);
	double halfLeading = (font.pixelSize() * 1.1 - (fm.ascent() + fm.descent())) / 2.0;
	above = std::max(above, fm.ascent() + halfLeading);
	below = std::max(below, fm.descent() + halfLeading);
}

} // namespace

void ElementItem::setContent(const ElementData &data, const QString &text)
{
	prepareGeometryChange();
	ed = data;

	// index.html と同じく、保存されている文字サイズの数値を px として扱う
	int px = data.font.pointSize() > 0 ? data.font.pointSize()
					   : (data.font.pixelSize() > 0 ? data.font.pixelSize() : 48);
	QFont baseFont = data.font;
	baseFont.setPixelSize(std::max(1, px));

	// index.html では innerHTML として入るので、タグ（大きさ・色の部分指定など）を解釈して描く
	RichText rt = parseRichText(text);
	bool wrap = data.maxWidth > 0 && data.wrapMode == "wrap";
	QString shown = rt.text;
	// 元の改行文字は、折り返しあり（pre-wrap）なら改行、なし（nowrap）なら空白として表示される
	shown.replace('\n', wrap ? QChar(QChar::LineSeparator) : QChar(' '));

	RichLayout rl;
	layoutRichText(rt, shown, baseFont, wrap ? data.maxWidth : 0, rl);
	double width = rl.width;

	if (wrap) {
		// 自動改行: 枠の幅は最大幅で固定
		width = data.maxWidth;
	} else if (data.maxWidth > 0 && data.wrapMode == "shrink" && rl.width > data.maxWidth) {
		// 自動縮小: 収まるように要素全体の文字サイズを下げる（部分指定の倍率はそのまま。index.html と同じ計算）
		int shrunk = std::max(1, (int)std::floor(px * (double)data.maxWidth / rl.width));
		baseFont.setPixelSize(shrunk);
		layoutRichText(rt, shown, baseFont, 0, rl);
		width = rl.width;
	}

	double left = 0;
	if (data.align == "center")
		left = -width / 2.0;
	else if (data.align == "right")
		left = -width;

	// 行ごとに高さとベースラインを決め、範囲ごとの文字を輪郭にする
	outlinePath = QPainterPath();
	fills.clear();
	QMap<QRgb, int> fillIndex;
	QColor baseColor(data.color);
	double y = 0;

	QTextLayout &layout = *rl.layout;
	for (int li = 0; li < layout.lineCount(); ++li) {
		QTextLine line = layout.lineAt(li);
		int lineStart = line.textStart();
		int lineEnd = lineStart + line.textLength();

		double above = 0, below = 0;
		addLineBox(baseFont, above, below);
		for (int ri = 0; ri < rt.runs.size(); ++ri) {
			const RichTextRun &run = rt.runs[ri];
			if (run.start < lineEnd && run.start + run.length > lineStart)
				addLineBox(rl.runFonts[ri], above, below);
		}
		double baseline = y + above;

		double lineWidth = line.naturalTextWidth();
		double x0 = left;
		if (data.align == "center")
			x0 += (width - lineWidth) / 2.0;
		else if (data.align == "right")
			x0 += width - lineWidth;

		for (int ri = 0; ri < rt.runs.size(); ++ri) {
			const RichTextRun &run = rt.runs[ri];
			int from = std::max(run.start, lineStart);
			int to = std::min(run.start + run.length, lineEnd);
			if (from >= to)
				continue;
			QString segment = shown.mid(from, to - from);
			segment.remove(QChar(QChar::LineSeparator));
			if (segment.trimmed().isEmpty())
				continue;

			QColor color = run.color.isEmpty() ? baseColor : QColor(run.color);
			if (!color.isValid())
				color = baseColor;

			QPainterPath glyphs;
			glyphs.addText(x0 + line.cursorToX(from), baseline, rl.runFonts[ri], segment);
			outlinePath.addPath(glyphs);
			auto it = fillIndex.find(color.rgba());
			if (it == fillIndex.end()) {
				fillIndex.insert(color.rgba(), fills.size());
				fills << qMakePair(color, glyphs);
			} else {
				fills[it.value()].second.addPath(glyphs);
			}
		}

		y += above + below;
	}
	if (layout.lineCount() == 0)
		y = baseFont.pixelSize() * 1.1;

	box = QRectF(left, 0, std::max(width, 1.0), y);

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

	if (ed.outlineEnabled && ed.outlineSize > 0) {
		QPen outlinePen(QColor(ed.outlineColor), ed.outlineSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		painter->strokePath(outlinePath, outlinePen);
	}
	for (const auto &fill : fills)
		painter->fillPath(fill.second, fill.first);

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
