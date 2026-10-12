#include "RichTextMarkup.h"

#include <QRegularExpression>

namespace {

struct Style {
	double scale = 1.0;
	QString color;
	bool bold = false;
	bool italic = false;

	bool operator==(const Style &o) const
	{
		return scale == o.scale && color == o.color && bold == o.bold && italic == o.italic;
	}
};

struct OpenTag {
	QString name;
	Style style; // このタグの中で使う書式
};

// style 属性の値（"font-size:70%; color:#f00"）を書式に反映する
void applyStyleAttribute(const QString &css, Style &style)
{
	const QStringList decls = css.split(';', Qt::SkipEmptyParts);
	for (const QString &decl : decls) {
		int colon = decl.indexOf(':');
		if (colon < 0)
			continue;
		QString key = decl.left(colon).trimmed().toLower();
		QString value = decl.mid(colon + 1).trimmed();

		if (key == "font-size") {
			QString v = value.toLower();
			double factor = 0;
			bool ok = false;
			if (v.endsWith('%'))
				factor = v.chopped(1).trimmed().toDouble(&ok) / 100.0;
			else if (v.endsWith("em"))
				factor = v.chopped(2).trimmed().toDouble(&ok);
			// px など要素の文字サイズに連動しない指定は、自動縮小と両立しないので扱わない
			if (ok && factor > 0)
				style.scale *= factor;
		} else if (key == "color") {
			if (!value.isEmpty())
				style.color = value;
		} else if (key == "font-weight") {
			QString v = value.toLower();
			bool ok = false;
			int w = v.toInt(&ok);
			style.bold = v == "bold" || v == "bolder" || (ok && w >= 600);
		} else if (key == "font-style") {
			QString v = value.toLower();
			style.italic = v == "italic" || v == "oblique";
		}
	}
}

// タグの中身（"<" と ">" の間）から属性 style の値を取り出す
QString styleAttribute(const QString &tagBody)
{
	static const QRegularExpression re(R"re((?:^|\s)style\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))re",
					   QRegularExpression::CaseInsensitiveOption);
	QRegularExpressionMatch m = re.match(tagBody);
	if (!m.hasMatch())
		return QString();
	for (int i = 1; i <= 3; ++i) {
		if (m.capturedStart(i) >= 0)
			return m.captured(i);
	}
	return QString();
}

// "&amp;" などの文字参照を1つ解釈する。解釈できたら consumed に読んだ長さを入れる
bool decodeEntity(const QString &s, int pos, QString &out, int &consumed)
{
	int semi = s.indexOf(';', pos);
	if (semi < 0 || semi - pos > 10)
		return false;
	QString name = s.mid(pos + 1, semi - pos - 1);
	consumed = semi - pos + 1;

	if (name.startsWith('#')) {
		bool ok = false;
		uint code = name.startsWith("#x", Qt::CaseInsensitive) ? name.mid(2).toUInt(&ok, 16)
								       : name.mid(1).toUInt(&ok, 10);
		if (!ok || code == 0 || code > 0x10FFFF)
			return false;
		char32_t c = code;
		out = QString::fromUcs4(&c, 1);
		return true;
	}

	static const QList<QPair<QString, QString>> named = {
		{"lt", "<"}, {"gt", ">"}, {"amp", "&"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", QString(QChar(0x00A0))},
	};
	for (const auto &e : named) {
		if (name == e.first) {
			out = e.second;
			return true;
		}
	}
	return false;
}

} // namespace

RichText parseRichText(const QString &markup)
{
	RichText result;
	QList<OpenTag> stack;
	Style current;

	auto append = [&](const QString &str) {
		if (str.isEmpty())
			return;
		if (!result.runs.isEmpty()) {
			RichTextRun &last = result.runs.last();
			Style lastStyle{last.scale, last.color, last.bold, last.italic};
			if (lastStyle == current && last.start + last.length == result.text.size()) {
				last.length += str.size();
				result.text += str;
				return;
			}
		}
		RichTextRun run;
		run.start = result.text.size();
		run.length = str.size();
		run.scale = current.scale;
		run.color = current.color;
		run.bold = current.bold;
		run.italic = current.italic;
		result.runs << run;
		result.text += str;
	};

	const int n = markup.size();
	int i = 0;
	while (i < n) {
		QChar c = markup[i];

		if (c == '<') {
			// コメント
			if (markup.mid(i, 4) == "<!--") {
				int end = markup.indexOf("-->", i + 4);
				i = end < 0 ? n : end + 3;
				continue;
			}
			// タグとして扱うのは "<英字" と "</英字" だけ（それ以外の "<" は文字のまま）
			bool closing = i + 1 < n && markup[i + 1] == '/';
			int nameStart = i + (closing ? 2 : 1);
			int end = markup.indexOf('>', i);
			if (nameStart < n && markup[nameStart].isLetter() && end > 0) {
				QString body = markup.mid(nameStart, end - nameStart);
				int nameEnd = 0;
				while (nameEnd < body.size() &&
				       (body[nameEnd].isLetterOrNumber() || body[nameEnd] == '-'))
					++nameEnd;
				QString name = body.left(nameEnd).toLower();
				QString attrs = body.mid(nameEnd);
				bool selfClosing = attrs.trimmed().endsWith('/');
				i = end + 1;

				if (name == "br") {
					if (!closing)
						append(QString(QChar::LineSeparator));
					continue;
				}

				if (closing) {
					// 対応する開きタグまで戻す（見つからなければ無視）
					for (int k = stack.size() - 1; k >= 0; --k) {
						if (stack[k].name == name) {
							stack.erase(stack.begin() + k, stack.end());
							current = stack.isEmpty() ? Style() : stack.last().style;
							break;
						}
					}
					continue;
				}

				if (selfClosing)
					continue;

				Style next = current;
				if (name == "b" || name == "strong")
					next.bold = true;
				else if (name == "i" || name == "em")
					next.italic = true;
				if (name == "span" || name == "b" || name == "strong" || name == "i" || name == "em")
					applyStyleAttribute(styleAttribute(attrs), next);

				// 空要素は閉じタグが来ないので書式の入れ子に積まない
				static const QStringList voidTags = {"img",  "hr",  "input", "meta",
								     "link", "wbr", "col",   "area"};
				if (voidTags.contains(name))
					continue;

				stack << OpenTag{name, next};
				current = next;
				continue;
			}
		}

		if (c == '&') {
			QString decoded;
			int consumed = 0;
			if (decodeEntity(markup, i, decoded, consumed)) {
				append(decoded);
				i += consumed;
				continue;
			}
		}

		// 次の特別な文字までまとめて追加する
		int j = i + 1;
		while (j < n && markup[j] != '<' && markup[j] != '&')
			++j;
		append(markup.mid(i, j - i));
		i = j;
	}

	return result;
}

QString richTextToPlain(const QString &markup)
{
	QString text = parseRichText(markup).text;
	text.replace(QChar::LineSeparator, ' ');
	text.replace('\n', ' ');
	text.replace(QChar(0x00A0), ' ');
	return text.simplified();
}
