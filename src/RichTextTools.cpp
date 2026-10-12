#include "RichTextTools.h"
#include "RichTextMarkup.h"

#include <QAction>
#include <QColorDialog>
#include <QCursor>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QToolTip>

namespace {

QIcon decorationIcon()
{
	QPixmap pm(32, 32);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing, true);
	QFont f;
	f.setPixelSize(24);
	f.setBold(true);
	p.setFont(f);
	p.setPen(QColor(220, 220, 220));
	p.drawText(QRect(0, 0, 32, 26), Qt::AlignCenter, "A");
	p.fillRect(QRect(6, 26, 20, 4), QColor(255, 80, 80));
	return QIcon(pm);
}

// 選択中の文字を open / close のタグで囲む（元に戻す操作が効くように insert を使う）
void wrapSelection(QLineEdit *edit, int start, const QString &selected, const QString &open, const QString &close)
{
	edit->setSelection(start, selected.size());
	edit->insert(open + selected + close);
	edit->setSelection(start, open.size() + selected.size() + close.size());
}

// タグをすべて取り除いた文字に戻す（改行と記号はそのまま表示されるように残す）
QString stripDecorations(const QString &markup)
{
	QString text = parseRichText(markup).text;
	QString out;
	for (QChar c : text) {
		if (c == QChar::LineSeparator)
			out += "<br>";
		else if (c == '&')
			out += "&amp;";
		else if (c == '<')
			out += "&lt;";
		else if (c == '>')
			out += "&gt;";
		else
			out += c;
	}
	return out;
}

void showMenu(QLineEdit *edit)
{
	// メニューやダイアログを開くと選択が外れることがあるので、先に覚えておく
	const int start = edit->selectionStart();
	const QString selected = edit->selectedText();
	const bool hasSelection = start >= 0 && !selected.isEmpty();

	QMenu menu(edit);

	QMenu *sizeMenu = menu.addMenu("選択した文字の大きさ");
	for (int percent : {50, 70, 85, 120, 150}) {
		sizeMenu->addAction(QString("%1%").arg(percent), edit, [=]() {
			wrapSelection(edit, start, selected, QString("<span style=\"font-size:%1%\">").arg(percent),
				      "</span>");
		});
	}
	sizeMenu->setEnabled(hasSelection);

	QAction *colorAction = menu.addAction("選択した文字の色...", edit, [=]() {
		QColor color = QColorDialog::getColor(Qt::white, edit, "文字の色を選択");
		if (!color.isValid())
			return;
		wrapSelection(edit, start, selected, QString("<span style=\"color:%1\">").arg(color.name()), "</span>");
	});
	colorAction->setEnabled(hasSelection);

	QAction *boldAction = menu.addAction("選択した文字を太字", edit,
					     [=]() { wrapSelection(edit, start, selected, "<b>", "</b>"); });
	boldAction->setEnabled(hasSelection);

	menu.addSeparator();
	menu.addAction("装飾をすべて解除", edit, [edit]() {
		edit->selectAll();
		edit->insert(stripDecorations(edit->text()));
	});

	if (!hasSelection)
		QToolTip::showText(QCursor::pos(), "装飾したい文字を選択してからメニューを開いてください", edit);
	menu.exec(QCursor::pos());
}

} // namespace

void installRichTextMenu(QLineEdit *edit)
{
	QAction *action = edit->addAction(decorationIcon(), QLineEdit::TrailingPosition);
	action->setToolTip("選択した文字の大きさ・色を変える");
	QObject::connect(action, &QAction::triggered, edit, [edit]() { showMenu(edit); });
}
