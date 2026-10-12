#pragma once

#include <QList>
#include <QString>

// 表示文字列に書ける簡易タグ（index.html では innerHTML としてそのまま解釈される）を解析する。
// プレビューで本番と同じ見た目を再現するため、次の範囲だけを正式にサポートする。
//   <span style="font-size:70%; color:#ff4444">…</span>  一部だけ大きさ（% / em）・色を変える
//   <b> <strong> / <i> <em>                              太字 / 斜体
//   <br>                                                 改行
//   &lt; &gt; &amp; &quot; &#39; &nbsp; &#NN; &#xNN;     記号
// それ以外のタグは取り除き、中の文字だけを残す。閉じ忘れは末尾で閉じた扱いになる。
// QtCore だけに依存する（描画側で色の文字列を QColor に変換する）。

struct RichTextRun {
	int start = 0;
	int length = 0;
	double scale = 1.0; // 要素の文字サイズに対する倍率（入れ子は掛け合わせる）
	QString color;      // 空なら要素の色のまま
	bool bold = false;  // true なら太字にする（false は要素の設定のまま）
	bool italic = false;
};

struct RichText {
	// タグを除いた文字。<br> は QChar::LineSeparator、元の改行文字は '\n' のまま残す
	QString text;
	// text 全体を隙間なく覆う書式の範囲（同じ書式が続く部分はまとめてある）
	QList<RichTextRun> runs;
};

RichText parseRichText(const QString &markup);

// タグを除いた表示用の文字（改行は空白にする）。プルダウンなど一行で見せる場所用
QString richTextToPlain(const QString &markup);
