#pragma once

class QLineEdit;

// 入力欄の右端に「文字の装飾」ボタンを付ける。
// 選択した文字を <span style="font-size:…%"> などのタグで囲み、一部だけ大きさや色を変えられるようにする。
void installRichTextMenu(QLineEdit *edit);
