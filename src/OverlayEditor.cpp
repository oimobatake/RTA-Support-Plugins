#include "OverlayEditor.h"
#include "OverlayCanvas.h"
#include "ElementCatalog.h"
#include "RichTextMarkup.h"

#include <QBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QMimeData>
#include <QUndoStack>
#include <QUndoCommand>
#include <QTimer>
#include <QPainter>
#include <QFileDialog>
#include <QFontDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QGraphicsScene>
#include <QStyle>
#include <cmath>
#include <algorithm>

namespace {

// --- 元に戻す／やり直し: 1レイアウト分の変更前後を丸ごと持つ ---
class SnapshotCommand : public QUndoCommand {
public:
	SnapshotCommand(OverlayEditor *editor, const QString &layout, const OverlayEditor::Elements &before,
			const OverlayEditor::Elements &after, const QString &text, const QString &mergeKey)
		: QUndoCommand(text),
		  editor(editor),
		  layout(layout),
		  before(before),
		  after(after),
		  mergeKey(mergeKey)
	{
	}

	void undo() override { editor->restoreLayout(layout, before); }
	void redo() override
	{
		// 最初の redo は push 時に呼ばれる。変更は適用済みなので何もしない
		if (first) {
			first = false;
			return;
		}
		editor->restoreLayout(layout, after);
	}
	int id() const override { return mergeKey.isEmpty() ? -1 : 1; }
	bool mergeWith(const QUndoCommand *other) override
	{
		auto *o = static_cast<const SnapshotCommand *>(other);
		if (o->layout != layout || o->mergeKey != mergeKey)
			return false;
		after = o->after;
		return true;
	}

private:
	OverlayEditor *editor;
	QString layout;
	OverlayEditor::Elements before;
	OverlayEditor::Elements after;
	QString mergeKey;
	bool first = true;
};

// --- パーツ一覧（キャンバスへドラッグできるツリー） ---
class PartsTree : public QTreeWidget {
public:
	using QTreeWidget::QTreeWidget;

protected:
	QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
	{
		if (items.isEmpty())
			return nullptr;
		QString id = items.first()->data(0, Qt::UserRole).toString();
		if (id.isEmpty())
			return nullptr;
		auto *mime = new QMimeData();
		mime->setData(kElementMimeType, id.toUtf8());
		return mime;
	}
};

// 配置状態を表すアイコン（● 配置済み / ◐ 配置済みだが非表示 / ○ 未配置）
QIcon stateIcon(bool placed, bool visible)
{
	QPixmap pm(14, 14);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r(2, 2, 10, 10);
	if (!placed) {
		p.setPen(QPen(QColor(150, 150, 150), 1.5));
		p.setBrush(Qt::NoBrush);
	} else if (!visible) {
		p.setPen(QPen(QColor(130, 130, 130), 1));
		p.setBrush(QColor(110, 110, 110));
	} else {
		p.setPen(QPen(QColor(60, 160, 60), 1));
		p.setBrush(QColor(110, 210, 90));
	}
	p.drawEllipse(r);
	return QIcon(pm);
}

// 色ボタンの見た目を更新
void setColorButton(QPushButton *btn, const QString &color)
{
	btn->setText(color.toUpper());
	QColor c(color);
	QString fg = c.lightness() > 140 ? "#000000" : "#FFFFFF";
	btn->setStyleSheet(QString("QPushButton { background-color: %1; color: %2; }").arg(c.name(), fg));
}

// --- 配置プリセット ---
struct PresetEntry {
	QString id;
	double x;
	double y;
	QString align;
};
struct Preset {
	QString name;
	std::vector<PresetEntry> entries;
};

const std::vector<Preset> &presets()
{
	static const std::vector<Preset> list = {
		{"1人走（右側に情報欄）",
		 {
			 {"GameTitle", 1600, 120, "center"},
			 {"Category", 1600, 200, "center"},
			 {"Hardware", 1600, 270, "center"},
			 {"EstimateTime", 1600, 340, "center"},
			 {"RunnerName_1", 1600, 600, "center"},
			 {"CountUpTimer", 1600, 780, "center"},
			 {"Commentator", 1600, 960, "center"},
		 }},
		{"2人並走（左右に画面、下に情報欄）",
		 {
			 {"RunnerName_1", 480, 700, "center"},
			 {"RunnerTimer_1", 480, 770, "center"},
			 {"RunnerName_2", 1440, 700, "center"},
			 {"RunnerTimer_2", 1440, 770, "center"},
			 {"CountUpTimer", 960, 700, "center"},
			 {"GameTitle", 960, 860, "center"},
			 {"Category", 960, 930, "center"},
			 {"Commentator", 960, 1000, "center"},
		 }},
		{"4人並走（4分割）",
		 {
			 {"RunnerName_1", 20, 470, "left"},
			 {"RunnerTimer_1", 940, 470, "right"},
			 {"RunnerName_2", 980, 470, "left"},
			 {"RunnerTimer_2", 1900, 470, "right"},
			 {"RunnerName_3", 20, 1010, "left"},
			 {"RunnerTimer_3", 940, 1010, "right"},
			 {"RunnerName_4", 980, 1010, "left"},
			 {"RunnerTimer_4", 1900, 1010, "right"},
			 {"CountUpTimer", 960, 514, "center"},
		 }},
		{"待機画面（次のゲーム3件）",
		 {
			 {"GameTitle", 960, 80, "center"},
			 {"Category", 960, 150, "center"},
			 {"NextGameTitle_1", 160, 320, "left"},
			 {"NextCategory_1", 160, 380, "left"},
			 {"NextRunnerName_1", 1760, 320, "right"},
			 {"NextEstimate_1", 1760, 380, "right"},
			 {"NextGameTitle_2", 160, 500, "left"},
			 {"NextCategory_2", 160, 560, "left"},
			 {"NextRunnerName_2", 1760, 500, "right"},
			 {"NextEstimate_2", 1760, 560, "right"},
			 {"NextGameTitle_3", 160, 680, "left"},
			 {"NextCategory_3", 160, 740, "left"},
			 {"NextRunnerName_3", 1760, 680, "right"},
			 {"NextEstimate_3", 1760, 740, "right"},
			 {"CountDownTimer", 960, 900, "center"},
		 }},
	};
	return list;
}

QPointF rounded(const QPointF &p)
{
	return QPointF(std::round(p.x()), std::round(p.y()));
}

// エディタの表示設定は OBS を閉じるまで覚えておく
struct EditorPrefs {
	bool grid = true;
	bool snap = true;
	int gridSize = 20;
	bool safeArea = false;
	bool live = true;
	QSize windowSize = QSize(1440, 860);
};
EditorPrefs &prefs()
{
	static EditorPrefs p;
	return p;
}

} // namespace

// ============================================================
// 構築
// ============================================================

