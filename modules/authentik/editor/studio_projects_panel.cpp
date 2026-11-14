/**************************************************************************/
/*  studio_projects_panel.cpp                                             */
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

#include "studio_projects_panel.h"

#ifdef TOOLS_ENABLED

#include "authentik_editor_plugin.h"

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/string/translation_server.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tree.h"
#include "scene/main/timer.h"
#include "servers/display/display_server.h"

static String _format_phase_label(const String &p_phase) {
	if (p_phase == "pre_production") {
		return "Pre-Production";
	}
	if (p_phase == "post_production") {
		return "Post-Production";
	}
	if (p_phase.is_empty()) {
		return "-";
	}
	return p_phase.capitalize();
}

static String _array_to_csv(const Variant &p_var) {
	if (p_var.get_type() == Variant::ARRAY) {
		Array arr = p_var;
		String out;
		for (int i = 0; i < arr.size(); i++) {
			if (i > 0) {
				out += ", ";
			}
			out += String(arr[i]);
		}
		return out;
	}
	return String();
}

StudioProjectsPanel::StudioProjectsPanel() {
	set_name("StudioProjectsTab");
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_theme_constant_override("separation", int(8 * EDSCALE));

	studio_client.instantiate();
	studio_client->set_client_slug("aetherion");
	studio_client->connect("projects_loaded", callable_mp(this, &StudioProjectsPanel::_on_projects_loaded));
	studio_client->connect("config_loaded", callable_mp(this, &StudioProjectsPanel::_on_config_loaded));
	studio_client->connect("request_failed", callable_mp(this, &StudioProjectsPanel::_on_request_failed));

	// Top toolbar
	HBoxContainer *top_hb = memnew(HBoxContainer);
	top_hb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	add_child(top_hb);

	refresh_button = memnew(Button(TTR("Refresh")));
	refresh_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_refresh_pressed));
	top_hb->add_child(refresh_button);

	account_button = memnew(Button(TTR("Authentik & Studio Settings...")));
	account_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_account_pressed));
	top_hb->add_child(account_button);

	export_studio_config_button = memnew(Button(TTR("Export Studio Config...")));
	export_studio_config_button->set_tooltip_text(TTR("Export generalist studio-wide Aetherion configuration manifest (.yaml / .json)."));
	export_studio_config_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_export_studio_config_pressed));
	top_hb->add_child(export_studio_config_button);

	search_edit = memnew(LineEdit);
	search_edit->set_placeholder(TTR("Filter Studio Projects"));
	search_edit->set_clear_button_enabled(true);
	search_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	search_edit->connect(SceneStringName(text_changed), callable_mp(this, &StudioProjectsPanel::_on_search_changed));
	top_hb->add_child(search_edit);

	Label *user_lbl = memnew(Label(TTR("User:")));
	top_hb->add_child(user_lbl);

	target_user_edit = memnew(LineEdit);
	target_user_edit->set_placeholder(TTR("me (or username / email)"));
	target_user_edit->set_clear_button_enabled(true);
	target_user_edit->set_custom_minimum_size(Size2(180 * EDSCALE, 0));
	target_user_edit->set_tooltip_text(TTR("Leave empty or 'me' to list your Studio projects, or enter a colleague's username/email and press Enter."));
	target_user_edit->connect(SceneStringName(text_submitted), callable_mp(this, &StudioProjectsPanel::_on_target_user_submitted));
	top_hb->add_child(target_user_edit);

	include_all_kinds_check = memnew(CheckBox);
	include_all_kinds_check->set_text(TTR("Include Analogue"));
	include_all_kinds_check->set_tooltip_text(TTR("Also list tabletop / analogue Studio projects alongside digital and hybrid Aetherion projects."));
	include_all_kinds_check->connect(SceneStringName(toggled), callable_mp(this, &StudioProjectsPanel::_on_filter_toggled));
	top_hb->add_child(include_all_kinds_check);

	include_archived_check = memnew(CheckBox);
	include_archived_check->set_text(TTR("Archived"));
	include_archived_check->connect(SceneStringName(toggled), callable_mp(this, &StudioProjectsPanel::_on_filter_toggled));
	top_hb->add_child(include_archived_check);

	// Status / onboarding banner
	status_banner = memnew(PanelContainer);
	status_banner->set_visible(false);
	add_child(status_banner);

	HBoxContainer *status_hb = memnew(HBoxContainer);
	status_hb->add_theme_constant_override("separation", int(10 * EDSCALE));
	status_banner->add_child(status_hb);

	status_label = memnew(Label);
	status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_hb->add_child(status_label);

	status_action_button = memnew(Button(TTR("Configure / Sign In...")));
	status_action_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_account_pressed));
	status_hb->add_child(status_action_button);

	// Split view: left project list, right project & repository inspector
	split_container = memnew(HSplitContainer);
	split_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	split_container->set_split_offset(int(360 * EDSCALE));
	add_child(split_container);

	VBoxContainer *left_vb = memnew(VBoxContainer);
	left_vb->set_custom_minimum_size(Size2(280 * EDSCALE, 0));
	left_vb->add_theme_constant_override("separation", int(6 * EDSCALE));
	split_container->add_child(left_vb);

	projects_header_label = memnew(Label(TTR("Studio Projects")));
	projects_header_label->set_theme_type_variation("HeaderSmall");
	left_vb->add_child(projects_header_label);

	projects_list = memnew(ItemList);
	projects_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	projects_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	projects_list->set_allow_reselect(true);
	projects_list->connect("item_selected", callable_mp(this, &StudioProjectsPanel::_on_project_selected));
	projects_list->connect("item_activated", callable_mp(this, &StudioProjectsPanel::_on_project_activated));
	left_vb->add_child(projects_list);

	PanelContainer *right_panel = memnew(PanelContainer);
	right_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	split_container->add_child(right_panel);

	ScrollContainer *right_scroll = memnew(ScrollContainer);
	right_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	right_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right_panel->add_child(right_scroll);

	VBoxContainer *right_outer_vb = memnew(VBoxContainer);
	right_outer_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_outer_vb->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right_outer_vb->add_theme_constant_override("separation", int(10 * EDSCALE));
	right_scroll->add_child(right_outer_vb);

	empty_detail_label = memnew(Label(TTR("Select a Studio project on the left to inspect its pipeline state, Lore VCS & Git repositories, and local Aetherion workspace.")));
	empty_detail_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	empty_detail_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_detail_label->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	empty_detail_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	right_outer_vb->add_child(empty_detail_label);

	detail_content_vb = memnew(VBoxContainer);
	detail_content_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_content_vb->add_theme_constant_override("separation", int(8 * EDSCALE));
	detail_content_vb->set_visible(false);
	right_outer_vb->add_child(detail_content_vb);

	detail_title_label = memnew(Label);
	detail_title_label->set_theme_type_variation("HeaderMedium");
	detail_content_vb->add_child(detail_title_label);

	detail_meta_label = memnew(Label);
	detail_meta_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	detail_content_vb->add_child(detail_meta_label);

	detail_milestone_label = memnew(Label);
	detail_milestone_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	detail_content_vb->add_child(detail_milestone_label);

	detail_content_vb->add_child(memnew(HSeparator));

	Label *local_header = memnew(Label(TTR("Local Aetherion Workspace")));
	local_header->set_theme_type_variation("HeaderSmall");
	detail_content_vb->add_child(local_header);

	detail_local_path_label = memnew(Label);
	detail_local_path_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	detail_content_vb->add_child(detail_local_path_label);

	HBoxContainer *local_actions_hb = memnew(HBoxContainer);
	local_actions_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	detail_content_vb->add_child(local_actions_hb);

	open_in_editor_button = memnew(Button(TTR("Open in Aetherion")));
	open_in_editor_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_in_editor_pressed));
	local_actions_hb->add_child(open_in_editor_button);

	bind_folder_button = memnew(Button(TTR("Bind Local Folder...")));
	bind_folder_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_bind_folder_pressed));
	local_actions_hb->add_child(bind_folder_button);

	show_in_fm_button = memnew(Button(TTR("Show in File Manager")));
	show_in_fm_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_show_in_fm_pressed));
	local_actions_hb->add_child(show_in_fm_button);

	open_intranet_button = memnew(Button(TTR("Open in Intranet")));
	open_intranet_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_intranet_pressed));
	local_actions_hb->add_child(open_intranet_button);

	export_project_config_button = memnew(Button(TTR("Export Project Config...")));
	export_project_config_button->set_tooltip_text(TTR("Export project-scoped Aetherion configuration manifest (.yaml / .json) with workspace, VCS repositories, and access permissions."));
	export_project_config_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_export_project_config_pressed));
	local_actions_hb->add_child(export_project_config_button);

	detail_content_vb->add_child(memnew(HSeparator));

	Label *vcs_header = memnew(Label(TTR("Lore VCS & Git Repositories")));
	vcs_header->set_theme_type_variation("HeaderSmall");
	detail_content_vb->add_child(vcs_header);

	lore_summary_label = memnew(Label);
	lore_summary_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	detail_content_vb->add_child(lore_summary_label);

	repos_tree = memnew(Tree);
	repos_tree->set_columns(5);
	repos_tree->set_column_titles_visible(true);
	repos_tree->set_column_title(0, TTR("Repository"));
	repos_tree->set_column_title(1, TTR("VCS"));
	repos_tree->set_column_title(2, TTR("Access"));
	repos_tree->set_column_title(3, TTR("Workspace / Locks"));
	repos_tree->set_column_title(4, TTR("Remote URI"));
	repos_tree->set_column_expand(0, true);
	repos_tree->set_column_expand(1, false);
	repos_tree->set_column_custom_minimum_width(1, int(70 * EDSCALE));
	repos_tree->set_column_expand(2, false);
	repos_tree->set_column_custom_minimum_width(2, int(90 * EDSCALE));
	repos_tree->set_column_expand(3, true);
	repos_tree->set_column_expand(4, true);
	repos_tree->set_hide_root(true);
	repos_tree->set_custom_minimum_size(Size2(0, 190 * EDSCALE));
	repos_tree->connect("item_selected", callable_mp(this, &StudioProjectsPanel::_on_repo_selected));
	detail_content_vb->add_child(repos_tree);

	HBoxContainer *repo_actions_hb = memnew(HBoxContainer);
	repo_actions_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	detail_content_vb->add_child(repo_actions_hb);

	clone_repo_button = memnew(Button(TTR("Clone Selected Repository...")));
	clone_repo_button->set_disabled(true);
	clone_repo_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_clone_repo_pressed));
	repo_actions_hb->add_child(clone_repo_button);

	copy_clone_cmd_button = memnew(Button(TTR("Copy Clone Command")));
	copy_clone_cmd_button->set_disabled(true);
	copy_clone_cmd_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_copy_clone_cmd_pressed));
	repo_actions_hb->add_child(copy_clone_cmd_button);

	// Dialogs & timers
	bind_dir_dialog = memnew(EditorFileDialog);
	bind_dir_dialog->set_previews_enabled(false);
	bind_dir_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	bind_dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	bind_dir_dialog->set_title(TTR("Select Local Project Checkout Folder"));
	bind_dir_dialog->connect("dir_selected", callable_mp(this, &StudioProjectsPanel::_on_bind_dir_selected));
	add_child(bind_dir_dialog);

	clone_dir_dialog = memnew(EditorFileDialog);
	clone_dir_dialog->set_previews_enabled(false);
	clone_dir_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	clone_dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	clone_dir_dialog->set_title(TTR("Select Parent Directory for Clone"));
	clone_dir_dialog->connect("dir_selected", callable_mp(this, &StudioProjectsPanel::_on_clone_parent_dir_selected));
	add_child(clone_dir_dialog);

	export_config_dialog = memnew(EditorFileDialog);
	export_config_dialog->set_previews_enabled(false);
	export_config_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	export_config_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	export_config_dialog->set_title(TTR("Export Studio Configuration Manifest"));
	export_config_dialog->add_filter("*.yaml, *.yml", TTR("YAML Configuration Manifest"));
	export_config_dialog->add_filter("*.json", TTR("JSON Configuration Manifest"));
	export_config_dialog->connect("file_selected", callable_mp(this, &StudioProjectsPanel::_on_export_config_file_selected));
	add_child(export_config_dialog);

	info_dialog = memnew(AcceptDialog);
	info_dialog->set_title(TTR("Studio"));
	add_child(info_dialog);

	fallback_account_dialog = memnew(AuthentikAccountDialog);
	add_child(fallback_account_dialog);

	clone_poll_timer = memnew(Timer);
	clone_poll_timer->set_wait_time(0.5);
	clone_poll_timer->set_one_shot(false);
	clone_poll_timer->connect("timeout", callable_mp(this, &StudioProjectsPanel::_on_clone_poll_timeout));
	add_child(clone_poll_timer);
}

