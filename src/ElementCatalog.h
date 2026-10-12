#pragma once

#include <QString>
#include <vector>

// オーバーレイに配置できるパーツの一覧（エディタのパーツ一覧・プレビュー用）
struct ElementInfo {
	QString id;     // 内部ID（overlay_data.json / index.html の要素ID）
	QString name;   // 表示名
	QString group;  // パーツ一覧での分類
	QString sample; // 値が空のときにプレビューで表示する仮の文字
};

inline const std::vector<ElementInfo> &elementCatalog()
{
	static const std::vector<ElementInfo> catalog = {
		{"GameTitle", "ゲームタイトル", "ゲーム情報", "Super Mario 64"},
		{"Category", "カテゴリ", "ゲーム情報", "120 Star"},
		{"Hardware", "ハードウェア", "ゲーム情報", "N64"},
		{"EstimateTime", "予想時間", "ゲーム情報", "01:45:00"},

		{"CountUpTimer", "カウントアップタイマー", "タイマー", "01:23:45"},
		{"CountDownTimer", "カウントダウンタイマー", "タイマー", "00:15:00"},
		{"RunnerTimer_1", "走者タイマー1", "タイマー", "01:23:45"},
		{"RunnerTimer_2", "走者タイマー2", "タイマー", "01:24:10"},
		{"RunnerTimer_3", "走者タイマー3", "タイマー", "01:25:30"},
		{"RunnerTimer_4", "走者タイマー4", "タイマー", "01:26:05"},

		{"RunnerName_1", "走者名1", "走者・解説", "走者1"},
		{"RunnerName_2", "走者名2", "走者・解説", "走者2"},
		{"RunnerName_3", "走者名3", "走者・解説", "走者3"},
		{"RunnerName_4", "走者名4", "走者・解説", "走者4"},
		{"Commentator", "解説者", "走者・解説", "解説者"},

		{"NextGameTitle_1", "次ゲーム1: タイトル", "次のゲーム1", "次のゲーム1"},
		{"NextRunnerName_1", "次ゲーム1: 走者", "次のゲーム1", "次の走者1"},
		{"NextCategory_1", "次ゲーム1: カテゴリ", "次のゲーム1", "Any%"},
		{"NextHardware_1", "次ゲーム1: ハード", "次のゲーム1", "SFC"},
		{"NextEstimate_1", "次ゲーム1: 予定時間", "次のゲーム1", "00:30:00"},

		{"NextGameTitle_2", "次ゲーム2: タイトル", "次のゲーム2", "次のゲーム2"},
		{"NextRunnerName_2", "次ゲーム2: 走者", "次のゲーム2", "次の走者2"},
		{"NextCategory_2", "次ゲーム2: カテゴリ", "次のゲーム2", "Any%"},
		{"NextHardware_2", "次ゲーム2: ハード", "次のゲーム2", "PS"},
		{"NextEstimate_2", "次ゲーム2: 予定時間", "次のゲーム2", "00:45:00"},

		{"NextGameTitle_3", "次ゲーム3: タイトル", "次のゲーム3", "次のゲーム3"},
		{"NextRunnerName_3", "次ゲーム3: 走者", "次のゲーム3", "次の走者3"},
		{"NextCategory_3", "次ゲーム3: カテゴリ", "次のゲーム3", "100%"},
		{"NextHardware_3", "次ゲーム3: ハード", "次のゲーム3", "GBA"},
		{"NextEstimate_3", "次ゲーム3: 予定時間", "次のゲーム3", "01:00:00"},
	};
	return catalog;
}

inline const ElementInfo *findElementInfo(const QString &id)
{
	for (const auto &info : elementCatalog()) {
		if (info.id == id)
			return &info;
	}
	return nullptr;
}