OverlayEditor::OverlayEditor(const Layouts &layouts, const QString &currentLayout,
			     const std::map<QString, QString> &backgrounds, const QMap<QString, QString> &values,
			     QWidget *parent)
	: QDialog(parent),
	  layoutsData(layouts),
	  originalLayouts(layouts),
	  bgPaths(backgrounds),
	  values(values),
	  currentName(currentLayout)
{
	setWindowTitle("オーバーレイ ビジュアルエディタ");
	setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
	setSizeGripEnabled(true);
	setMinimumSize(900, 560);
	resize(prefs().windowSize);

	if (!layoutsData.count(currentName) && !layoutsData.empty())
		currentName = layoutsData.begin()->first;

	undoStack = new QUndoStack(this);
	liveTimer = new QTimer(this);
	liveTimer->setSingleShot(true);
	liveTimer->setInterval(80);
	connect(liveTimer, &QTimer::timeout, this, [this]() {
		if (!liveCallback || !liveCheck->isChecked())
			return;
		// ドラッグ中はまだデータに入っていないので、キャンバス上の位置を優先する
		Layouts preview = layoutsData;
		for (auto &[id, item] : items) {
			if (item->isVisible() && preview[currentName].count(id))
				preview[currentName][id].pos = rounded(item->pos());
		}
		liveCallback(preview);
	});

	scene = new QGraphicsScene(this);
	scene->setSceneRect(-300, -300, kCanvasWidth + 600, kCanvasHeight + 600);
	view = new CanvasView(scene, this);

	for (const auto &info : elementCatalog()) {
		auto *item = new ElementItem(info.id, info.name);
		item->setVisible(false);
		scene->addItem(item);
		items[info.id] = item;
	}

	// --- 全体のレイアウト ---
	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(8, 8, 8, 8);
	mainLayout->addWidget(buildToolbar());

	auto *splitter = new QSplitter(Qt::Horizontal, this);
	splitter->addWidget(buildPalette());
	splitter->addWidget(view);
	splitter->addWidget(buildProperties());
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setStretchFactor(2, 0);
	splitter->setSizes({230, 900, 300});
	splitter->setCollapsible(1, false);
	mainLayout->addWidget(splitter, 1);

	// --- 下部（ヒントとボタン） ---
	auto *bottom = new QHBoxLayout();
	hintLabel = new QLabel(this);
	hintLabel->setStyleSheet("color: palette(mid);");
	coordLabel = new QLabel(this);
	coordLabel->setMinimumWidth(130);
	zoomLabel = new QLabel(this);
	zoomLabel->setMinimumWidth(70);

	auto *btnCancel = new QPushButton("キャンセル", this);
	btnCancel->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
	auto *btnApply = new QPushButton("適用", this);
	btnApply->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
	btnApply->setDefault(false);
	btnApply->setAutoDefault(false);
	btnCancel->setAutoDefault(false);
	btnApply->setMinimumWidth(120);
	btnApply->setStyleSheet("QPushButton { background-color: #deff9a; color: #000; font-weight: bold; }"
				"QPushButton:hover { background-color: #c0ff60; }");
	connect(btnApply, &QPushButton::clicked, this, &QDialog::accept);
	connect(btnCancel, &QPushButton::clicked, this, &OverlayEditor::reject);

	bottom->addWidget(hintLabel, 1);
	bottom->addWidget(coordLabel);
	bottom->addWidget(zoomLabel);
	bottom->addSpacing(12);
	bottom->addWidget(btnCancel);
	bottom->addWidget(btnApply);
	mainLayout->addLayout(bottom);

	// --- キャンバスのイベント ---
	connect(view, &CanvasView::placeRequested, this, &OverlayEditor::placeElement);
	connect(view, &CanvasView::dropRequested, this, [this](const QString &id, const QPointF &center) {
		if (!items.count(id))
			return;
		items[id]->setContent(cur()[id], textFor(id));
		placeElement(id, view->anchorForCenter(items[id], center));
	});
	connect(view, &CanvasView::itemsMoved, this, &OverlayEditor::onItemsMoved);
	connect(view, &CanvasView::nudgeRequested, this, &OverlayEditor::nudge);
	connect(view, &CanvasView::deleteRequested, this, &OverlayEditor::unplaceSelected);
	connect(view, &CanvasView::escapePressed, this, [this]() {
		if (ghost)
			stopPlacing();
		else
			scene->clearSelection();
	});
	connect(view, &CanvasView::contextMenuRequested, this, &OverlayEditor::showContextMenu);
	connect(view, &CanvasView::cursorMoved, this, [this](const QPointF &p) {
		coordLabel->setText(QString("X: %1  Y: %2").arg(std::round(p.x())).arg(std::round(p.y())));
		if (ghost)
			scheduleLivePreview();
	});
	connect(view, &CanvasView::zoomChanged, this,
		[this](double z) {
			zoomLabel->setText(QString("%1%").arg(std::round(z * 100)));
			int idx = view->isFitMode() ? 0 : zoomBox->findData(std::round(z * 100) / 100.0);
			zoomBox->setCurrentIndex(idx);
		});
	connect(scene, &QGraphicsScene::selectionChanged, this, [this]() {
		if (syncing)
			return;
		syncPaletteSelection();
		syncPropertiesFromSelection();
	});

	// --- ショートカット ---
	QAction *undoAction = undoStack->createUndoAction(this, "元に戻す");
	undoAction->setShortcut(QKeySequence::Undo);
	QAction *redoAction = undoStack->createRedoAction(this, "やり直し");
	redoAction->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Shift+Z")});
	addAction(undoAction);
	addAction(redoAction);

	auto addViewShortcut = [this](const QKeySequence &key, const std::function<void()> &fn) {
		auto *a = new QAction(view);
		a->setShortcut(key);
		a->setShortcutContext(Qt::WidgetShortcut);
		connect(a, &QAction::triggered, this, fn);
		view->addAction(a);
	};
	addViewShortcut(QKeySequence::SelectAll, [this]() {
		for (auto &[id, item] : items)
			if (item->isVisible())
				item->setSelected(true);
	});
	addViewShortcut(QKeySequence::Copy, [this]() { copyStyle(); });
	addViewShortcut(QKeySequence::Paste, [this]() { pasteStyle(); });

	// --- 初期表示 ---
	view->setGridVisible(prefs().grid);
	view->setSnapEnabled(prefs().snap);
	view->setGridSize(prefs().gridSize);
	view->setSafeAreaVisible(prefs().safeArea);

	refreshItems();
	refreshPalette();
	updateBackground();
	syncPropertiesFromSelection();
	view->setFitMode();

	bool anyPlaced = false;
	for (auto &[id, ed] : cur())
		anyPlaced |= ed.isPlaced;
	showHint(anyPlaced ? "パーツをドラッグで移動、矢印キーで1px（Shift+矢印で10px）移動できます"
			   : "左の一覧からパーツを選び、キャンバスをクリックして配置します（ドラッグ＆ドロップも可）");
}

OverlayEditor::~OverlayEditor()
{
	// シーン破棄時の選択変更通知が、破棄済みのメンバーに届かないようにする
	if (scene)
		scene->blockSignals(true);
}