StudioProjectsPanel::~StudioProjectsPanel() {
	if (studio_client.is_valid()) {
		studio_client->cancel();
	}
}

void StudioProjectsPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			Authentik::ensure_editor_settings_registered();
			if (Authentik::get_singleton()) {
				Authentik::get_singleton()->reload_settings();
			}
			_load_local_bindings();
			_connect_auth_signals();
			_update_theme_icons();
			_update_status_state();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_disconnect_auth_signals();
			if (studio_client.is_valid()) {
				studio_client->cancel();
			}
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (is_visible_in_tree()) {
				_load_local_bindings();
				if (!initial_fetch_done && studio_client.is_valid() && studio_client->is_configured() && studio_client->has_credentials()) {
					refresh_projects();
				} else {
					_update_status_state();
				}
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_update_theme_icons();
			_rebuild_project_list();
			_update_detail_pane();
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			EditorSettings *es = EditorSettings::get_singleton();
			if (es && es->check_changed_settings_in_group("network/authentik")) {
				if (Authentik::get_singleton()) {
					Authentik::get_singleton()->reload_settings();
				}
				if (is_visible_in_tree() && studio_client.is_valid() && studio_client->is_configured() && studio_client->has_credentials()) {
					refresh_projects();
				} else {
					_update_status_state();
				}
			}
		} break;
	}
}

