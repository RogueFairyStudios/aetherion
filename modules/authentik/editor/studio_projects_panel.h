/**************************************************************************/
/*  studio_projects_panel.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "../authentik.h"
#include "../studio_api_client.h"

#include "core/os/os.h"
#include "scene/gui/box_container.h"

class AcceptDialog;
class AuthentikAccountDialog;
class Button;
class CheckBox;
class EditorFileDialog;
class HSplitContainer;
class ItemList;
class Label;
class LineEdit;
class PanelContainer;
class Timer;
class Tree;
class TreeItem;

class StudioProjectsPanel : public VBoxContainer {
	GDCLASS(StudioProjectsPanel, VBoxContainer);

private:
	Ref<StudioApiClient> studio_client;
	HashMap<String, String> local_bindings;
	Array loaded_projects;
	Vector<int> filtered_project_indices;
	Dictionary loaded_user;
	bool initial_fetch_done = false;
	bool auth_signals_connected = false;

	// Active background VCS clone state
	OS::ProcessID active_clone_pid = 0;
	String active_clone_project_id;
	String active_clone_target_dir;
	String active_clone_workspace_subpath;
	Timer *clone_poll_timer = nullptr;

	// Top bar
	Button *refresh_button = nullptr;
	Button *account_button = nullptr;
	Button *export_studio_config_button = nullptr;
	LineEdit *search_edit = nullptr;
	LineEdit *target_user_edit = nullptr;
	CheckBox *include_all_kinds_check = nullptr;
	CheckBox *include_archived_check = nullptr;

	// Status banner
	PanelContainer *status_banner = nullptr;
	Label *status_label = nullptr;
	Button *status_action_button = nullptr;

	// Split content
	HSplitContainer *split_container = nullptr;
	Label *projects_header_label = nullptr;
	ItemList *projects_list = nullptr;

	// Right detail pane
	VBoxContainer *detail_content_vb = nullptr;
	Label *empty_detail_label = nullptr;
	Label *detail_title_label = nullptr;
	Label *detail_meta_label = nullptr;
	Label *detail_milestone_label = nullptr;
	Label *detail_local_path_label = nullptr;
	Button *open_in_editor_button = nullptr;
	Button *bind_folder_button = nullptr;
	Button *show_in_fm_button = nullptr;
	Button *open_intranet_button = nullptr;
	Button *export_project_config_button = nullptr;

	Label *lore_summary_label = nullptr;
	Tree *repos_tree = nullptr;
	Button *clone_repo_button = nullptr;
	Button *copy_clone_cmd_button = nullptr;

	// Dialogs
	EditorFileDialog *bind_dir_dialog = nullptr;
	EditorFileDialog *clone_dir_dialog = nullptr;
	EditorFileDialog *export_config_dialog = nullptr;
	AcceptDialog *info_dialog = nullptr;
	AuthentikAccountDialog *fallback_account_dialog = nullptr;

	String pending_bind_project_id;
	String pending_bind_workspace_subpath;
	Dictionary pending_clone_repo;
	String pending_clone_project_id;
	String pending_export_project_ref;
	String pending_export_config_path;

	String _get_bindings_file_path() const;
	void _load_local_bindings();
	void _save_local_bindings() const;
	String _resolve_godot_project_dir(const String &p_selected_dir, const String &p_workspace_subpath) const;
	String _get_bound_local_dir(const String &p_project_id) const;

	void _connect_auth_signals();
	void _disconnect_auth_signals();
	void _update_theme_icons();
	void _update_status_state(const String &p_custom_message = String(), bool p_is_error = false);

	void _rebuild_project_list();
	void _update_detail_pane();
	int _get_selected_project_data_index() const;
	Dictionary _get_selected_repo_dict() const;

	void _on_refresh_pressed();
	void _on_open_account_pressed();
	void _on_search_changed(const String &p_text);
	void _on_target_user_submitted(const String &p_text);
	void _on_filter_toggled(bool p_pressed);
	void _on_project_selected(int p_list_idx);
	void _on_project_activated(int p_list_idx);
	void _on_repo_selected();

	void _on_open_in_editor_pressed();
	void _on_bind_folder_pressed();
	void _on_bind_dir_selected(const String &p_dir);
	void _on_show_in_fm_pressed();
	void _on_open_intranet_pressed();
	void _on_export_studio_config_pressed();
	void _on_export_project_config_pressed();
	void _on_export_config_file_selected(const String &p_path);
	void _on_clone_repo_pressed();
	void _on_clone_parent_dir_selected(const String &p_parent_dir);
	void _on_copy_clone_cmd_pressed();
	void _on_clone_poll_timeout();

	void _on_projects_loaded(const Array &p_projects, const Dictionary &p_user);
	void _on_config_loaded(const String &p_tool, const String &p_project_ref, const String &p_format, const String &p_raw_content, const Dictionary &p_parsed_config);
	void _on_request_failed(int p_http_code, const String &p_message);
	void _on_auth_changed();
	void _on_auth_session_changed(const Ref<AuthentikSession> &p_session);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void refresh_projects();

	StudioProjectsPanel();
	~StudioProjectsPanel() override;
};

#endif // TOOLS_ENABLED