QWidget *OverlayEditor::buildToolbar()
{
	auto *bar = new QWidget(this);
	auto *h = new QHBoxLayout(bar);
	h->setContentsMargins(0, 0, 0, 4);

	// レイアウト切替
	h->addWidget(new QLabel("レイアウト:", bar));
	layoutBox = new QComboBox(bar);
	for (auto &[name, elements] : layoutsData)
		layoutBox->addItem(name);
	layoutBox->setCurrentText(currentName);
	connect(layoutBox, &QComboBox::currentTextChanged, this, [this](const QString &name) {
		if (!name.isEmpty() && name != currentName)
			switchLayout(name);
	});
	h->addWidget(layoutBox);

	// 配置メニュー（プリセット・複製・一括解除）
	auto *placeBtn = new QToolButton(bar);
	placeBtn->setText("配置 ▾");
	placeBtn->setPopupMode(QToolButton::InstantPopup);
	auto *placeMenu = new QMenu(placeBtn);
	QMenu *presetMenu = placeMenu->addMenu("プリセットで配置");
	for (int i = 0; i < (int)presets().size(); ++i)
		presetMenu->addAction(presets()[i].name, this, [this, i]() { applyPreset(i); });
	QMenu *copyMenu = placeMenu->addMenu("別のレイアウトから複製");
	connect(copyMenu, &QMenu::aboutToShow, this, [this, copyMenu]() {
		copyMenu->clear();
		for (auto &[name, elements] : layoutsData) {
			if (name == currentName)
				continue;
			copyMenu->addAction(name, this, [this, name = name]() { copyFromLayout(name); });
		}
		if (copyMenu->isEmpty())
			copyMenu->addAction("（他のレイアウトがありません）")->setEnabled(false);
	});
	placeMenu->addSeparator();
	placeMenu->addAction("すべて未配置に戻す", this, &OverlayEditor::unplaceAll);
	placeBtn->setMenu(placeMenu);
	h->addWidget(placeBtn);

	// 背景画像
	auto *bgBtn = new QToolButton(bar);
	bgBtn->setText("背景画像 ▾");
	bgBtn->setPopupMode(QToolButton::InstantPopup);
	auto *bgMenu = new QMenu(bgBtn);
	bgMenu->addAction("画像を選択…", this, [this]() {
		QString start = bgPaths.count(currentName) ? bgPaths[currentName] : QString();
		QString path = QFileDialog::getOpenFileName(this, "背景画像を選択（このレイアウト用）", start,
							    "画像 (*.png *.jpg *.jpeg *.bmp *.webp)");
		if (path.isEmpty())
			return;
		bgPaths[currentName] = path;
		updateBackground();
	});
	bgMenu->addAction("背景をなしにする", this, [this]() {
		bgPaths.erase(currentName);
		updateBackground();
	});
	bgBtn->setMenu(bgMenu);
	bgBtn->setToolTip("配信画面のフレーム画像などを背景に表示して、合わせて配置できます（OBSには出力されません）");
	h->addWidget(bgBtn);

	h->addSpacing(12);

	// ズーム
	h->addWidget(new QLabel("表示:", bar));
	zoomBox = new QComboBox(bar);
	zoomBox->addItem("全体表示", 0.0);
	for (int z : {25, 50, 75, 100, 150, 200})
		zoomBox->addItem(QString("%1%").arg(z), z / 100.0);
	connect(zoomBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
		double z = zoomBox->itemData(i).toDouble();
		if (z <= 0)
			view->setFitMode();
		else
			view->setZoom(z);
	});
	zoomBox->setToolTip("Ctrl+ホイールでも拡大縮小できます");
	h->addWidget(zoomBox);

	h->addSpacing(12);

	// グリッド・吸着
	gridCheck = new QCheckBox("グリッド", bar);
	gridCheck->setChecked(prefs().grid);
	connect(gridCheck, &QCheckBox::toggled, this, [this](bool on) {
		prefs().grid = on;
		view->setGridVisible(on);
	});
	h->addWidget(gridCheck);

	snapCheck = new QCheckBox("吸着", bar);
	snapCheck->setChecked(prefs().snap);
	snapCheck->setToolTip("グリッドと他のパーツの端・中央に吸着します（ドラッグ中に Alt を押すと一時的に無効）");
	connect(snapCheck, &QCheckBox::toggled, this, [this](bool on) {
		prefs().snap = on;
		view->setSnapEnabled(on);
	});
	h->addWidget(snapCheck);

	gridSizeBox = new QComboBox(bar);
	for (int s : {10, 20, 40})
		gridSizeBox->addItem(QString("%1px").arg(s), s);
	gridSizeBox->setCurrentIndex(gridSizeBox->findData(prefs().gridSize));
	connect(gridSizeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		prefs().gridSize = gridSizeBox->itemData(i).toInt();
		view->setGridSize(prefs().gridSize);
	});
	h->addWidget(gridSizeBox);

	safeAreaCheck = new QCheckBox("セーフエリア", bar);
	safeAreaCheck->setChecked(prefs().safeArea);
	safeAreaCheck->setToolTip("画面の 90% / 80% の範囲を点線で表示します");
	connect(safeAreaCheck, &QCheckBox::toggled, this, [this](bool on) {
		prefs().safeArea = on;
		view->setSafeAreaVisible(on);
	});
	h->addWidget(safeAreaCheck);

	h->addStretch();

	// 元に戻す／やり直し
	auto *undoBtn = new QToolButton(bar);
	undoBtn->setText("↶ 元に戻す");
	undoBtn->setToolTip("Ctrl+Z");
	undoBtn->setEnabled(false);
	connect(undoBtn, &QToolButton::clicked, undoStack, &QUndoStack::undo);
	connect(undoStack, &QUndoStack::canUndoChanged, undoBtn, &QToolButton::setEnabled);
	auto *redoBtn = new QToolButton(bar);
	redoBtn->setText("↷ やり直し");
	redoBtn->setToolTip("Ctrl+Y");
	redoBtn->setEnabled(false);
	connect(redoBtn, &QToolButton::clicked, undoStack, &QUndoStack::redo);
	connect(undoStack, &QUndoStack::canRedoChanged, redoBtn, &QToolButton::setEnabled);
	h->addWidget(undoBtn);
	h->addWidget(redoBtn);

	h->addSpacing(12);

	liveCheck = new QCheckBox("OBSにリアルタイム反映", bar);
	liveCheck->setChecked(prefs().live);
	liveCheck->setToolTip("編集中の内容をすぐ OBS に表示します。キャンセルすると開く前の状態に戻ります");
	connect(liveCheck, &QCheckBox::toggled, this, [this](bool on) {
		prefs().live = on;
		if (!liveCallback)
			return;
		if (on)
			scheduleLivePreview();
		else
			liveCallback(originalLayouts); // OBS 側を開いた時点の状態に戻す
	});
	h->addWidget(liveCheck);

	return bar;
}