void StudioProjectsPanel::_update_theme_icons() {
	if (!is_inside_tree()) {
		return;
	}
	refresh_button->set_button_icon(get_editor_theme_icon(SNAME("Reload")));
	account_button->set_button_icon(get_editor_theme_icon(SNAME("CryptoKey")));
	export_studio_config_button->set_button_icon(get_editor_theme_icon(SNAME("Save")));
	search_edit->set_right_icon(get_editor_theme_icon(SNAME("Search")));
	open_in_editor_button->set_button_icon(get_editor_theme_icon(SNAME("Edit")));
	bind_folder_button->set_button_icon(get_editor_theme_icon(SNAME("Folder")));
	show_in_fm_button->set_button_icon(get_editor_theme_icon(SNAME("Load")));
	open_intranet_button->set_button_icon(get_editor_theme_icon(SNAME("ExternalLink")));
	export_project_config_button->set_button_icon(get_editor_theme_icon(SNAME("Save")));
	clone_repo_button->set_button_icon(get_editor_theme_icon(SNAME("VcsBranches")));
	copy_clone_cmd_button->set_button_icon(get_editor_theme_icon(SNAME("ActionCopy")));
}

void StudioProjectsPanel::_connect_auth_signals() {
	if (auth_signals_connected || !Authentik::get_singleton()) {
		return;
	}
	Authentik *auth = Authentik::get_singleton();
	auth->connect("authenticated", callable_mp(this, &StudioProjectsPanel::_on_auth_session_changed));
	auth->connect("session_refreshed", callable_mp(this, &StudioProjectsPanel::_on_auth_session_changed));
	auth->connect("logged_out", callable_mp(this, &StudioProjectsPanel::_on_auth_changed));
	auth_signals_connected = true;
}

void StudioProjectsPanel::_disconnect_auth_signals() {
	if (!auth_signals_connected || !Authentik::get_singleton()) {
		auth_signals_connected = false;
		return;
	}
	Authentik *auth = Authentik::get_singleton();
	auth->disconnect("authenticated", callable_mp(this, &StudioProjectsPanel::_on_auth_session_changed));
	auth->disconnect("session_refreshed", callable_mp(this, &StudioProjectsPanel::_on_auth_session_changed));
	auth->disconnect("logged_out", callable_mp(this, &StudioProjectsPanel::_on_auth_changed));
	auth_signals_connected = false;
}

String StudioProjectsPanel::_get_bindings_file_path() const {
	if (EditorPaths::get_singleton() && EditorPaths::get_singleton()->are_paths_valid()) {
		return EditorPaths::get_singleton()->get_config_dir().path_join("authentik_studio_bindings.cfg");
	}
	return "user://authentik_studio_bindings.cfg";
}

