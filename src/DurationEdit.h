#pragma once

#include <QAbstractSpinBox>
#include <QString>

// 24時間を超える時間を扱える「時:分:秒」の入力欄。
// QTimeEdit は 23:59:59 までしか扱えないため、カウントダウンタイマー用に用意している。
// 値は秒数で持ち、表示は「HH:mm:ss」(時は2桁以上、最大 999:59:59)。
class DurationEdit : public QAbstractSpinBox {
public:
	static constexpr int kMaxSeconds = 999 * 3600 + 59 * 60 + 59;

	explicit DurationEdit(QWidget *parent = nullptr);

	int seconds() const { return value; }
	void setSeconds(int secs);

	// 秒数 <-> "HH:mm:ss" の変換。parse は不正な文字列なら -1 を返す
	static QString format(int secs);
	static int parse(const QString &text);

	void stepBy(int steps) override;
	QValidator::State validate(QString &input, int &pos) const override;
	void fixup(QString &input) const override;

protected:
	StepEnabled stepEnabled() const override;
	void focusOutEvent(QFocusEvent *event) override;

private:
	void commitText();

	int value = 0;
};