QWidget *OverlayEditor::buildPalette()
{
	auto *panel = new QWidget(this);
	auto *v = new QVBoxLayout(panel);
	v->setContentsMargins(0, 0, 0, 0);

	auto *title = new QLabel("<b>パーツ一覧</b>", panel);
	v->addWidget(title);

	searchEdit = new QLineEdit(panel);
	searchEdit->setPlaceholderText("検索…");
	searchEdit->setClearButtonEnabled(true);
	connect(searchEdit, &QLineEdit::textChanged, this, [this]() { refreshPalette(); });
	v->addWidget(searchEdit);

	palette = new PartsTree(panel);
	palette->setHeaderHidden(true);
	palette->setSelectionMode(QAbstractItemView::ExtendedSelection);
	palette->setDragEnabled(true);
	palette->setDragDropMode(QAbstractItemView::DragOnly);
	palette->setRootIsDecorated(true);
	palette->setMinimumWidth(180);

	std::map<QString, QTreeWidgetItem *> groups;
	for (const auto &info : elementCatalog()) {
		QTreeWidgetItem *group = groups[info.group];
		if (!group) {
			group = new QTreeWidgetItem(palette, QStringList(info.group));
			group->setFlags(Qt::ItemIsEnabled);
			QFont f = group->font(0);
			f.setBold(true);
			group->setFont(0, f);
			groups[info.group] = group;
		}
		auto *item = new QTreeWidgetItem(group, QStringList(info.name));
		item->setData(0, Qt::UserRole, info.id);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
		paletteItems[info.id] = item;
	}
	palette->expandAll();
	v->addWidget(palette, 1);

	auto *legend = new QLabel("● 配置済み　◐ 非表示　○ 未配置\nダブルクリックで画面中央に配置", panel);
	legend->setStyleSheet("color: palette(mid); font-size: 11px;");
	v->addWidget(legend);

	connect(palette, &QTreeWidget::itemSelectionChanged, this, [this]() {
		if (syncing)
			return;
		QStringList ids;
		for (QTreeWidgetItem *it : palette->selectedItems()) {
			QString id = it->data(0, Qt::UserRole).toString();
			if (!id.isEmpty())
				ids << id;
		}

		// 未配置のパーツを1つ選んだら配置モードへ
		if (ids.size() == 1 && !cur()[ids.first()].isPlaced) {
			startPlacing(ids.first());
			return;
		}
		if (ghost)
			stopPlacing();

		syncing = true;
		scene->clearSelection();
		ElementItem *last = nullptr;
		for (const QString &id : ids) {
			if (cur()[id].isPlaced) {
				items[id]->setSelected(true);
				last = items[id];
			}
		}
		syncing = false;
		if (last)
			view->ensureVisible(last, 40, 40);
		syncPropertiesFromSelection();
		view->viewport()->update();
	});

	// 選択済みの未配置パーツをもう一度クリックしたときも配置モードに入る
	connect(palette, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) {
		QString id = it->data(0, Qt::UserRole).toString();
		if (!id.isEmpty() && !ghost && palette->selectedItems().size() == 1 && !cur()[id].isPlaced)
			startPlacing(id);
	});

	connect(palette, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
		QString id = it->data(0, Qt::UserRole).toString();
		if (id.isEmpty())
			return;
		if (!cur()[id].isPlaced) {
			items[id]->setContent(cur()[id], textFor(id));
			placeElement(id, view->anchorForCenter(items[id], QPointF(kCanvasWidth / 2, kCanvasHeight / 2)));
		} else {
			view->centerOn(items[id]);
		}
	});

	return panel;
}