void StudioProjectsPanel::_load_local_bindings() {
	local_bindings.clear();
	String cfg_path = _get_bindings_file_path();
	if (!FileAccess::exists(cfg_path)) {
		return;
	}
	Ref<ConfigFile> cfg;
	cfg.instantiate();
	if (cfg->load(cfg_path) != OK || !cfg->has_section("bindings")) {
		return;
	}
	Vector<String> keys = cfg->get_section_keys("bindings");
	for (const String &k : keys) {
		String val = String(cfg->get_value("bindings", k, String())).strip_edges();
		if (!val.is_empty()) {
			local_bindings[k] = val;
		}
	}
}

void StudioProjectsPanel::_save_local_bindings() const {
	Ref<ConfigFile> cfg;
	cfg.instantiate();
	for (const KeyValue<String, String> &E : local_bindings) {
		if (!E.key.is_empty() && !E.value.is_empty()) {
			cfg->set_value("bindings", E.key, E.value);
		}
	}
	cfg->save(_get_bindings_file_path());
}

String StudioProjectsPanel::_resolve_godot_project_dir(const String &p_selected_dir, const String &p_workspace_subpath) const {
	String base = p_selected_dir.strip_edges().simplify_path();
	if (base.is_empty()) {
		return String();
	}
	if (FileAccess::exists(base.path_join("project.godot"))) {
		return base;
	}
	String sub = p_workspace_subpath.strip_edges();
	if (!sub.is_empty() && sub != "." && !sub.begins_with("/") && !sub.contains("..")) {
		String candidate = base.path_join(sub).simplify_path();
		if (FileAccess::exists(candidate.path_join("project.godot"))) {
			return candidate;
		}
	}
	return base;
}

String StudioProjectsPanel::_get_bound_local_dir(const String &p_project_id) const {
	if (local_bindings.has(p_project_id)) {
		return local_bindings[p_project_id];
	}
	return String();
}

void StudioProjectsPanel::_update_status_state(const String &p_custom_message, bool p_is_error) {
	if (!is_inside_tree()) {
		return;
	}

	bool configured = studio_client.is_valid() && studio_client->is_configured();
	bool has_creds = studio_client.is_valid() && studio_client->has_credentials();
	bool busy = (studio_client.is_valid() && studio_client->is_busy()) || active_clone_pid != 0;

	refresh_button->set_disabled(busy || !configured || !has_creds);
	if (export_studio_config_button) {
		export_studio_config_button->set_disabled(busy || !configured || !has_creds);
	}
	if (export_project_config_button) {
		export_project_config_button->set_disabled(busy || !configured || !has_creds || _get_selected_project_data_index() < 0);
	}

	String msg = p_custom_message;
	bool show_action = false;
	bool error_style = p_is_error;

	if (msg.is_empty()) {
		if (!configured) {
			msg = TTR("Studio API URL is not configured yet. Click 'Configure / Sign In...' to set your Studio Server URL (e.g. https://studio.example.com) and Authentik account.");
			show_action = true;
		} else if (!has_creds) {
			msg = TTR("Sign in with your Authentik account to view your assigned Studio projects and Lore VCS repositories.");
			show_action = true;
		}
	}

	status_banner->set_visible(!msg.is_empty());
	status_label->set_text(msg);
	status_action_button->set_visible(show_action);

	Color col = error_style
			? get_theme_color(SNAME("error_color"), EditorStringName(Editor))
			: get_theme_color(SceneStringName(font_color), SNAME("Label"));
	status_label->add_theme_color_override(SceneStringName(font_color), col);
}

void StudioProjectsPanel::refresh_projects() {
	if (studio_client.is_null()) {
		return;
	}
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->reload_settings();
	}
	if (!studio_client->is_configured() || !studio_client->has_credentials()) {
		_update_status_state();
		return;
	}

	String target = target_user_edit->get_text().strip_edges();
	bool all_kinds = include_all_kinds_check->is_pressed();
	bool inc_archived = include_archived_check->is_pressed();

	_update_status_state(TTR("Fetching Studio projects from Studio Server..."), false);
	Error err = (target.is_empty() || target.to_lower() == "me")
			? studio_client->fetch_my_projects(all_kinds, inc_archived)
			: studio_client->fetch_user_projects(target, all_kinds, inc_archived);
	if (err == OK) {
		initial_fetch_done = true;
		refresh_button->set_disabled(true);
	}
}

void StudioProjectsPanel::_on_projects_loaded(const Array &p_projects, const Dictionary &p_user) {
	loaded_projects = p_projects;
	loaded_user = p_user;
	_update_status_state();

	String who = String(p_user.get("name", String()));
	if (who.is_empty()) {
		who = String(p_user.get("username", String()));
	}
	if (who.is_empty()) {
		who = String(p_user.get("email", String()));
	}
	if (!who.is_empty()) {
		projects_header_label->set_text(vformat(TTR("Studio Projects (%d) — %s"), loaded_projects.size(), who));
	} else {
		projects_header_label->set_text(vformat(TTR("Studio Projects (%d)"), loaded_projects.size()));
	}

	_rebuild_project_list();
}

void StudioProjectsPanel::_on_config_loaded(const String &p_tool, const String &p_project_ref, const String &p_format, const String &p_raw_content, const Dictionary &p_parsed_config) {
	if (pending_export_config_path.is_empty()) {
		_update_status_state();
		return;
	}
	String target_path = pending_export_config_path;
	pending_export_config_path = String();

	Ref<FileAccess> f = FileAccess::open(target_path, FileAccess::WRITE);
	if (f.is_null()) {
		_update_status_state(vformat(TTR("Failed to write configuration manifest to %s."), target_path), true);
		return;
	}
	f->store_string(p_raw_content);
	f->flush();

	String scope_label = p_project_ref.is_empty() ? TTR("studio-wide") : p_project_ref;
	_update_status_state(vformat(TTR("Exported %s (%s, %s) configuration manifest to %s."), p_tool, scope_label, p_format.to_upper(), target_path), false);
}

