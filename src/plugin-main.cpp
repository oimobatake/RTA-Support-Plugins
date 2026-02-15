/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

//#pragma warning(disable : 4996) // これを無効にすることで、非推奨の関数の警告を抑制

#include <obs-module.h>
#include <plugin-support.h>
#include <obs-frontend-api.h>
#include "RTAPluginDock.h"

extern "C" {

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// グローバルなドックのポインタ
static RTAPluginDock *g_RTAPluginDock = nullptr;

// OBSのUIイベントを監視する関数
static void obs_frontend_event_callback(enum obs_frontend_event event, void *private_data)
{
	// OBSのUIの準備が整った、という合図が来たら
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		// ドックがまだ作られていなければ
		if (!g_RTAPluginDock) {
			// 私たちのUIクラスのインスタンスを作成し、
			g_RTAPluginDock = new RTAPluginDock();
			// それをOBSに「ドック」として追加する
			obs_frontend_add_dock_by_id(
							"rta_support_plugin_dock",  // 1. 固有のID
						    "RTA Support Plugin",       // 2. メニューに表示される名前
						    g_RTAPluginDock				// 3. UIウィジェットのポインタ
			);          
		}
	}
}

bool obs_module_load(void)
{
	obs_frontend_add_event_callback(obs_frontend_event_callback, nullptr);
	return true;
}

void obs_module_unload(void)
{
	// プラグインがアンロードされるときの処理
}

} // extern "C"