QWidget *OverlayEditor::buildProperties()
{
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setMinimumWidth(270);

	propPanel = new QWidget(scroll);
	auto *v = new QVBoxLayout(propPanel);
	v->setContentsMargins(4, 0, 4, 0);

	propName = new QLabel(propPanel);
	propName->setWordWrap(true);
	v->addWidget(propName);

	// --- 位置 ---
	auto *posGroup = new QGroupBox("位置", propPanel);
	auto *posForm = new QFormLayout(posGroup);
	posXSpin = new QDoubleSpinBox(posGroup);
	posXSpin->setRange(-10000, 10000);
	posXSpin->setDecimals(0);
	posYSpin = new QDoubleSpinBox(posGroup);
	posYSpin->setRange(-10000, 10000);
	posYSpin->setDecimals(0);
	posForm->addRow("X", posXSpin);
	posForm->addRow("Y", posYSpin);
	visibleCheck = new QCheckBox("OBSに表示する", posGroup);
	visibleCheck->setToolTip("オフにすると配置したまま一時的に隠せます");
	posForm->addRow(visibleCheck);
	v->addWidget(posGroup);

	connect(posXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double x) {
		if (syncing)
			return;
		QStringList ids = selectedIds();
		if (ids.size() != 1)
			return;
		Elements before = cur();
		cur()[ids.first()].pos.setX(x);
		refreshItem(ids.first());
		commit("X座標の変更", before, "posx:" + ids.first());
	});
	connect(posYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double y) {
		if (syncing)
			return;
		QStringList ids = selectedIds();
		if (ids.size() != 1)
			return;
		Elements before = cur();
		cur()[ids.first()].pos.setY(y);
		refreshItem(ids.first());
		commit("Y座標の変更", before, "posy:" + ids.first());
	});
	connect(visibleCheck, &QCheckBox::toggled, this, [this](bool on) {
		if (!syncing)
			applyToSelection("表示の切り替え", QString(), [on](ElementData &ed) { ed.isVisible = on; });
	});

	// --- 文字 ---
	auto *textGroup = new QGroupBox("文字", propPanel);
	auto *textForm = new QFormLayout(textGroup);
	fontBtn = new QPushButton(textGroup);
	connect(fontBtn, &QPushButton::clicked, this, [this]() {
		QStringList ids = selectedIds();
		if (ids.isEmpty())
			return;
		bool ok = false;
		QFont f = QFontDialog::getFont(&ok, cur()[ids.first()].font, this, "フォントを選択");
		if (!ok)
			return;
		applyToSelection("フォントの変更", QString(), [f](ElementData &ed) { ed.font = f; });
	});
	textForm->addRow("フォント", fontBtn);

	fontSizeSpin = new QSpinBox(textGroup);
	fontSizeSpin->setRange(1, 500);
	fontSizeSpin->setSuffix(" px");
	connect(fontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int px) {
		if (!syncing)
			applyToSelection("文字サイズの変更", "fontsize",
					 [px](ElementData &ed) { ed.font.setPointSize(px); });
	});
	textForm->addRow("サイズ", fontSizeSpin);

	auto *styleRow = new QHBoxLayout();
	boldCheck = new QCheckBox("太字", textGroup);
	italicCheck = new QCheckBox("斜体", textGroup);
	styleRow->addWidget(boldCheck);
	styleRow->addWidget(italicCheck);
	styleRow->addStretch();
	textForm->addRow(styleRow);
	connect(boldCheck, &QCheckBox::toggled, this, [this](bool on) {
		if (!syncing)
			applyToSelection("太字の切り替え", QString(), [on](ElementData &ed) { ed.font.setBold(on); });
	});
	connect(italicCheck, &QCheckBox::toggled, this, [this](bool on) {
		if (!syncing)
			applyToSelection("斜体の切り替え", QString(), [on](ElementData &ed) { ed.font.setItalic(on); });
	});

	colorBtn = new QPushButton(textGroup);
	connect(colorBtn, &QPushButton::clicked, this, [this]() {
		QStringList ids = selectedIds();
		if (ids.isEmpty())
			return;
		QColor c = QColorDialog::getColor(QColor(cur()[ids.first()].color), this, "文字色");
		if (c.isValid())
			applyToSelection("文字色の変更", QString(), [c](ElementData &ed) { ed.color = c.name(); });
	});
	textForm->addRow("色", colorBtn);

	alignBox = new QComboBox(textGroup);
	alignBox->addItem("左揃え", "left");
	alignBox->addItem("中央揃え", "center");
	alignBox->addItem("右揃え", "right");
	alignBox->setToolTip("揃えを変えても画面上の見た目の位置はそのまま保たれます");
	connect(alignBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		if (syncing)
			return;
		QString align = alignBox->itemData(i).toString();
		// 見た目の位置を保ったまま、基準点（X座標）を揃えに合わせて付け替える
		std::map<QString, QRectF> rects;
		for (const QString &id : selectedIds())
			rects[id] = items[id]->sceneTextRect();
		applyToSelection("揃えの変更", QString(), [align, rects, this](ElementData &ed) {
			for (auto &[id, r] : rects) {
				if (&cur()[id] != &ed)
					continue;
				double x = align == "center" ? r.center().x() : (align == "right" ? r.right() : r.left());
				ed.pos.setX(std::round(x));
			}
			ed.align = align;
		});
	});
	textForm->addRow("揃え", alignBox);
	v->addWidget(textGroup);

	// --- 縁取り ---
	auto *outlineGroup = new QGroupBox("縁取り", propPanel);
	auto *outlineForm = new QFormLayout(outlineGroup);
	outlineCheck = new QCheckBox("縁取りを付ける", outlineGroup);
	connect(outlineCheck, &QCheckBox::toggled, this, [this](bool on) {
		if (!syncing)
			applyToSelection("縁取りの切り替え", QString(), [on](ElementData &ed) { ed.outlineEnabled = on; });
	});
	outlineForm->addRow(outlineCheck);
	outlineSizeSpin = new QSpinBox(outlineGroup);
	outlineSizeSpin->setRange(0, 50);
	outlineSizeSpin->setSuffix(" px");
	connect(outlineSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int s) {
		if (!syncing)
			applyToSelection("縁取りの太さ", "outlinesize", [s](ElementData &ed) { ed.outlineSize = s; });
	});
	outlineForm->addRow("太さ", outlineSizeSpin);
	outlineColorBtn = new QPushButton(outlineGroup);
	connect(outlineColorBtn, &QPushButton::clicked, this, [this]() {
		QStringList ids = selectedIds();
		if (ids.isEmpty())
			return;
		QColor c = QColorDialog::getColor(QColor(cur()[ids.first()].outlineColor), this, "縁取りの色");
		if (c.isValid())
			applyToSelection("縁取りの色", QString(), [c](ElementData &ed) { ed.outlineColor = c.name(); });
	});
	outlineForm->addRow("色", outlineColorBtn);
	v->addWidget(outlineGroup);

	// --- 幅 ---
	auto *widthGroup = new QGroupBox("最大幅", propPanel);
	auto *widthForm = new QFormLayout(widthGroup);
	maxWidthSpin = new QSpinBox(widthGroup);
	maxWidthSpin->setRange(0, 3840);
	maxWidthSpin->setSuffix(" px");
	maxWidthSpin->setSpecialValueText("制限なし");
	maxWidthSpin->setSingleStep(10);
	connect(maxWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int w) {
		if (!syncing)
			applyToSelection("最大幅の変更", "maxwidth", [w](ElementData &ed) { ed.maxWidth = w; });
	});
	widthForm->addRow("最大幅", maxWidthSpin);
	wrapBox = new QComboBox(widthGroup);
	wrapBox->addItem("はみ出す（制限なし）", "none");
	wrapBox->addItem("自動縮小", "shrink");
	wrapBox->addItem("自動改行", "wrap");
	connect(wrapBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		if (syncing)
			return;
		QString mode = wrapBox->itemData(i).toString();
		applyToSelection("幅を超えたときの動作", QString(), [mode](ElementData &ed) { ed.wrapMode = mode; });
	});
	widthForm->addRow("超えたとき", wrapBox);
	v->addWidget(widthGroup);

	// --- スタイルのコピー・配置解除 ---
	auto *actionRow = new QGridLayout();
	auto *copyStyleBtn = new QPushButton("スタイルをコピー", propPanel);
	copyStyleBtn->setToolTip("Ctrl+C（キャンバス上）");
	connect(copyStyleBtn, &QPushButton::clicked, this, &OverlayEditor::copyStyle);
	pasteStyleBtn = new QPushButton("スタイルを貼り付け", propPanel);
	pasteStyleBtn->setToolTip("フォント・色・揃え・縁取り・最大幅を貼り付けます（Ctrl+V）");
	pasteStyleBtn->setEnabled(false);
	connect(pasteStyleBtn, &QPushButton::clicked, this, &OverlayEditor::pasteStyle);
	auto *unplaceBtn = new QPushButton("配置を外す", propPanel);
	unplaceBtn->setToolTip("パーツ一覧に戻します（Delete）");
	connect(unplaceBtn, &QPushButton::clicked, this, &OverlayEditor::unplaceSelected);
	actionRow->addWidget(copyStyleBtn, 0, 0);
	actionRow->addWidget(pasteStyleBtn, 0, 1);
	actionRow->addWidget(unplaceBtn, 1, 0, 1, 2);
	v->addLayout(actionRow);

	// --- 整列 ---
	auto *alignGrp = new QGroupBox("整列", propPanel);
	alignGroup = alignGrp;
	auto *grid = new QGridLayout(alignGrp);
	auto addAlignBtn = [this, alignGrp, grid](const QString &text, const QString &tip, int row, int col,
						  const std::function<void()> &fn, bool multiOnly) {
		auto *b = new QPushButton(text, alignGrp);
		b->setToolTip(tip);
		connect(b, &QPushButton::clicked, this, fn);
		grid->addWidget(b, row, col);
		if (multiOnly)
			multiOnlyButtons << b;
	};
	const QString singleNote = "（1つだけ選択中は画面に対して揃えます）";
	addAlignBtn("左", "左端を揃える" + singleNote, 0, 0, [this]() { alignSelection(AlignLeft); }, false);
	addAlignBtn("左右中央", "左右の中央を揃える" + singleNote, 0, 1, [this]() { alignSelection(AlignHCenter); },
		    false);
	addAlignBtn("右", "右端を揃える" + singleNote, 0, 2, [this]() { alignSelection(AlignRight); }, false);
	addAlignBtn("上", "上端を揃える" + singleNote, 1, 0, [this]() { alignSelection(AlignTop); }, false);
	addAlignBtn("上下中央", "上下の中央を揃える" + singleNote, 1, 1, [this]() { alignSelection(AlignVCenter); },
		    false);
	addAlignBtn("下", "下端を揃える" + singleNote, 1, 2, [this]() { alignSelection(AlignBottom); }, false);
	addAlignBtn("横に等間隔", "3つ以上選択したパーツを横方向に等間隔で並べる", 2, 0,
		    [this]() { alignSelection(DistributeH); }, true);
	addAlignBtn("縦に等間隔", "3つ以上選択したパーツを縦方向に等間隔で並べる", 2, 1,
		    [this]() { alignSelection(DistributeV); }, true);
	auto *centerBtn = new QToolButton(alignGrp);
	centerBtn->setText("画面中央へ ▾");
	centerBtn->setPopupMode(QToolButton::InstantPopup);
	centerBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	auto *centerMenu = new QMenu(centerBtn);
	centerMenu->addAction("左右の中央へ", this, [this]() { centerOnCanvas(Qt::Horizontal); });
	centerMenu->addAction("上下の中央へ", this, [this]() { centerOnCanvas(Qt::Vertical); });
	centerBtn->setMenu(centerMenu);
	grid->addWidget(centerBtn, 2, 2);
	v->addWidget(alignGrp);

	v->addStretch();
	scroll->setWidget(propPanel);
	return scroll;
}