void StudioProjectsPanel::_on_request_failed(int p_http_code, const String &p_message) {
	pending_export_config_path = String();
	_update_status_state(p_message, true);
}

void StudioProjectsPanel::_on_auth_changed() {
	loaded_projects.clear();
	loaded_user.clear();
	initial_fetch_done = false;
	projects_header_label->set_text(TTR("Studio Projects"));
	_rebuild_project_list();
	_update_status_state();
}

void StudioProjectsPanel::_on_auth_session_changed(const Ref<AuthentikSession> &p_session) {
	if (is_visible_in_tree() && studio_client.is_valid() && studio_client->is_configured()) {
		refresh_projects();
	} else {
		_update_status_state();
	}
}

void StudioProjectsPanel::_rebuild_project_list() {
	String prev_selected_id;
	int cur_idx = _get_selected_project_data_index();
	if (cur_idx >= 0 && cur_idx < loaded_projects.size() && loaded_projects[cur_idx].get_type() == Variant::DICTIONARY) {
		prev_selected_id = String(Dictionary(loaded_projects[cur_idx]).get("id", String()));
	}

	projects_list->clear();
	filtered_project_indices.clear();

	String filter = search_edit->get_text().strip_edges().to_lower();
	int restore_list_idx = -1;

	for (int i = 0; i < loaded_projects.size(); i++) {
		if (loaded_projects[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary proj = loaded_projects[i];
		String pid = String(proj.get("id", String()));
		String name = String(proj.get("name", pid));
		String phase = _format_phase_label(String(proj.get("phase", String())));
		String role = String(proj.get("role", String()));
		String engine_raw = String(proj.get("engine", String())).strip_edges();
		Dictionary lore = proj.get("lore", Dictionary());
		String slug = String(lore.get("projectSlug", String()));

		if (!filter.is_empty()) {
			String haystack = (name + " " + slug + " " + engine_raw + " " + phase + " " + role).to_lower();
			if (!haystack.contains(filter)) {
				continue;
			}
		}

		String bound_dir = _get_bound_local_dir(pid);
		bool has_godot = !bound_dir.is_empty() && FileAccess::exists(bound_dir.path_join("project.godot"));
		int assigned_tasks = int(proj.get("assignedOpenTaskCount", 0));

		String label = name + "  [" + phase + " • " + role + "]";
		if (assigned_tasks > 0) {
			label += vformat(TTR("  (%d assigned)"), assigned_tasks);
		}
		if (has_godot) {
			label += TTR("  [Local]");
		}

		int list_idx = projects_list->add_item(label, get_editor_theme_icon(has_godot ? SNAME("StatusSuccess") : SNAME("Folder")));
		filtered_project_indices.push_back(i);
		if (!prev_selected_id.is_empty() && pid == prev_selected_id) {
			restore_list_idx = list_idx;
		}
	}

	if (restore_list_idx >= 0) {
		projects_list->select(restore_list_idx);
	} else if (projects_list->get_item_count() > 0) {
		projects_list->select(0);
	}

	_update_detail_pane();
}

int StudioProjectsPanel::_get_selected_project_data_index() const {
	Vector<int> sel = projects_list->get_selected_items();
	if (sel.is_empty()) {
		return -1;
	}
	int list_idx = sel[0];
	if (list_idx < 0 || list_idx >= filtered_project_indices.size()) {
		return -1;
	}
	return filtered_project_indices[list_idx];
}

void StudioProjectsPanel::_update_detail_pane() {
	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size() || loaded_projects[data_idx].get_type() != Variant::DICTIONARY) {
		empty_detail_label->set_visible(true);
		detail_content_vb->set_visible(false);
		return;
	}

	empty_detail_label->set_visible(false);
	detail_content_vb->set_visible(true);

	Dictionary proj = loaded_projects[data_idx];
	String pid = String(proj.get("id", String()));
	String name = String(proj.get("name", pid));
	String kind_raw = String(proj.get("kind", "digital")).strip_edges().to_lower();
	String kind = kind_raw.capitalize();
	String engine_raw = String(proj.get("engine", String())).strip_edges();
	String engine_label = engine_raw.is_empty() ? (kind_raw == "analogue" ? String("Tabletop") : String("Aetherion")) : engine_raw.capitalize();
	String phase = _format_phase_label(String(proj.get("phase", String())));
	String role = String(proj.get("role", String()));
	int open_tasks = int(proj.get("openTaskCount", 0));
	int assigned_tasks = int(proj.get("assignedOpenTaskCount", 0));
	String part_roles = _array_to_csv(proj.get("participantRoles", Array()));

	detail_title_label->set_text(name);

	String meta_text = vformat(TTR("Kind: %s   |   Engine: %s   |   Phase: %s   |   Studio Access: %s   |   Tasks: %d assigned / %d open"),
			kind, engine_label, phase, role, assigned_tasks, open_tasks);
	if (!part_roles.is_empty()) {
		meta_text += "\n" + vformat(TTR("Project Credits / Roles: %s"), part_roles);
	}
	detail_meta_label->set_text(meta_text);

	String sched_parts;
	if (proj.get("activeMilestone", Variant()).get_type() == Variant::DICTIONARY) {
		Dictionary ms = proj["activeMilestone"];
		String ms_name = String(ms.get("name", String()));
		String ms_due = String(ms.get("dueDate", String()));
		if (!ms_name.is_empty()) {
			sched_parts += vformat(TTR("Active Milestone: %s"), ms_name);
			if (!ms_due.is_empty()) {
				sched_parts += " (" + ms_due + ")";
			}
		}
	}
	if (proj.get("activeCycle", Variant()).get_type() == Variant::DICTIONARY) {
		Dictionary cyc = proj["activeCycle"];
		String cyc_name = String(cyc.get("name", String()));
		if (!cyc_name.is_empty()) {
			if (!sched_parts.is_empty()) {
				sched_parts += "   •   ";
			}
			sched_parts += vformat(TTR("Active Sprint / Cycle: %s"), cyc_name);
		}
	}
	detail_milestone_label->set_visible(!sched_parts.is_empty());
	detail_milestone_label->set_text(sched_parts);

	// Local binding state
	String bound_dir = _get_bound_local_dir(pid);
	bool dir_exists = !bound_dir.is_empty() && DirAccess::exists(bound_dir);
	bool has_project_godot = dir_exists && FileAccess::exists(bound_dir.path_join("project.godot"));

	if (bound_dir.is_empty()) {
		detail_local_path_label->set_text(TTR("Not bound to a local folder yet. Bind an existing checkout or clone a repository below."));
	} else if (has_project_godot) {
		detail_local_path_label->set_text(vformat(TTR("Bound Local Project: %s"), bound_dir));
	} else if (dir_exists) {
		detail_local_path_label->set_text(vformat(TTR("Bound Folder (no project.godot at root): %s"), bound_dir));
	} else {
		detail_local_path_label->set_text(vformat(TTR("Bound Folder Missing on Disk: %s"), bound_dir));
	}

	open_in_editor_button->set_disabled(!has_project_godot);
	show_in_fm_button->set_disabled(!dir_exists);
	open_intranet_button->set_disabled(String(proj.get("intranetUrl", String())).is_empty());
	if (export_project_config_button) {
		bool can_export = studio_client.is_valid() && studio_client->is_configured() && studio_client->has_credentials() && !studio_client->is_busy() && active_clone_pid == 0;
		export_project_config_button->set_disabled(!can_export);
	}

	// Lore summary
	Dictionary lore = proj.get("lore", Dictionary());
	String lore_slug = String(lore.get("projectSlug", String()));
	String lore_vis = String(lore.get("visibility", "internal"));
	String lore_actions = _array_to_csv(lore.get("actions", Array()));
	bool can_create_repo = bool(lore.get("canCreateRepo", false));
	if (!lore_slug.is_empty()) {
		lore_summary_label->set_text(vformat(
				TTR("Lore Namespace: %s/*   |   Visibility: %s   |   Permissions: %s%s"),
				lore_slug,
				lore_vis,
				lore_actions.is_empty() ? TTR("none") : lore_actions,
				can_create_repo ? TTR(" (can create repos)") : String()));
		lore_summary_label->set_visible(true);
	} else {
		lore_summary_label->set_visible(false);
	}

	// Repositories tree
	repos_tree->clear();
	TreeItem *root = repos_tree->create_item();
	Array repos = proj.get("repositories", Array());
	TreeItem *first_item = nullptr;
	for (int i = 0; i < repos.size(); i++) {
		if (repos[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary r = repos[i];
		TreeItem *item = repos_tree->create_item(root);
		String r_name = String(r.get("name", String(r.get("slug", "repo"))));
		String r_lore_full = String(r.get("loreFullName", String()));
		if (!r_lore_full.is_empty()) {
			r_name += " (" + r_lore_full + ")";
		}
		String r_sys = String(r.get("system", "git")).to_upper();
		bool can_write = bool(r.get("canWrite", false));
		String r_ws = String(r.get("workspacePath", "."));
		int lock_count = int(r.get("lockCount", 0));
		String ws_locks = r_ws;
		if (lock_count > 0) {
			ws_locks += vformat(TTR(" • %d lock(s)"), lock_count);
		}

		item->set_text(0, r_name);
		item->set_icon(0, get_editor_theme_icon(SNAME("VcsBranches")));
		item->set_text(1, r_sys);
		item->set_text(2, can_write ? TTR("Read / Write") : TTR("Read-Only"));
		item->set_text(3, ws_locks);
		item->set_text(4, String(r.get("remoteUri", String())));
		item->set_metadata(0, r);
		if (!first_item) {
			first_item = item;
		}
	}

	if (first_item) {
		first_item->select(0);
	}
	_on_repo_selected();
}

Dictionary StudioProjectsPanel::_get_selected_repo_dict() const {
	TreeItem *sel = repos_tree->get_selected();
	if (!sel) {
		return Dictionary();
	}
	Variant meta = sel->get_metadata(0);
	if (meta.get_type() == Variant::DICTIONARY) {
		return meta;
	}
	return Dictionary();
}

void StudioProjectsPanel::_on_repo_selected() {
	Dictionary repo = _get_selected_repo_dict();
	Array cmd = repo.get("cloneCommand", Array());
	bool can_clone = !cmd.is_empty() && active_clone_pid == 0;
	clone_repo_button->set_disabled(!can_clone);
	copy_clone_cmd_button->set_disabled(cmd.is_empty());
}

void StudioProjectsPanel::_on_refresh_pressed() {
	refresh_projects();
}

void StudioProjectsPanel::_on_open_account_pressed() {
	if (has_connections("configure_account_requested")) {
		emit_signal("configure_account_requested");
		return;
	}
	if (fallback_account_dialog) {
		fallback_account_dialog->open_dialog();
	}
}

void StudioProjectsPanel::_on_search_changed(const String &p_text) {
	_rebuild_project_list();
}

void StudioProjectsPanel::_on_target_user_submitted(const String &p_text) {
	refresh_projects();
}

void StudioProjectsPanel::_on_filter_toggled(bool p_pressed) {
	refresh_projects();
}

void StudioProjectsPanel::_on_project_selected(int p_list_idx) {
	_update_detail_pane();
}

void StudioProjectsPanel::_on_project_activated(int p_list_idx) {
	_update_detail_pane();
	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	String pid = String(proj.get("id", String()));
	String bound_dir = _get_bound_local_dir(pid);
	if (!bound_dir.is_empty() && FileAccess::exists(bound_dir.path_join("project.godot"))) {
		emit_signal("open_project_requested", bound_dir);
	}
}

void StudioProjectsPanel::_on_open_in_editor_pressed() {
	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	String bound_dir = _get_bound_local_dir(String(proj.get("id", String())));
	if (!bound_dir.is_empty() && FileAccess::exists(bound_dir.path_join("project.godot"))) {
		emit_signal("open_project_requested", bound_dir);
	}
}

void StudioProjectsPanel::_on_bind_folder_pressed() {
	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	pending_bind_project_id = String(proj.get("id", String()));
	pending_bind_workspace_subpath = String(proj.get("primaryWorkspacePath", "."));

	String existing = _get_bound_local_dir(pending_bind_project_id);
	if (!existing.is_empty() && DirAccess::exists(existing)) {
		bind_dir_dialog->set_current_dir(existing);
	} else if (EditorSettings::get_singleton()) {
		String def_dir = EDITOR_GET("filesystem/directories/default_project_path");
		if (!def_dir.is_empty() && DirAccess::exists(def_dir)) {
			bind_dir_dialog->set_current_dir(def_dir);
		}
	}
	bind_dir_dialog->popup_file_dialog();
}

void StudioProjectsPanel::_on_bind_dir_selected(const String &p_dir) {
	if (pending_bind_project_id.is_empty()) {
		return;
	}
	String resolved = _resolve_godot_project_dir(p_dir, pending_bind_workspace_subpath);
	local_bindings[pending_bind_project_id] = resolved;
	_save_local_bindings();

	if (FileAccess::exists(resolved.path_join("project.godot"))) {
		emit_signal("project_bound", resolved);
	}
	_rebuild_project_list();
}

void StudioProjectsPanel::_on_show_in_fm_pressed() {
	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	String bound_dir = _get_bound_local_dir(String(proj.get("id", String())));
	if (!bound_dir.is_empty() && DirAccess::exists(bound_dir) && OS::get_singleton()) {
		OS::get_singleton()->shell_show_in_file_manager(bound_dir, true);
	}
}

void StudioProjectsPanel::_on_open_intranet_pressed() {
	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	String url = String(proj.get("intranetUrl", String())).strip_edges();
	if ((url.begins_with("https://") || url.begins_with("http://")) && OS::get_singleton()) {
		OS::get_singleton()->shell_open(url);
	}
}

void StudioProjectsPanel::_on_export_studio_config_pressed() {
	if (!export_config_dialog || studio_client.is_null() || !studio_client->is_configured() || !studio_client->has_credentials()) {
		return;
	}
	pending_export_project_ref = String();
	if (EditorSettings::get_singleton()) {
		String def_dir = EDITOR_GET("filesystem/directories/default_project_path");
		if (!def_dir.is_empty() && DirAccess::exists(def_dir)) {
			export_config_dialog->set_current_dir(def_dir);
		}
	}
	export_config_dialog->set_title(TTR("Export Studio-Wide Aetherion Configuration"));
	export_config_dialog->set_current_file("studio-aetherion.yaml");
	export_config_dialog->popup_file_dialog();
}

void StudioProjectsPanel::_on_export_project_config_pressed() {
	int data_idx = _get_selected_project_data_index();
	if (!export_config_dialog || data_idx < 0 || data_idx >= loaded_projects.size() || studio_client.is_null()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	String pid = String(proj.get("id", String())).strip_edges();
	Dictionary lore = proj.get("lore", Dictionary());
	String slug = String(lore.get("projectSlug", proj.get("slug", String()))).strip_edges();
	String ref = !slug.is_empty() ? slug : pid;
	if (ref.is_empty()) {
		return;
	}

	pending_export_project_ref = ref;
	String safe_prefix = OS::get_singleton() ? OS::get_singleton()->get_safe_dir_name(slug.is_empty() ? "project" : slug) : (slug.is_empty() ? String("project") : slug);
	String bound_dir = _get_bound_local_dir(pid);
	if (!bound_dir.is_empty() && DirAccess::exists(bound_dir)) {
		export_config_dialog->set_current_dir(bound_dir);
	} else if (EditorSettings::get_singleton()) {
		String def_dir = EDITOR_GET("filesystem/directories/default_project_path");
		if (!def_dir.is_empty() && DirAccess::exists(def_dir)) {
			export_config_dialog->set_current_dir(def_dir);
		}
	}
	export_config_dialog->set_title(TTR("Export Project Aetherion Configuration"));
	export_config_dialog->set_current_file(safe_prefix + "-aetherion.yaml");
	export_config_dialog->popup_file_dialog();
}

void StudioProjectsPanel::_on_export_config_file_selected(const String &p_path) {
	if (studio_client.is_null() || p_path.strip_edges().is_empty()) {
		return;
	}
	pending_export_config_path = p_path.strip_edges();
	String ext = pending_export_config_path.get_extension().to_lower();
	String fmt = (ext == "json") ? "json" : "yaml";

	_update_status_state(TTR("Generating configuration manifest from Studio Server..."), false);
	Error err = pending_export_project_ref.is_empty()
			? studio_client->fetch_studio_config("aetherion", fmt)
			: studio_client->fetch_project_config(pending_export_project_ref, "aetherion", fmt);
	if (err == OK) {
		_update_status_state(TTR("Generating configuration manifest from Studio Server..."), false);
	} else {
		pending_export_config_path = String();
	}
}

void StudioProjectsPanel::_on_copy_clone_cmd_pressed() {
	Dictionary repo = _get_selected_repo_dict();
	Array cmd = repo.get("cloneCommand", Array());
	if (cmd.is_empty() || !DisplayServer::get_singleton()) {
		return;
	}
	String joined;
	for (int i = 0; i < cmd.size(); i++) {
		if (i > 0) {
			joined += " ";
		}
		joined += String(cmd[i]);
	}
	DisplayServer::get_singleton()->clipboard_set(joined);
	_update_status_state(vformat(TTR("Copied clone command to clipboard: %s"), joined), false);
}

void StudioProjectsPanel::_on_clone_repo_pressed() {
	int data_idx = _get_selected_project_data_index();
	Dictionary repo = _get_selected_repo_dict();
	if (data_idx < 0 || data_idx >= loaded_projects.size() || repo.is_empty()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	pending_clone_project_id = String(proj.get("id", String()));
	pending_clone_repo = repo;

	if (EditorSettings::get_singleton()) {
		String def_dir = EDITOR_GET("filesystem/directories/default_project_path");
		if (!def_dir.is_empty() && DirAccess::exists(def_dir)) {
			clone_dir_dialog->set_current_dir(def_dir);
		}
	}
	clone_dir_dialog->popup_file_dialog();
}

void StudioProjectsPanel::_on_clone_parent_dir_selected(const String &p_parent_dir) {
	if (pending_clone_repo.is_empty() || !OS::get_singleton()) {
		return;
	}

	String sys = String(pending_clone_repo.get("system", "git")).to_lower();
	String remote = String(pending_clone_repo.get("remoteUri", String())).strip_edges();
	String slug = String(pending_clone_repo.get("slug", "repo")).strip_edges();
	if (slug.is_empty()) {
		slug = "studio-repo";
	}
	slug = OS::get_singleton()->get_safe_dir_name(slug);

	// Reject malformed or option-like remote URIs so VCS binaries never interpret remoteUri as a CLI flag.
	if (remote.is_empty() || remote.begins_with("-")) {
		info_dialog->set_text(TTR("Cannot clone repository: invalid remote URI."));
		info_dialog->popup_centered();
		return;
	}

	String bin = (sys == "lore") ? "lore" : "git";
	String target_dir = p_parent_dir.simplify_path().path_join(slug);
	if (DirAccess::exists(target_dir)) {
		info_dialog->set_text(vformat(TTR("Target folder already exists:\n%s\n\nUse 'Bind Local Folder...' to bind an existing checkout."), target_dir));
		info_dialog->popup_centered();
		return;
	}

	List<String> args;
	args.push_back("clone");
	if (bin == "git") {
		args.push_back("--");
	}
	args.push_back(remote);
	args.push_back(target_dir);

	OS::ProcessID pid = 0;
	Error err = OS::get_singleton()->create_process(bin, args, &pid, false);
	if (err != OK || pid == 0) {
		info_dialog->set_text(vformat(TTR("Could not launch '%s' CLI on PATH.\nUse 'Copy Clone Command' to run '%s clone %s' in your terminal."), bin, bin, remote));
		info_dialog->popup_centered();
		return;
	}

	active_clone_pid = pid;
	active_clone_project_id = pending_clone_project_id;
	active_clone_target_dir = target_dir;
	active_clone_workspace_subpath = String(pending_clone_repo.get("workspacePath", "."));
	clone_repo_button->set_disabled(true);
	refresh_button->set_disabled(true);
	_update_status_state(vformat(TTR("Cloning %s into %s..."), remote, target_dir), false);
	clone_poll_timer->start();
}

void StudioProjectsPanel::_on_clone_poll_timeout() {
	if (active_clone_pid == 0 || !OS::get_singleton()) {
		clone_poll_timer->stop();
		return;
	}
	if (OS::get_singleton()->is_process_running(active_clone_pid)) {
		return;
	}

	int exit_code = OS::get_singleton()->get_process_exit_code(active_clone_pid);
	OS::ProcessID finished_pid = active_clone_pid;
	(void)finished_pid;
	active_clone_pid = 0;
	clone_poll_timer->stop();

	if (exit_code == 0 && DirAccess::exists(active_clone_target_dir)) {
		String resolved = _resolve_godot_project_dir(active_clone_target_dir, active_clone_workspace_subpath);
		if (!active_clone_project_id.is_empty()) {
			local_bindings[active_clone_project_id] = resolved;
			_save_local_bindings();
		}
		if (FileAccess::exists(resolved.path_join("project.godot"))) {
			emit_signal("project_bound", resolved);
		}
		_update_status_state(vformat(TTR("Repository cloned successfully to %s."), resolved), false);
		_rebuild_project_list();
	} else {
		_update_status_state(vformat(TTR("Repository clone exited with code %d."), exit_code), true);
		_on_repo_selected();
	}
}

void StudioProjectsPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("refresh_projects"), &StudioProjectsPanel::refresh_projects);

	ADD_SIGNAL(MethodInfo("project_bound", PropertyInfo(Variant::STRING, "project_dir")));
	ADD_SIGNAL(MethodInfo("open_project_requested", PropertyInfo(Variant::STRING, "project_dir")));
	ADD_SIGNAL(MethodInfo("configure_account_requested"));
}

#endif // TOOLS_ENABLED
