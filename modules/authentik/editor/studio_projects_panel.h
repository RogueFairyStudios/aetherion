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
#include "core/os/process_id.h"
#include "scene/gui/box_container.h"
#include "scene/gui/panel_container.h"

class AcceptDialog;
class AuthentikAccountDialog;
class Button;
class CheckBox;
class EditorFileDialog;
class FlowContainer;
class HSplitContainer;
class Label;
class LineEdit;
class OptionButton;
class ScrollContainer;
class TextureRect;
class Timer;
class Tree;
class TreeItem;

class StudioProjectsPanel : public VBoxContainer {
	GDCLASS(StudioProjectsPanel, VBoxContainer);

public:
	enum KindFilterMode {
		KIND_FILTER_DIGITAL_HYBRID = 0,
		KIND_FILTER_ALL_KINDS = 1,
		KIND_FILTER_AETHERION_ONLY = 2,
	};

	enum SortMode {
		SORT_LOCAL_FIRST = 0,
		SORT_NAME_ASC = 1,
		SORT_ASSIGNED_TASKS = 2,
		SORT_PHASE = 3,
	};

private:
	Ref<StudioApiClient> studio_client;
	HashMap<String, String> local_bindings;
	Array loaded_projects;
	Vector<int> filtered_project_indices;
	Vector<PanelContainer *> project_card_nodes;
	int selected_filtered_index = -1;
	int hovered_filtered_index = -1;
	Dictionary loaded_user;
	bool initial_fetch_done = false;
	bool auth_signals_connected = false;
	String transient_status_message;
	bool transient_status_is_error = false;

	// Active background VCS clone state
	ProcessID active_clone_pid = 0;
	String active_clone_project_id;
	String active_clone_target_dir;
	String active_clone_workspace_subpath;
	Timer *clone_poll_timer = nullptr;

	// Top header bar (connection identity + global actions)
	PanelContainer *top_header_panel = nullptr;
	TextureRect *connection_status_icon = nullptr;
	Label *connection_title_label = nullptr;
	Label *connection_subtitle_label = nullptr;
	Button *refresh_button = nullptr;
	Button *import_studio_config_button = nullptr;
	Button *export_studio_config_button = nullptr;
	Button *account_button = nullptr;

	// Secondary filter & sort toolbar
	HBoxContainer *filter_bar_hb = nullptr;
	LineEdit *search_edit = nullptr;
	LineEdit *target_user_edit = nullptr;
	OptionButton *kind_filter_option = nullptr;
	OptionButton *sort_option = nullptr;
	CheckBox *include_archived_check = nullptr;

	// Transient status / feedback banner
	PanelContainer *status_banner = nullptr;
	TextureRect *status_icon = nullptr;
	Label *status_label = nullptr;
	Button *status_action_button = nullptr;
	Button *status_dismiss_button = nullptr;

	// Onboarding / empty-state hero view
	PanelContainer *onboarding_outer_panel = nullptr;
	PanelContainer *onboarding_card = nullptr;
	TextureRect *onboarding_icon = nullptr;
	Label *onboarding_title_label = nullptr;
	Label *onboarding_desc_label = nullptr;
	Label *onboarding_server_pill = nullptr;
	Button *onboarding_login_browser_btn = nullptr;
	Button *onboarding_login_device_btn = nullptr;
	Button *onboarding_import_config_btn = nullptr;
	Button *onboarding_configure_btn = nullptr;
	Button *onboarding_clear_filters_btn = nullptr;
	Button *onboarding_cancel_login_btn = nullptr;
	Button *onboarding_copy_url_btn = nullptr;

	// Inline device code panel inside onboarding card
	PanelContainer *onboarding_device_panel = nullptr;
	Label *onboarding_device_code_label = nullptr;
	Label *onboarding_device_url_label = nullptr;
	Button *onboarding_copy_code_btn = nullptr;
	Button *onboarding_open_verify_btn = nullptr;

	// Main workspace split view
	HSplitContainer *split_container = nullptr;
	Label *projects_header_label = nullptr;
	Label *projects_stats_label = nullptr;
	PanelContainer *projects_list_panel = nullptr;
	ScrollContainer *projects_scroll = nullptr;
	VBoxContainer *projects_cards_vb = nullptr;

	// Right detail inspector
	PanelContainer *right_panel = nullptr;
	Label *empty_detail_label = nullptr;
	VBoxContainer *detail_content_vb = nullptr;

	// Project hero header
	Label *detail_title_label = nullptr;
	Label *detail_slug_label = nullptr;
	Button *open_intranet_button = nullptr;
	Button *export_project_config_button = nullptr;