// ============================================================
// 表示の同期
// ============================================================

QString OverlayEditor::textFor(const QString &id) const
{
	QString value = values.value(id);
	// タグだけで中身のない値（"<span></span>" など）は空とみなして仮の文字を出す
	if (!richTextToPlain(value).isEmpty())
		return value;
	if (const ElementInfo *info = findElementInfo(id))
		return info->sample;
	return id;
}

void OverlayEditor::showHint(const QString &text)
{
	hintLabel->setText(text);
}

QStringList OverlayEditor::selectedIds() const
{
	QStringList ids;
	// 一覧の並び順で返す（先頭がプロパティ欄に表示される代表）
	for (const auto &info : elementCatalog()) {
		auto it = items.find(info.id);
		if (it != items.end() && it->second->isVisible() && it->second->isSelected())
			ids << info.id;
	}
	return ids;
}

void OverlayEditor::refreshItem(const QString &id)
{
	auto it = items.find(id);
	if (it == items.end())
		return;
	const ElementData &ed = cur()[id];
	ElementItem *item = it->second;
	item->setContent(ed, textFor(id));
	item->setPos(ed.pos);
	if (!ed.isPlaced && item->isSelected())
		item->setSelected(false);
	item->setVisible(ed.isPlaced);
}

void OverlayEditor::refreshItems()
{
	bool wasSyncing = syncing;
	syncing = true;
	for (auto &[id, item] : items)
		refreshItem(id);
	syncing = wasSyncing;
	view->viewport()->update();
}

void OverlayEditor::refreshPalette()
{
	QString filter = searchEdit ? searchEdit->text().trimmed() : QString();
	for (auto &[id, item] : paletteItems) {
		const ElementData &ed = cur()[id];
		item->setIcon(0, stateIcon(ed.isPlaced, ed.isVisible));
		item->setToolTip(0, !ed.isPlaced ? "未配置：選んでキャンバスをクリックすると配置します"
						 : (ed.isVisible ? "配置済み" : "配置済み（OBSでは非表示）"));
		item->setHidden(!filter.isEmpty() && !item->text(0).contains(filter, Qt::CaseInsensitive) &&
				!id.contains(filter, Qt::CaseInsensitive));
	}
	for (int i = 0; i < palette->topLevelItemCount(); ++i) {
		QTreeWidgetItem *group = palette->topLevelItem(i);
		bool anyVisible = false;
		for (int c = 0; c < group->childCount(); ++c)
			anyVisible |= !group->child(c)->isHidden();
		group->setHidden(!anyVisible);
	}
}

void OverlayEditor::syncPaletteSelection()
{
	bool wasSyncing = syncing;
	syncing = true;
	palette->clearSelection();
	QTreeWidgetItem *last = nullptr;
	for (const QString &id : selectedIds()) {
		paletteItems[id]->setSelected(true);
		last = paletteItems[id];
	}
	if (last)
		palette->scrollToItem(last);
	syncing = wasSyncing;
}

void OverlayEditor::syncPropertiesFromSelection()
{
	QStringList ids = selectedIds();
	bool wasSyncing = syncing;
	syncing = true;

	bool any = !ids.isEmpty();
	for (QWidget *w : propPanel->findChildren<QGroupBox *>())
		w->setEnabled(any);
	for (QPushButton *b : propPanel->findChildren<QPushButton *>())
		b->setEnabled(any);
	pasteStyleBtn->setEnabled(any && styleClipboard.has_value());
	for (QPushButton *b : multiOnlyButtons)
		b->setEnabled(ids.size() >= 3);

	if (!any) {
		propName->setText("<b>パーツが選択されていません</b><br>キャンバス上のパーツをクリックするか、"
				  "左の一覧から選んでください。複数選択は Ctrl+クリックか範囲ドラッグです。");
		syncing = wasSyncing;
		return;
	}

	const ElementData &ed = cur()[ids.first()];
	const ElementInfo *info = findElementInfo(ids.first());
	QString name = info ? info->name : ids.first();
	if (ids.size() == 1)
		propName->setText(QString("<b>%1</b>").arg(name.toHtmlEscaped()));
	else
		propName->setText(QString("<b>%1個のパーツを選択中</b><br>変更は選択中のすべてに適用されます（表示は「%2」の値）")
					  .arg(ids.size())
					  .arg(name.toHtmlEscaped()));

	posXSpin->setEnabled(ids.size() == 1);
	posYSpin->setEnabled(ids.size() == 1);
	posXSpin->setValue(ed.pos.x());
	posYSpin->setValue(ed.pos.y());
	visibleCheck->setChecked(ed.isVisible);

	int px = ed.font.pointSize() > 0 ? ed.font.pointSize() : 48;
	fontBtn->setText(ed.font.family());
	fontSizeSpin->setValue(px);
	boldCheck->setChecked(ed.font.bold());
	italicCheck->setChecked(ed.font.italic());
	setColorButton(colorBtn, ed.color);
	int ai = alignBox->findData(ed.align);
	alignBox->setCurrentIndex(ai >= 0 ? ai : 0);
	outlineCheck->setChecked(ed.outlineEnabled);
	outlineSizeSpin->setValue(ed.outlineSize);
	setColorButton(outlineColorBtn, ed.outlineColor);
	maxWidthSpin->setValue(ed.maxWidth);
	int wi = wrapBox->findData(ed.wrapMode);
	wrapBox->setCurrentIndex(wi >= 0 ? wi : 0);

	syncing = wasSyncing;
}

