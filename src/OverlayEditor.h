#pragma once

#include <QDialog>
#include <QMap>
#include <QPointF>
#include <functional>
#include <map>
#include <optional>
#include "RTAPluginDock.h"

class QGraphicsScene;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QToolButton;
class QUndoStack;
class QTimer;
class QWidget;
class CanvasView;
class ElementItem;

// --- ビジュアルエディタ本体 ---
// パーツ一覧（左）で選んだパーツをキャンバス（中央）に配置し、プロパティ（右）で見た目を編集する。
// 開いた時点の全レイアウトのコピーを編集し、適用時に呼び出し側が layouts() で受け取る。
class OverlayEditor : public QDialog {
	Q_OBJECT
public:
	using Elements = std::map<QString, ElementData>;
	using Layouts = std::map<QString, Elements>;

	OverlayEditor(const Layouts &layouts, const QString &currentLayout,
		      const std::map<QString, QString> &backgrounds, const QMap<QString, QString> &values,
		      QWidget *parent = nullptr);
	~OverlayEditor() override;

	const Layouts &layouts() const { return layoutsData; }
	const std::map<QString, QString> &backgrounds() const { return bgPaths; }
	QString currentLayout() const { return currentName; }

	// 編集中の内容を OBS に反映するためのコールバック（「リアルタイム反映」用）
	void setLivePreviewCallback(std::function<void(const Layouts &)> callback) { liveCallback = callback; }

	// 元に戻す／やり直しから呼ばれる
	void restoreLayout(const QString &layout, const Elements &elements);

public slots:
	void accept() override;
	void reject() override;

private:
	enum AlignMode { AlignLeft, AlignHCenter, AlignRight, AlignTop, AlignVCenter, AlignBottom, DistributeH, DistributeV };

	Elements &cur() { return layoutsData[currentName]; }

	// UI 構築
	QWidget *buildToolbar();
	QWidget *buildPalette();
	QWidget *buildProperties();

	// 表示の同期
	void switchLayout(const QString &name);
	void refreshItems();
	void refreshItem(const QString &id);
	void refreshPalette();
	void syncPropertiesFromSelection();
	void syncPaletteSelection();
	void updateBackground();
	QStringList selectedIds() const;
	QString textFor(const QString &id) const;
	void showHint(const QString &text);

	// 編集操作
	void commit(const QString &text, const Elements &before, const QString &mergeKey = QString());
	void applyToSelection(const QString &text, const QString &mergeKey, const std::function<void(ElementData &)> &fn);
	void startPlacing(const QString &id);
	void stopPlacing();
	void placeElement(const QString &id, const QPointF &anchor);
	void unplaceSelected();
	void onItemsMoved(const QMap<QString, QPointF> &before);
	void nudge(int dx, int dy);
	void alignSelection(AlignMode mode);
	void centerOnCanvas(Qt::Orientation orientation);
	void applyPreset(int index);
	void copyFromLayout(const QString &source);
	void unplaceAll();
	void copyStyle();
	void pasteStyle();
	void showContextMenu(const QPoint &globalPos);
	void scheduleLivePreview();

	// データ
	Layouts layoutsData;
	Layouts originalLayouts;
	std::map<QString, QString> bgPaths;
	QMap<QString, QString> values;
	QString currentName;
	std::optional<ElementData> styleClipboard;
	std::function<void(const Layouts &)> liveCallback;
	bool syncing = false;

	// キャンバス
	QGraphicsScene *scene = nullptr;
	CanvasView *view = nullptr;
	std::map<QString, ElementItem *> items;
	ElementItem *ghost = nullptr;

	// ツールバー
	QComboBox *layoutBox = nullptr;
	QComboBox *zoomBox = nullptr;
	QCheckBox *gridCheck = nullptr;
	QCheckBox *snapCheck = nullptr;
	QComboBox *gridSizeBox = nullptr;
	QCheckBox *safeAreaCheck = nullptr;
	QCheckBox *liveCheck = nullptr;
	QToolButton *copyLayoutBtn = nullptr;

	// パーツ一覧
	QLineEdit *searchEdit = nullptr;
	QTreeWidget *palette = nullptr;
	std::map<QString, QTreeWidgetItem *> paletteItems;

	// プロパティ
	QWidget *propPanel = nullptr;
	QLabel *propName = nullptr;
	QDoubleSpinBox *posXSpin = nullptr;
	QDoubleSpinBox *posYSpin = nullptr;
	QCheckBox *visibleCheck = nullptr;
	QPushButton *fontBtn = nullptr;
	QSpinBox *fontSizeSpin = nullptr;
	QCheckBox *boldCheck = nullptr;
	QCheckBox *italicCheck = nullptr;
	QPushButton *colorBtn = nullptr;
	QComboBox *alignBox = nullptr;
	QCheckBox *outlineCheck = nullptr;
	QSpinBox *outlineSizeSpin = nullptr;
	QPushButton *outlineColorBtn = nullptr;
	QSpinBox *maxWidthSpin = nullptr;
	QComboBox *wrapBox = nullptr;
	QPushButton *pasteStyleBtn = nullptr;
	QWidget *alignGroup = nullptr;
	QList<QPushButton *> multiOnlyButtons;

	// 下部
	QLabel *hintLabel = nullptr;
	QLabel *coordLabel = nullptr;
	QLabel *zoomLabel = nullptr;

	QUndoStack *undoStack = nullptr;
	QTimer *liveTimer = nullptr;
};
