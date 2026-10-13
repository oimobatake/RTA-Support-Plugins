#include "DurationEdit.h"

#include <QLineEdit>
#include <QRegularExpression>
#include <algorithm>

DurationEdit::DurationEdit(QWidget *parent) : QAbstractSpinBox(parent)
{
	setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	setCorrectionMode(QAbstractSpinBox::CorrectToPreviousValue);
	lineEdit()->setText(format(value));
	connect(this, &QAbstractSpinBox::editingFinished, this, [this]() { commitText(); });
}

void DurationEdit::setSeconds(int secs)
{
	value = std::clamp(secs, 0, kMaxSeconds);
	lineEdit()->setText(format(value));
}

QString DurationEdit::format(int secs)
{
	secs = std::clamp(secs, 0, kMaxSeconds);
	return QString("%1:%2:%3")
		.arg(secs / 3600, 2, 10, QChar('0'))
		.arg((secs / 60) % 60, 2, 10, QChar('0'))
		.arg(secs % 60, 2, 10, QChar('0'));
}

int DurationEdit::parse(const QString &text)
{
	static const QRegularExpression re("^\\s*(\\d{1,3}):([0-5]\\d):([0-5]\\d)\\s*$");
	auto m = re.match(text);
	if (!m.hasMatch())
		return -1;
	return m.captured(1).toInt() * 3600 + m.captured(2).toInt() * 60 + m.captured(3).toInt();
}

QValidator::State DurationEdit::validate(QString &input, int &) const
{
	if (parse(input) >= 0)
		return QValidator::Acceptable;
	// 入力途中 (桁が足りない・空欄など) は許可する
	static const QRegularExpression partial("^\\d{0,3}(:([0-5]\\d?)?(:([0-5]\\d?)?)?)?$");
	return partial.match(input).hasMatch() ? QValidator::Intermediate : QValidator::Invalid;
}

void DurationEdit::fixup(QString &input) const
{
	input = format(value);
}

void DurationEdit::commitText()
{
	int secs = parse(lineEdit()->text());
	if (secs >= 0)
		value = secs;
	lineEdit()->setText(format(value));
}

void DurationEdit::focusOutEvent(QFocusEvent *event)
{
	commitText();
	QAbstractSpinBox::focusOutEvent(event);
}

void DurationEdit::stepBy(int steps)
{
	commitText();
	// カーソル位置の区切り (時・分・秒) を増減する
	const QString text = lineEdit()->text();
	int pos = lineEdit()->cursorPosition();
	int firstColon = text.indexOf(':');
	int secondColon = text.indexOf(':', firstColon + 1);
	int unit = 1;
	if (pos <= firstColon)
		unit = 3600;
	else if (pos <= secondColon)
		unit = 60;

	value = std::clamp(value + steps * unit, 0, kMaxSeconds);
	QString newText = format(value);
	lineEdit()->setText(newText);

	// 増減した区切りを選択状態に戻す
	int c1 = newText.indexOf(':');
	int c2 = newText.indexOf(':', c1 + 1);
	if (unit == 3600)
		lineEdit()->setSelection(0, c1);
	else if (unit == 60)
		lineEdit()->setSelection(c1 + 1, c2 - c1 - 1);
	else
		lineEdit()->setSelection(c2 + 1, newText.size() - c2 - 1);
}

QAbstractSpinBox::StepEnabled DurationEdit::stepEnabled() const
{
	if (isReadOnly())
		return StepNone;
	StepEnabled e = StepNone;
	if (value > 0)
		e |= StepDownEnabled;
	if (value < kMaxSeconds)
		e |= StepUpEnabled;
	return e;
}