void OverlayEditor::updateBackground()
{
	QPixmap pm;
	auto it = bgPaths.find(currentName);
	if (it != bgPaths.end() && !it->second.isEmpty()) {
		pm.load(it->second);
		if (pm.isNull())
			showHint("背景画像を読み込めませんでした: " + it->second);
	}
	view->setBackgroundImage(pm);
}

void OverlayEditor::switchLayout(const QString &name)
{
	if (!layoutsData.count(name))
		return;
	if (ghost)
		stopPlacing();
	currentName = name;
	if (layoutBox->currentText() != name) {
		layoutBox->blockSignals(true);
		layoutBox->setCurrentText(name);
		layoutBox->blockSignals(false);
	}
	syncing = true;
	scene->clearSelection();
	syncing = false;
	refreshItems();
	refreshPalette();
	updateBackground();
	syncPaletteSelection();
	syncPropertiesFromSelection();
	scheduleLivePreview();
}

void OverlayEditor::restoreLayout(const QString &layout, const Elements &elements)
{
	layoutsData[layout] = elements;
	if (layout != currentName) {
		switchLayout(layout);
		return;
	}
	if (ghost && cur()[ghost->id()].isPlaced)
		stopPlacing();
	refreshItems();
	refreshPalette();
	syncPaletteSelection();
	syncPropertiesFromSelection();
	scheduleLivePreview();
}

void OverlayEditor::scheduleLivePreview()
{
	if (liveCallback && liveCheck && liveCheck->isChecked())
		liveTimer->start();
}

// ============================================================
// 編集操作
// ============================================================

void OverlayEditor::commit(const QString &text, const Elements &before, const QString &mergeKey)
{
	undoStack->push(new SnapshotCommand(this, currentName, before, cur(), text, mergeKey));
	refreshPalette();
	scheduleLivePreview();
}

void OverlayEditor::applyToSelection(const QString &text, const QString &mergeKey,
				     const std::function<void(ElementData &)> &fn)
{
	QStringList ids = selectedIds();
	if (ids.isEmpty())
		return;
	Elements before = cur();
	for (const QString &id : ids) {
		fn(cur()[id]);
		refreshItem(id);
	}
	commit(text, before, mergeKey.isEmpty() ? QString() : mergeKey + ":" + ids.join(","));
	syncPropertiesFromSelection();
	view->viewport()->update();
}

void OverlayEditor::startPlacing(const QString &id)
{
	if (ghost)
		stopPlacing();
	const ElementInfo *info = findElementInfo(id);
	QString name = info ? info->name : id;

	syncing = true;
	scene->clearSelection();
	syncing = false;
	syncPropertiesFromSelection();

	ghost = new ElementItem(id, name);
	ghost->setFlag(QGraphicsItem::ItemIsSelectable, false);
	ghost->setAcceptHoverEvents(false);
	ghost->setAcceptedMouseButtons(Qt::NoButton);
	ghost->setOpacity(0.6);
	ghost->setZValue(1000);
	ghost->setContent(cur()[id], textFor(id));
	ghost->setVisible(false);
	scene->addItem(ghost);
	view->setPlacingItem(ghost);
	view->setFocus();
	showHint(QString("キャンバスをクリックして「%1」を配置します（Esc または右クリックでキャンセル）").arg(name));
}

void OverlayEditor::stopPlacing()
{
	if (!ghost)
		return;
	view->setPlacingItem(nullptr);
	scene->removeItem(ghost);
	delete ghost;
	ghost = nullptr;
	syncing = true;
	palette->clearSelection();
	syncing = false;
	showHint("パーツをドラッグで移動、矢印キーで1px（Shift+矢印で10px）移動できます");
	view->viewport()->update();
}

void OverlayEditor::placeElement(const QString &id, const QPointF &anchor)
{
	if (!cur().count(id))
		return;
	if (ghost)
		stopPlacing();

	Elements before = cur();
	ElementData &ed = cur()[id];
	ed.isPlaced = true;
	ed.isVisible = true;
	ed.pos = rounded(anchor);
	refreshItem(id);

	const ElementInfo *info = findElementInfo(id);
	commit(QString("配置: %1").arg(info ? info->name : id), before);

	syncing = true;
	scene->clearSelection();
	items[id]->setSelected(true);
	syncing = false;
	syncPaletteSelection();
	syncPropertiesFromSelection();
	view->setFocus();
}

void OverlayEditor::unplaceSelected()
{
	QStringList ids = selectedIds();
	if (ids.isEmpty())
		return;
	Elements before = cur();
	for (const QString &id : ids) {
		cur()[id].isPlaced = false;
		refreshItem(id);
	}
	commit("配置を外す", before);
	syncPaletteSelection();
	syncPropertiesFromSelection();
	showHint("一覧に戻しました（Ctrl+Z で元に戻せます）");
}

void OverlayEditor::onItemsMoved(const QMap<QString, QPointF> &)
{
	Elements before = cur();
	for (const QString &id : selectedIds())
		cur()[id].pos = rounded(items[id]->pos());
	refreshItems();
	commit("移動", before);
	syncPropertiesFromSelection();
}

void OverlayEditor::nudge(int dx, int dy)
{
	QStringList ids = selectedIds();
	if (ids.isEmpty())
		return;
	QPointF delta = view->clampDelta(view->selectionRect(), QPointF(dx, dy));
	if (delta.isNull())
		return;
	Elements before = cur();
	for (const QString &id : ids) {
		cur()[id].pos = rounded(cur()[id].pos + delta);
		refreshItem(id);
	}
	commit("キーで移動", before, "nudge:" + ids.join(","));
	syncPropertiesFromSelection();
}