	// KPI stat cards
	PanelContainer *kpi_engine_card = nullptr;
	Label *kpi_engine_title_lbl = nullptr;
	Label *kpi_engine_value_lbl = nullptr;
	PanelContainer *kpi_phase_card = nullptr;
	Label *kpi_phase_title_lbl = nullptr;
	Label *kpi_phase_value_lbl = nullptr;
	PanelContainer *kpi_role_card = nullptr;
	Label *kpi_role_title_lbl = nullptr;
	Label *kpi_role_value_lbl = nullptr;
	PanelContainer *kpi_tasks_card = nullptr;
	Label *kpi_tasks_title_lbl = nullptr;
	Label *kpi_tasks_value_lbl = nullptr;

	// Credits & Milestone banner
	Label *detail_credits_label = nullptr;
	PanelContainer *milestone_banner = nullptr;
	TextureRect *milestone_icon = nullptr;
	Label *detail_milestone_label = nullptr;

	// Local workspace card
	PanelContainer *local_workspace_card = nullptr;
	TextureRect *local_status_icon = nullptr;
	Label *local_header_label = nullptr;
	PanelContainer *local_status_pill = nullptr;
	Label *local_status_pill_label = nullptr;
	PanelContainer *local_path_box = nullptr;
	Label *detail_local_path_label = nullptr;
	Button *copy_local_path_button = nullptr;
	Button *open_in_editor_button = nullptr;
	Button *run_project_button = nullptr;
	Button *bind_folder_button = nullptr;
	Button *show_in_fm_button = nullptr;
	Button *unbind_folder_button = nullptr;

	// Repositories card
	PanelContainer *vcs_card = nullptr;
	TextureRect *vcs_header_icon = nullptr;
	Label *vcs_header_label = nullptr;
	Label *lore_summary_label = nullptr;
	Label *empty_repos_label = nullptr;
	Tree *repos_tree = nullptr;
	Button *clone_repo_button = nullptr;
	Button *copy_clone_cmd_button = nullptr;
	Button *copy_remote_uri_button = nullptr;

	// Dialogs
	EditorFileDialog *bind_dir_dialog = nullptr;
	EditorFileDialog *clone_dir_dialog = nullptr;
	EditorFileDialog *import_config_dialog = nullptr;
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
	void _apply_theme_styles();
	void _set_transient_status(const String &p_message, bool p_is_error = false);
	void _clear_transient_status();
	void _update_ui_state();

	void _rebuild_project_list();
	void _refresh_card_selection_styles();
	void _update_detail_pane();
	int _get_selected_project_data_index() const;
	Dictionary _get_selected_repo_dict() const;

	void _on_card_gui_input(const Ref<InputEvent> &p_event, int p_filtered_idx);
	void _on_card_mouse_entered(int p_filtered_idx);
	void _on_card_mouse_exited(int p_filtered_idx);
	void _on_card_clicked(int p_filtered_idx);
	void _on_card_activated(int p_filtered_idx);

	void _on_refresh_pressed();
	void _on_open_account_pressed();
	void _on_login_browser_pressed();
	void _on_login_device_pressed();
	void _on_cancel_login_pressed();
	void _on_copy_login_url_pressed();
	void _on_copy_device_code_pressed();
	void _on_open_device_verify_pressed();
	void _on_clear_filters_pressed();

	void _on_search_changed(const String &p_text);
	void _on_target_user_submitted(const String &p_text);
	void _on_kind_filter_changed(int p_index);
	void _on_sort_changed(int p_index);
	void _on_filter_toggled(bool p_pressed);
	void _on_repo_selected();

	void _on_open_in_editor_pressed();
	void _on_run_project_pressed();
	void _on_bind_folder_pressed();
	void _on_bind_dir_selected(const String &p_dir);
	void _on_unbind_folder_pressed();
	void _on_copy_local_path_pressed();
	void _on_show_in_fm_pressed();
	void _on_open_intranet_pressed();
	void _on_import_studio_config_pressed();
	void _on_import_config_file_selected(const String &p_path);
	void _on_export_studio_config_pressed();
	void _on_export_project_config_pressed();
	void _on_export_config_file_selected(const String &p_path);
	void _on_clone_repo_pressed();
	void _on_clone_parent_dir_selected(const String &p_parent_dir);
	void _on_copy_clone_cmd_pressed();
	void _on_copy_remote_uri_pressed();
	void _on_clone_poll_timeout();

	void _on_projects_loaded(const Array &p_projects, const Dictionary &p_user);
	void _on_config_loaded(const String &p_tool, const String &p_project_ref, const String &p_format, const String &p_raw_content, const Dictionary &p_parsed_config);
	void _on_request_failed(int p_http_code, const String &p_message);
	void _on_auth_changed();
	void _on_auth_state_changed(int p_status);
	void _on_auth_url_ready(const String &p_url);
	void _on_auth_device_code(const String &p_user_code, const String &p_verify_uri, const String &p_verify_complete, int p_expires);
	void _on_auth_failed(int p_code, const String &p_message);
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