void OverlayEditor::alignSelection(AlignMode mode)
{
	QStringList ids = selectedIds();
	if (ids.isEmpty())
		return;
	if ((mode == DistributeH || mode == DistributeV) && ids.size() < 3)
		return;

	std::map<QString, QRectF> rects;
	QRectF unionRect;
	for (const QString &id : ids) {
		rects[id] = items[id]->sceneTextRect();
		unionRect = unionRect.isNull() ? rects[id] : unionRect.united(rects[id]);
	}
	// 1つだけなら画面を基準に揃える
	QRectF ref = ids.size() == 1 ? QRectF(0, 0, kCanvasWidth, kCanvasHeight) : unionRect;

	std::map<QString, QPointF> deltas;
	if (mode == DistributeH || mode == DistributeV) {
		bool horizontal = mode == DistributeH;
		QStringList sorted = ids;
		std::sort(sorted.begin(), sorted.end(), [&](const QString &a, const QString &b) {
			return horizontal ? rects[a].center().x() < rects[b].center().x()
					  : rects[a].center().y() < rects[b].center().y();
		});
		double total = 0;
		for (const QString &id : sorted)
			total += horizontal ? rects[id].width() : rects[id].height();
		double start = horizontal ? rects[sorted.first()].left() : rects[sorted.first()].top();
		double end = horizontal ? rects[sorted.last()].right() : rects[sorted.last()].bottom();
		double gap = (end - start - total) / (sorted.size() - 1);
		double cursor = start;
		for (const QString &id : sorted) {
			const QRectF &r = rects[id];
			deltas[id] = horizontal ? QPointF(cursor - r.left(), 0) : QPointF(0, cursor - r.top());
			cursor += (horizontal ? r.width() : r.height()) + gap;
		}
	} else {
		for (const QString &id : ids) {
			const QRectF &r = rects[id];
			switch (mode) {
			case AlignLeft:
				deltas[id] = QPointF(ref.left() - r.left(), 0);
				break;
			case AlignHCenter:
				deltas[id] = QPointF(ref.center().x() - r.center().x(), 0);
				break;
			case AlignRight:
				deltas[id] = QPointF(ref.right() - r.right(), 0);
				break;
			case AlignTop:
				deltas[id] = QPointF(0, ref.top() - r.top());
				break;
			case AlignVCenter:
				deltas[id] = QPointF(0, ref.center().y() - r.center().y());
				break;
			case AlignBottom:
				deltas[id] = QPointF(0, ref.bottom() - r.bottom());
				break;
			default:
				break;
			}
		}
	}

	Elements before = cur();
	for (auto &[id, d] : deltas) {
		cur()[id].pos = rounded(cur()[id].pos + d);
		refreshItem(id);
	}
	commit("整列", before);
	syncPropertiesFromSelection();
}

void OverlayEditor::centerOnCanvas(Qt::Orientation orientation)
{
	QStringList ids = selectedIds();
	if (ids.isEmpty())
		return;
	QRectF r = view->selectionRect();
	QPointF delta = orientation == Qt::Horizontal ? QPointF(kCanvasWidth / 2 - r.center().x(), 0)
						      : QPointF(0, kCanvasHeight / 2 - r.center().y());
	Elements before = cur();
	for (const QString &id : ids) {
		cur()[id].pos = rounded(cur()[id].pos + delta);
		refreshItem(id);
	}
	commit("画面中央へ移動", before);
	syncPropertiesFromSelection();
}

void OverlayEditor::applyPreset(int index)
{
	if (index < 0 || index >= (int)presets().size())
		return;
	if (ghost)
		stopPlacing();
	const Preset &preset = presets()[index];
	Elements before = cur();
	for (auto &[id, ed] : cur())
		ed.isPlaced = false;
	for (const PresetEntry &e : preset.entries) {
		if (!cur().count(e.id))
			continue;
		ElementData &ed = cur()[e.id];
		ed.isPlaced = true;
		ed.isVisible = true;
		ed.pos = QPointF(e.x, e.y);
		ed.align = e.align;
	}
	refreshItems();
	commit("プリセット: " + preset.name, before);
	syncPaletteSelection();
	syncPropertiesFromSelection();
	showHint(QString("「%1」で配置しました。ここから自由に調整できます（Ctrl+Z で元に戻せます）").arg(preset.name));
}

void OverlayEditor::copyFromLayout(const QString &source)
{
	if (!layoutsData.count(source) || source == currentName)
		return;
	if (ghost)
		stopPlacing();
	Elements before = cur();
	cur() = layoutsData[source];
	refreshItems();
	commit(QString("「%1」から複製").arg(source), before);
	syncPaletteSelection();
	syncPropertiesFromSelection();
	showHint(QString("レイアウト「%1」の配置とスタイルを複製しました（Ctrl+Z で元に戻せます）").arg(source));
}

void OverlayEditor::unplaceAll()
{
	if (ghost)
		stopPlacing();
	Elements before = cur();
	for (auto &[id, ed] : cur())
		ed.isPlaced = false;
	refreshItems();
	commit("すべて未配置に戻す", before);
	syncPaletteSelection();
	syncPropertiesFromSelection();
	showHint("すべてのパーツを一覧に戻しました（Ctrl+Z で元に戻せます）");
}

void OverlayEditor::copyStyle()
{
	QStringList ids = selectedIds();
	if (ids.isEmpty())
		return;
	styleClipboard = cur()[ids.first()];
	pasteStyleBtn->setEnabled(true);
	const ElementInfo *info = findElementInfo(ids.first());
	showHint(QString("「%1」のスタイルをコピーしました。貼り付け先を選んで Ctrl+V").arg(info ? info->name : ids.first()));
}

void OverlayEditor::pasteStyle()
{
	if (!styleClipboard)
		return;
	ElementData src = *styleClipboard;
	// 揃えを変えると基準点がずれるので、見た目の位置を保つよう X を付け替える
	std::map<QString, QRectF> rects;
	for (const QString &id : selectedIds())
		rects[id] = items[id]->sceneTextRect();
	applyToSelection("スタイルの貼り付け", QString(), [src, rects, this](ElementData &ed) {
		for (auto &[id, r] : rects) {
			if (&cur()[id] != &ed)
				continue;
			if (ed.align != src.align) {
				double x = src.align == "center" ? r.center().x() : (src.align == "right" ? r.right() : r.left());
				ed.pos.setX(std::round(x));
			}
		}
		ed.font = src.font;
		ed.color = src.color;
		ed.align = src.align;
		ed.outlineEnabled = src.outlineEnabled;
		ed.outlineSize = src.outlineSize;
		ed.outlineColor = src.outlineColor;
		ed.maxWidth = src.maxWidth;
		ed.wrapMode = src.wrapMode;
	});
}

void OverlayEditor::showContextMenu(const QPoint &globalPos)
{
	QStringList ids = selectedIds();
	if (ids.isEmpty())
		return;
	QMenu menu(this);
	bool visible = cur()[ids.first()].isVisible;
	menu.addAction(visible ? "OBSで非表示にする" : "OBSに表示する", this, [this, visible]() {
		applyToSelection("表示の切り替え", QString(), [visible](ElementData &ed) { ed.isVisible = !visible; });
	});
	menu.addSeparator();
	menu.addAction("スタイルをコピー", this, &OverlayEditor::copyStyle);
	QAction *paste = menu.addAction("スタイルを貼り付け", this, &OverlayEditor::pasteStyle);
	paste->setEnabled(styleClipboard.has_value());
	menu.addSeparator();
	menu.addAction("左右の中央へ", this, [this]() { centerOnCanvas(Qt::Horizontal); });
	menu.addAction("上下の中央へ", this, [this]() { centerOnCanvas(Qt::Vertical); });
	menu.addSeparator();
	menu.addAction("配置を外す", this, &OverlayEditor::unplaceSelected);
	menu.exec(globalPos);
}

void OverlayEditor::accept()
{
	prefs().windowSize = size();
	QDialog::accept();
}

void OverlayEditor::reject()
{
	if (!undoStack->isClean()) {
		auto answer = QMessageBox::question(this, "変更を破棄", "編集した内容を破棄して閉じますか？",
						    QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
		if (answer != QMessageBox::Discard)
			return;
	}
	prefs().windowSize = size();
	QDialog::reject();
}
