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

#include "core/input/input_event.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
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
#include "scene/gui/flow_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/split_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/tree.h"
#include "scene/main/timer.h"
#include "scene/resources/style_box_flat.h"
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

static Ref<StyleBoxFlat> _make_card_style(const Color &p_bg, const Color &p_border, int p_radius, float p_pad_h, float p_pad_v, int p_border_w = 1) {
	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	sb->set_bg_color(p_bg);
	sb->set_border_color(p_border);
	sb->set_border_width_all(p_border_w);
	sb->set_corner_radius_all(p_radius);
	sb->set_content_margin_individual(p_pad_h, p_pad_v, p_pad_h, p_pad_v);
	return sb;
}

static PanelContainer *_make_pill_badge(const String &p_text, const Color &p_tint_color, const Ref<Font> &p_bold_font, int p_font_size) {
	PanelContainer *pill = memnew(PanelContainer);
	pill->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	pill->set_v_size_flags(Control::SIZE_SHRINK_CENTER);

	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	sb->set_bg_color(Color(p_tint_color.r, p_tint_color.g, p_tint_color.b, 0.16f));
	sb->set_border_color(Color(p_tint_color.r, p_tint_color.g, p_tint_color.b, 0.42f));
	sb->set_border_width_all(MAX(1, int(Math::round(EDSCALE))));
	sb->set_corner_radius_all(int(4 * EDSCALE));
	sb->set_content_margin_individual(6 * EDSCALE, 2 * EDSCALE, 6 * EDSCALE, 2 * EDSCALE);
	pill->add_theme_style_override(SceneStringName(panel), sb);

	Label *lbl = memnew(Label(p_text));
	lbl->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	if (p_bold_font.is_valid()) {
		lbl->add_theme_font_override(SceneStringName(font), p_bold_font);
	}
	lbl->add_theme_font_size_override(SceneStringName(font_size), p_font_size);
	lbl->add_theme_color_override(SceneStringName(font_color), p_tint_color.lightened(0.12f));
	pill->add_child(lbl);
	return pill;
}

static PanelContainer *_build_kpi_card(const String &p_caption, Label *&r_title_lbl, Label *&r_val_lbl) {
	PanelContainer *card = memnew(PanelContainer);
	card->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	card->set_custom_minimum_size(Size2(110 * EDSCALE, 0));

	VBoxContainer *vb = memnew(VBoxContainer);
	vb->add_theme_constant_override("separation", int(2 * EDSCALE));
	card->add_child(vb);

	r_title_lbl = memnew(Label(p_caption));
	vb->add_child(r_title_lbl);

	r_val_lbl = memnew(Label("-"));
	r_val_lbl->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	vb->add_child(r_val_lbl);

	return card;
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

	// ========================================================================
	// 1. Top Identity & Global Studio Actions Header
	// ========================================================================
	top_header_panel = memnew(PanelContainer);
	top_header_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(top_header_panel);

	HBoxContainer *header_hb = memnew(HBoxContainer);
	header_hb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header_hb->add_theme_constant_override("separation", int(10 * EDSCALE));
	top_header_panel->add_child(header_hb);

	connection_status_icon = memnew(TextureRect);
	connection_status_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	connection_status_icon->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	header_hb->add_child(connection_status_icon);

	VBoxContainer *conn_text_vb = memnew(VBoxContainer);
	conn_text_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	conn_text_vb->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	conn_text_vb->add_theme_constant_override("separation", int(1 * EDSCALE));
	header_hb->add_child(conn_text_vb);

	connection_title_label = memnew(Label(TTR("Studio Workspace")));
	connection_title_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	conn_text_vb->add_child(connection_title_label);

	connection_subtitle_label = memnew(Label);
	connection_subtitle_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	conn_text_vb->add_child(connection_subtitle_label);

	HBoxContainer *header_actions_hb = memnew(HBoxContainer);
	header_actions_hb->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	header_actions_hb->add_theme_constant_override("separation", int(6 * EDSCALE));
	header_hb->add_child(header_actions_hb);

	refresh_button = memnew(Button(TTR("Refresh")));
	refresh_button->set_tooltip_text(TTR("Reload assigned Studio projects and repository metadata."));
	refresh_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_refresh_pressed));
	header_actions_hb->add_child(refresh_button);

	import_studio_config_button = memnew(Button(TTR("Import Config...")));
	import_studio_config_button->set_tooltip_text(TTR("Import a Studio configuration manifest (.json) to configure Authentik SSO and Studio Server automatically."));
	import_studio_config_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_import_studio_config_pressed));
	header_actions_hb->add_child(import_studio_config_button);

	export_studio_config_button = memnew(Button(TTR("Export Config...")));
	export_studio_config_button->set_tooltip_text(TTR("Export a studio-wide Aetherion configuration manifest (.yaml / .json)."));
	export_studio_config_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_export_studio_config_pressed));
	header_actions_hb->add_child(export_studio_config_button);

	account_button = memnew(Button(TTR("Account & Server...")));
	account_button->set_tooltip_text(TTR("Manage your Authentik SSO session, OIDC provider, and Studio Server URL."));
	account_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_account_pressed));
	header_actions_hb->add_child(account_button);

	// ========================================================================
	// 2. Search, Scope & Sort Filter Bar
	// ========================================================================
	filter_bar_hb = memnew(HBoxContainer);
	filter_bar_hb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	filter_bar_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	add_child(filter_bar_hb);

	search_edit = memnew(LineEdit);
	search_edit->set_placeholder(TTR("Filter Studio projects by name, slug, engine, phase, or role..."));
	search_edit->set_clear_button_enabled(true);
	search_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	search_edit->connect(SceneStringName(text_changed), callable_mp(this, &StudioProjectsPanel::_on_search_changed));
	filter_bar_hb->add_child(search_edit);

	Label *user_lbl = memnew(Label(TTR("User:")));
	filter_bar_hb->add_child(user_lbl);

	target_user_edit = memnew(LineEdit);
	target_user_edit->set_placeholder(TTR("me (or username)"));
	target_user_edit->set_clear_button_enabled(true);
	target_user_edit->set_custom_minimum_size(Size2(150 * EDSCALE, 0));
	target_user_edit->set_tooltip_text(TTR("Leave empty or 'me' for your projects, or enter a colleague's username/email and press Enter."));
	target_user_edit->connect(SceneStringName(text_submitted), callable_mp(this, &StudioProjectsPanel::_on_target_user_submitted));
	filter_bar_hb->add_child(target_user_edit);

	Label *kind_lbl = memnew(Label(TTR("Show:")));
	filter_bar_hb->add_child(kind_lbl);

	kind_filter_option = memnew(OptionButton);
	kind_filter_option->add_item(TTR("Digital & Hybrid"), KIND_FILTER_DIGITAL_HYBRID);
	kind_filter_option->add_item(TTR("All Kinds (+ Analogue)"), KIND_FILTER_ALL_KINDS);
	kind_filter_option->add_item(TTR("Aetherion Engine Only"), KIND_FILTER_AETHERION_ONLY);
	kind_filter_option->connect(SceneStringName(item_selected), callable_mp(this, &StudioProjectsPanel::_on_kind_filter_changed));
	filter_bar_hb->add_child(kind_filter_option);

	Label *sort_lbl = memnew(Label(TTR("Sort:")));
	filter_bar_hb->add_child(sort_lbl);

	sort_option = memnew(OptionButton);
	sort_option->add_item(TTR("Local Linked First"), SORT_LOCAL_FIRST);
	sort_option->add_item(TTR("Name (A-Z)"), SORT_NAME_ASC);
	sort_option->add_item(TTR("Assigned Tasks"), SORT_ASSIGNED_TASKS);
	sort_option->add_item(TTR("Pipeline Phase"), SORT_PHASE);
	sort_option->connect(SceneStringName(item_selected), callable_mp(this, &StudioProjectsPanel::_on_sort_changed));
	filter_bar_hb->add_child(sort_option);

	include_archived_check = memnew(CheckBox);
	include_archived_check->set_text(TTR("Archived"));
	include_archived_check->set_tooltip_text(TTR("Include archived Studio projects."));
	include_archived_check->connect(SceneStringName(toggled), callable_mp(this, &StudioProjectsPanel::_on_filter_toggled));
	filter_bar_hb->add_child(include_archived_check);

	// ========================================================================
	// 3. Transient Activity / Error Toast Banner
	// ========================================================================
	status_banner = memnew(PanelContainer);
	status_banner->set_visible(false);
	add_child(status_banner);

	HBoxContainer *status_hb = memnew(HBoxContainer);
	status_hb->add_theme_constant_override("separation", int(10 * EDSCALE));
	status_banner->add_child(status_hb);

	status_icon = memnew(TextureRect);
	status_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	status_icon->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	status_hb->add_child(status_icon);

	status_label = memnew(Label);
	status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_hb->add_child(status_label);

	status_action_button = memnew(Button(TTR("Settings...")));
	status_action_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_account_pressed));
	status_hb->add_child(status_action_button);

	status_dismiss_button = memnew(Button);
	status_dismiss_button->set_flat(true);
	status_dismiss_button->set_tooltip_text(TTR("Dismiss notice"));
	status_dismiss_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_clear_transient_status));
	status_hb->add_child(status_dismiss_button);

	// ========================================================================
	// 4. Hero Onboarding / Empty-State View
	// ========================================================================
	onboarding_outer_panel = memnew(PanelContainer);
	onboarding_outer_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	onboarding_outer_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(onboarding_outer_panel);

	VBoxContainer *onboarding_center_vb = memnew(VBoxContainer);
	onboarding_center_vb->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	onboarding_center_vb->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	onboarding_outer_panel->add_child(onboarding_center_vb);

	onboarding_card = memnew(PanelContainer);
	onboarding_card->set_custom_minimum_size(Size2(540 * EDSCALE, 0));
	onboarding_center_vb->add_child(onboarding_card);

	VBoxContainer *ob_vb = memnew(VBoxContainer);
	ob_vb->add_theme_constant_override("separation", int(12 * EDSCALE));
	onboarding_card->add_child(ob_vb);

	HBoxContainer *ob_icon_hb = memnew(HBoxContainer);
	ob_icon_hb->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	ob_vb->add_child(ob_icon_hb);

	onboarding_icon = memnew(TextureRect);
	onboarding_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	onboarding_icon->set_custom_minimum_size(Size2(36 * EDSCALE, 36 * EDSCALE));
	ob_icon_hb->add_child(onboarding_icon);

	onboarding_title_label = memnew(Label);
	onboarding_title_label->set_theme_type_variation("HeaderMedium");
	onboarding_title_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	onboarding_title_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	ob_vb->add_child(onboarding_title_label);

	onboarding_desc_label = memnew(Label);
	onboarding_desc_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	onboarding_desc_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	onboarding_desc_label->set_custom_minimum_size(Size2(460 * EDSCALE, 0));
	ob_vb->add_child(onboarding_desc_label);

	onboarding_server_pill = memnew(Label);
	onboarding_server_pill->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	onboarding_server_pill->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	ob_vb->add_child(onboarding_server_pill);

	// Inline Device Code Verification Sub-Card
	onboarding_device_panel = memnew(PanelContainer);
	onboarding_device_panel->set_visible(false);
	ob_vb->add_child(onboarding_device_panel);

	VBoxContainer *dev_vb = memnew(VBoxContainer);
	dev_vb->add_theme_constant_override("separation", int(6 * EDSCALE));
	onboarding_device_panel->add_child(dev_vb);

	Label *dev_hint = memnew(Label(TTR("Confirm this verification code in your browser:")));
	dev_hint->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	dev_vb->add_child(dev_hint);

	onboarding_device_code_label = memnew(Label);
	onboarding_device_code_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	dev_vb->add_child(onboarding_device_code_label);

	onboarding_device_url_label = memnew(Label);
	onboarding_device_url_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	onboarding_device_url_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	dev_vb->add_child(onboarding_device_url_label);

	HBoxContainer *dev_btns = memnew(HBoxContainer);
	dev_btns->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	dev_btns->add_theme_constant_override("separation", int(8 * EDSCALE));
	dev_vb->add_child(dev_btns);

	onboarding_copy_code_btn = memnew(Button(TTR("Copy Code")));
	onboarding_copy_code_btn->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_copy_device_code_pressed));
	dev_btns->add_child(onboarding_copy_code_btn);

	onboarding_open_verify_btn = memnew(Button(TTR("Open Verification Page")));
	onboarding_open_verify_btn->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_device_verify_pressed));
	dev_btns->add_child(onboarding_open_verify_btn);

	FlowContainer *ob_actions = memnew(FlowContainer);
	ob_actions->set_alignment(FlowContainer::ALIGNMENT_CENTER);
	ob_actions->add_theme_constant_override("h_separation", int(8 * EDSCALE));
	ob_actions->add_theme_constant_override("v_separation", int(8 * EDSCALE));
	ob_vb->add_child(ob_actions);

	onboarding_login_browser_btn = memnew(Button(TTR("Sign In with Browser")));
	onboarding_login_browser_btn->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_login_browser_pressed));
	ob_actions->add_child(onboarding_login_browser_btn);

	onboarding_login_device_btn = memnew(Button(TTR("Sign In with Device Code")));
	onboarding_login_device_btn->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_login_device_pressed));
	ob_actions->add_child(onboarding_login_device_btn);

	onboarding_import_config_btn = memnew(Button(TTR("Import Studio Config (.json)...")));
	onboarding_import_config_btn->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_import_studio_config_pressed));
	ob_actions->add_child(onboarding_import_config_btn);

	onboarding_configure_btn = memnew(Button(TTR("Configure Server & Authentik...")));
	onboarding_configure_btn->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_account_pressed));
	ob_actions->add_child(onboarding_configure_btn);

	onboarding_copy_url_btn = memnew(Button(TTR("Copy Sign-In URL")));
	onboarding_copy_url_btn->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_copy_login_url_pressed));
	ob_actions->add_child(onboarding_copy_url_btn);

	onboarding_cancel_login_btn = memnew(Button(TTR("Cancel Sign-In")));
	onboarding_cancel_login_btn->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_cancel_login_pressed));
	ob_actions->add_child(onboarding_cancel_login_btn);

	onboarding_clear_filters_btn = memnew(Button(TTR("Clear Active Filters")));
	onboarding_clear_filters_btn->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_clear_filters_pressed));
	ob_actions->add_child(onboarding_clear_filters_btn);

	// ========================================================================
	// 5. Split Workspace: Rich Project Cards (Left) + Inspector (Right)
	// ========================================================================
	split_container = memnew(HSplitContainer);
	split_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	split_container->set_split_offset(int(390 * EDSCALE));
	add_child(split_container);

	// --- Left Column: Project Cards ---
	VBoxContainer *left_vb = memnew(VBoxContainer);
	left_vb->set_custom_minimum_size(Size2(300 * EDSCALE, 0));
	left_vb->add_theme_constant_override("separation", int(6 * EDSCALE));
	split_container->add_child(left_vb);

	HBoxContainer *left_hdr_hb = memnew(HBoxContainer);
	left_hdr_hb->add_theme_constant_override("separation", int(6 * EDSCALE));
	left_vb->add_child(left_hdr_hb);

	projects_header_label = memnew(Label(TTR("Studio Projects")));
	projects_header_label->set_theme_type_variation("HeaderSmall");
	projects_header_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	left_hdr_hb->add_child(projects_header_label);

	projects_stats_label = memnew(Label);
	projects_stats_label->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	left_hdr_hb->add_child(projects_stats_label);

	projects_list_panel = memnew(PanelContainer);
	projects_list_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	projects_list_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	left_vb->add_child(projects_list_panel);

	projects_scroll = memnew(ScrollContainer);
	projects_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	projects_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	projects_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	projects_list_panel->add_child(projects_scroll);

	MarginContainer *cards_margin = memnew(MarginContainer);
	cards_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	cards_margin->add_theme_constant_override("margin_left", int(6 * EDSCALE));
	cards_margin->add_theme_constant_override("margin_top", int(6 * EDSCALE));
	cards_margin->add_theme_constant_override("margin_right", int(6 * EDSCALE));
	cards_margin->add_theme_constant_override("margin_bottom", int(6 * EDSCALE));
	projects_scroll->add_child(cards_margin);

	projects_cards_vb = memnew(VBoxContainer);
	projects_cards_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	projects_cards_vb->add_theme_constant_override("separation", int(6 * EDSCALE));
	cards_margin->add_child(projects_cards_vb);

	// --- Right Column: Project & Repository Inspector ---
	right_panel = memnew(PanelContainer);
	right_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	split_container->add_child(right_panel);

	ScrollContainer *right_scroll = memnew(ScrollContainer);
	right_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	right_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right_panel->add_child(right_scroll);

	MarginContainer *right_margin = memnew(MarginContainer);
	right_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right_margin->add_theme_constant_override("margin_left", int(16 * EDSCALE));
	right_margin->add_theme_constant_override("margin_top", int(14 * EDSCALE));
	right_margin->add_theme_constant_override("margin_right", int(16 * EDSCALE));
	right_margin->add_theme_constant_override("margin_bottom", int(16 * EDSCALE));
	right_scroll->add_child(right_margin);

	VBoxContainer *right_outer_vb = memnew(VBoxContainer);
	right_outer_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_outer_vb->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right_margin->add_child(right_outer_vb);

	empty_detail_label = memnew(Label(TTR("Select a Studio project on the left to inspect its pipeline status, local Aetherion workspace, and Lore VCS / Git repositories.")));
	empty_detail_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	empty_detail_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_detail_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	empty_detail_label->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right_outer_vb->add_child(empty_detail_label);

	detail_content_vb = memnew(VBoxContainer);
	detail_content_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_content_vb->add_theme_constant_override("separation", int(12 * EDSCALE));
	detail_content_vb->set_visible(false);
	right_outer_vb->add_child(detail_content_vb);

	// Inspector Header: Title + Slug + Intranet & Export buttons
	HBoxContainer *proj_hero_hb = memnew(HBoxContainer);
	proj_hero_hb->add_theme_constant_override("separation", int(10 * EDSCALE));
	detail_content_vb->add_child(proj_hero_hb);

	VBoxContainer *proj_title_vb = memnew(VBoxContainer);
	proj_title_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	proj_title_vb->add_theme_constant_override("separation", int(2 * EDSCALE));
	proj_hero_hb->add_child(proj_title_vb);

	detail_title_label = memnew(Label);
	detail_title_label->set_theme_type_variation("HeaderLarge");
	detail_title_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	proj_title_vb->add_child(detail_title_label);

	detail_slug_label = memnew(Label);
	detail_slug_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	proj_title_vb->add_child(detail_slug_label);

	HBoxContainer *proj_quick_hb = memnew(HBoxContainer);
	proj_quick_hb->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	proj_quick_hb->add_theme_constant_override("separation", int(6 * EDSCALE));
	proj_hero_hb->add_child(proj_quick_hb);

	open_intranet_button = memnew(Button(TTR("Intranet Page")));
	open_intranet_button->set_tooltip_text(TTR("Open this project's documentation & hub page in the Studio Intranet."));
	open_intranet_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_intranet_pressed));
	proj_quick_hb->add_child(open_intranet_button);

	export_project_config_button = memnew(Button(TTR("Export Manifest...")));
	export_project_config_button->set_tooltip_text(TTR("Export project-scoped Aetherion configuration manifest (.yaml / .json)."));
	export_project_config_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_export_project_config_pressed));
	proj_quick_hb->add_child(export_project_config_button);

	// KPI Strip (4 cards)
	HBoxContainer *kpi_hb = memnew(HBoxContainer);
	kpi_hb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	kpi_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	detail_content_vb->add_child(kpi_hb);

	kpi_engine_card = _build_kpi_card(TTR("ENGINE & KIND"), kpi_engine_title_lbl, kpi_engine_value_lbl);
	kpi_hb->add_child(kpi_engine_card);

	kpi_phase_card = _build_kpi_card(TTR("PIPELINE PHASE"), kpi_phase_title_lbl, kpi_phase_value_lbl);
	kpi_hb->add_child(kpi_phase_card);

	kpi_role_card = _build_kpi_card(TTR("STUDIO ACCESS"), kpi_role_title_lbl, kpi_role_value_lbl);
	kpi_hb->add_child(kpi_role_card);

	kpi_tasks_card = _build_kpi_card(TTR("TASKS"), kpi_tasks_title_lbl, kpi_tasks_value_lbl);
	kpi_hb->add_child(kpi_tasks_card);

	detail_credits_label = memnew(Label);
	detail_credits_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	detail_credits_label->set_visible(false);
	detail_content_vb->add_child(detail_credits_label);

	// Active Milestone / Cycle Banner
	milestone_banner = memnew(PanelContainer);
	milestone_banner->set_visible(false);
	detail_content_vb->add_child(milestone_banner);

	HBoxContainer *ms_hb = memnew(HBoxContainer);
	ms_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	milestone_banner->add_child(ms_hb);

	milestone_icon = memnew(TextureRect);
	milestone_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	milestone_icon->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	ms_hb->add_child(milestone_icon);

	detail_milestone_label = memnew(Label);
	detail_milestone_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_milestone_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	ms_hb->add_child(detail_milestone_label);

	// --- Card 1: Local Aetherion Workspace ---
	local_workspace_card = memnew(PanelContainer);
	local_workspace_card->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_content_vb->add_child(local_workspace_card);

	VBoxContainer *local_vb = memnew(VBoxContainer);
	local_vb->add_theme_constant_override("separation", int(10 * EDSCALE));
	local_workspace_card->add_child(local_vb);

	HBoxContainer *local_hdr_hb = memnew(HBoxContainer);
	local_hdr_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	local_vb->add_child(local_hdr_hb);

	local_status_icon = memnew(TextureRect);
	local_status_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	local_status_icon->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	local_hdr_hb->add_child(local_status_icon);

	local_header_label = memnew(Label(TTR("Local Aetherion Workspace")));
	local_header_label->set_theme_type_variation("HeaderSmall");
	local_header_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	local_hdr_hb->add_child(local_header_label);

	local_status_pill = memnew(PanelContainer);
	local_status_pill->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	local_hdr_hb->add_child(local_status_pill);

	local_status_pill_label = memnew(Label);
	local_status_pill->add_child(local_status_pill_label);

	local_path_box = memnew(PanelContainer);
	local_path_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	local_vb->add_child(local_path_box);

	HBoxContainer *path_hb = memnew(HBoxContainer);
	path_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	local_path_box->add_child(path_hb);

	detail_local_path_label = memnew(Label);
	detail_local_path_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_local_path_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	path_hb->add_child(detail_local_path_label);

	copy_local_path_button = memnew(Button);
	copy_local_path_button->set_flat(true);
	copy_local_path_button->set_tooltip_text(TTR("Copy local workspace path to clipboard"));
	copy_local_path_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_copy_local_path_pressed));
	path_hb->add_child(copy_local_path_button);

	FlowContainer *local_actions_flow = memnew(FlowContainer);
	local_actions_flow->add_theme_constant_override("h_separation", int(8 * EDSCALE));
	local_actions_flow->add_theme_constant_override("v_separation", int(8 * EDSCALE));
	local_vb->add_child(local_actions_flow);

	open_in_editor_button = memnew(Button(TTR("Open in Aetherion")));
	open_in_editor_button->set_tooltip_text(TTR("Open this bound project directly in the Aetherion Editor."));
	open_in_editor_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_open_in_editor_pressed));
	local_actions_flow->add_child(open_in_editor_button);

	run_project_button = memnew(Button(TTR("Run Project")));
	run_project_button->set_tooltip_text(TTR("Run this bound project without opening the full editor."));
	run_project_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_run_project_pressed));
	local_actions_flow->add_child(run_project_button);

	bind_folder_button = memnew(Button(TTR("Bind Local Folder...")));
	bind_folder_button->set_tooltip_text(TTR("Link an existing local checkout directory on this workstation."));
	bind_folder_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_bind_folder_pressed));
	local_actions_flow->add_child(bind_folder_button);

	show_in_fm_button = memnew(Button(TTR("Show in File Manager")));
	show_in_fm_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_show_in_fm_pressed));
	local_actions_flow->add_child(show_in_fm_button);

	unbind_folder_button = memnew(Button(TTR("Unbind")));
	unbind_folder_button->set_tooltip_text(TTR("Remove the local directory association (does not delete any files on disk)."));
	unbind_folder_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_unbind_folder_pressed));
	local_actions_flow->add_child(unbind_folder_button);

	// --- Card 2: Lore VCS & Git Repositories ---
	vcs_card = memnew(PanelContainer);
	vcs_card->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_content_vb->add_child(vcs_card);

	VBoxContainer *vcs_vb = memnew(VBoxContainer);
	vcs_vb->add_theme_constant_override("separation", int(10 * EDSCALE));
	vcs_card->add_child(vcs_vb);

	HBoxContainer *vcs_hdr_hb = memnew(HBoxContainer);
	vcs_hdr_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	vcs_vb->add_child(vcs_hdr_hb);

	vcs_header_icon = memnew(TextureRect);
	vcs_header_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	vcs_header_icon->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	vcs_hdr_hb->add_child(vcs_header_icon);

	vcs_header_label = memnew(Label(TTR("Lore VCS & Git Repositories")));
	vcs_header_label->set_theme_type_variation("HeaderSmall");
	vcs_header_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vcs_hdr_hb->add_child(vcs_header_label);

	lore_summary_label = memnew(Label);
	lore_summary_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	vcs_vb->add_child(lore_summary_label);

	empty_repos_label = memnew(Label(TTR("No Lore VCS or Git repositories are registered on this Studio project yet.")));
	empty_repos_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	empty_repos_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_repos_label->set_custom_minimum_size(Size2(0, 64 * EDSCALE));
	empty_repos_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	empty_repos_label->set_visible(false);
	vcs_vb->add_child(empty_repos_label);

	repos_tree = memnew(Tree);
	repos_tree->set_columns(5);
	repos_tree->set_column_titles_visible(true);
	repos_tree->set_column_title(0, TTR("Repository"));
	repos_tree->set_column_title(1, TTR("VCS"));
	repos_tree->set_column_title(2, TTR("Access"));
	repos_tree->set_column_title(3, TTR("Workspace / Locks"));
	repos_tree->set_column_title(4, TTR("Remote URI"));
	repos_tree->set_column_expand(0, true);
	repos_tree->set_column_custom_minimum_width(0, int(140 * EDSCALE));
	repos_tree->set_column_expand(1, false);
	repos_tree->set_column_custom_minimum_width(1, int(68 * EDSCALE));
	repos_tree->set_column_expand(2, false);
	repos_tree->set_column_custom_minimum_width(2, int(98 * EDSCALE));
	repos_tree->set_column_expand(3, true);
	repos_tree->set_column_custom_minimum_width(3, int(110 * EDSCALE));
	repos_tree->set_column_expand(4, true);
	repos_tree->set_column_custom_minimum_width(4, int(150 * EDSCALE));
	repos_tree->set_hide_root(true);
	repos_tree->set_custom_minimum_size(Size2(0, 176 * EDSCALE));
	repos_tree->connect("item_selected", callable_mp(this, &StudioProjectsPanel::_on_repo_selected));
	vcs_vb->add_child(repos_tree);

	FlowContainer *repo_actions_flow = memnew(FlowContainer);
	repo_actions_flow->add_theme_constant_override("h_separation", int(8 * EDSCALE));
	repo_actions_flow->add_theme_constant_override("v_separation", int(8 * EDSCALE));
	vcs_vb->add_child(repo_actions_flow);

	clone_repo_button = memnew(Button(TTR("Clone Selected Repository...")));
	clone_repo_button->set_disabled(true);
	clone_repo_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_clone_repo_pressed));
	repo_actions_flow->add_child(clone_repo_button);

	copy_clone_cmd_button = memnew(Button(TTR("Copy Clone Command")));
	copy_clone_cmd_button->set_disabled(true);
	copy_clone_cmd_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_copy_clone_cmd_pressed));
	repo_actions_flow->add_child(copy_clone_cmd_button);

	copy_remote_uri_button = memnew(Button(TTR("Copy Remote URI")));
	copy_remote_uri_button->set_disabled(true);
	copy_remote_uri_button->connect(SceneStringName(pressed), callable_mp(this, &StudioProjectsPanel::_on_copy_remote_uri_pressed));
	repo_actions_flow->add_child(copy_remote_uri_button);

	// ========================================================================
	// 6. File Dialogs & Background Clone Poller
	// ========================================================================
	bind_dir_dialog = memnew(EditorFileDialog);
	bind_dir_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	bind_dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	bind_dir_dialog->set_title(TTR("Select Local Project Checkout Folder"));
	bind_dir_dialog->connect("dir_selected", callable_mp(this, &StudioProjectsPanel::_on_bind_dir_selected));
	add_child(bind_dir_dialog);

	clone_dir_dialog = memnew(EditorFileDialog);
	clone_dir_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	clone_dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	clone_dir_dialog->set_title(TTR("Select Parent Directory for Clone"));
	clone_dir_dialog->connect("dir_selected", callable_mp(this, &StudioProjectsPanel::_on_clone_parent_dir_selected));
	add_child(clone_dir_dialog);

	import_config_dialog = memnew(EditorFileDialog);
	import_config_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	import_config_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	import_config_dialog->set_title(TTR("Import Studio Configuration Manifest (.json)"));
	import_config_dialog->add_filter("*.json", TTR("Studio Configuration Manifest"));
	import_config_dialog->connect("file_selected", callable_mp(this, &StudioProjectsPanel::_on_import_config_file_selected));
	add_child(import_config_dialog);

	export_config_dialog = memnew(EditorFileDialog);
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
			_apply_theme_styles();
			_update_ui_state();
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
					_update_ui_state();
				}
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_apply_theme_styles();
			_rebuild_project_list();
			_update_ui_state();
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
					_update_ui_state();
				}
			}
		} break;
	}
}

void StudioProjectsPanel::_apply_theme_styles() {
	if (!is_inside_tree()) {
		return;
	}

	const Color base_col = get_theme_color(SNAME("base_color"), EditorStringName(Editor));
	const Color dark_col = get_theme_color(SNAME("dark_color_2"), EditorStringName(Editor));
	const Color accent_col = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color font_col = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	const Color muted_col = get_theme_color(SNAME("font_placeholder_color"), EditorStringName(Editor));
	const bool is_dark = base_col.get_luminance() < 0.5f;

	const Color surface_elevated = is_dark ? base_col.lightened(0.045f) : base_col.darkened(0.03f);
	const Color surface_card = is_dark ? base_col.lightened(0.025f) : base_col.lightened(0.02f);
	const Color surface_recessed = is_dark ? dark_col : base_col.darkened(0.055f);
	const Color border_subtle = is_dark ? base_col.lightened(0.13f) : base_col.darkened(0.12f);
	const int border_px = MAX(1, int(Math::round(EDSCALE)));
	const int radius_md = int(6 * EDSCALE);
	const int radius_lg = int(8 * EDSCALE);

	const Ref<Font> bold_font = get_theme_font(SNAME("bold"), EditorStringName(EditorFonts));
	const Ref<Font> source_font = get_theme_font(SNAME("source"), EditorStringName(EditorFonts));
	const int base_font_size = get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts));
	const int small_font_size = MAX(10, int(base_font_size * 0.86f));

	// Top header bar
	top_header_panel->add_theme_style_override(SceneStringName(panel),
			_make_card_style(surface_elevated, border_subtle, radius_md, 12 * EDSCALE, 8 * EDSCALE, border_px));
	connection_title_label->add_theme_font_override(SceneStringName(font), bold_font);
	connection_subtitle_label->add_theme_font_size_override(SceneStringName(font_size), small_font_size);
	connection_subtitle_label->add_theme_color_override(SceneStringName(font_color), muted_col);

	// Onboarding outer & hero card
	if (has_theme_stylebox("project_list", "ProjectManager")) {
		Ref<StyleBox> pl_sb = get_theme_stylebox("project_list", "ProjectManager");
		onboarding_outer_panel->add_theme_style_override(SceneStringName(panel), pl_sb);
		projects_list_panel->add_theme_style_override(SceneStringName(panel), pl_sb);
		right_panel->add_theme_style_override(SceneStringName(panel), pl_sb);
	} else {
		Ref<StyleBoxFlat> fallback_sb = _make_card_style(surface_recessed, border_subtle, radius_md, 4 * EDSCALE, 4 * EDSCALE, border_px);
		onboarding_outer_panel->add_theme_style_override(SceneStringName(panel), fallback_sb);
		projects_list_panel->add_theme_style_override(SceneStringName(panel), fallback_sb);
		right_panel->add_theme_style_override(SceneStringName(panel), fallback_sb);
	}

	Ref<StyleBoxFlat> hero_sb = _make_card_style(surface_elevated, border_subtle, radius_lg, 28 * EDSCALE, 24 * EDSCALE, border_px);
	hero_sb->set_border_width(SIDE_TOP, MAX(2, int(2 * EDSCALE)));
	hero_sb->set_border_color(Color(accent_col.r, accent_col.g, accent_col.b, 0.72f));
	onboarding_card->add_theme_style_override(SceneStringName(panel), hero_sb);

	onboarding_desc_label->add_theme_color_override(SceneStringName(font_color), font_col * Color(1, 1, 1, 0.85f));
	onboarding_server_pill->add_theme_font_override(SceneStringName(font), source_font);
	onboarding_server_pill->add_theme_font_size_override(SceneStringName(font_size), small_font_size);
	onboarding_server_pill->add_theme_color_override(SceneStringName(font_color), accent_col);

	onboarding_device_panel->add_theme_style_override(SceneStringName(panel),
			_make_card_style(surface_recessed, Color(accent_col.r, accent_col.g, accent_col.b, 0.45f), radius_md, 16 * EDSCALE, 12 * EDSCALE, border_px));
	onboarding_device_code_label->add_theme_font_override(SceneStringName(font), source_font);
	onboarding_device_code_label->add_theme_font_size_override(SceneStringName(font_size), int(base_font_size * 1.5f));
	onboarding_device_code_label->add_theme_color_override(SceneStringName(font_color), accent_col);
	onboarding_device_url_label->add_theme_font_size_override(SceneStringName(font_size), small_font_size);
	onboarding_device_url_label->add_theme_color_override(SceneStringName(font_color), muted_col);

	// Primary CTA styling on Open in Aetherion & Onboarding primary buttons
	Ref<StyleBoxFlat> cta_normal = _make_card_style(
			Color(accent_col.r, accent_col.g, accent_col.b, is_dark ? 0.22f : 0.18f),
			Color(accent_col.r, accent_col.g, accent_col.b, 0.75f),
			radius_md, 12 * EDSCALE, 6 * EDSCALE, border_px);
	Ref<StyleBoxFlat> cta_hover = _make_card_style(
			Color(accent_col.r, accent_col.g, accent_col.b, is_dark ? 0.34f : 0.28f),
			accent_col,
			radius_md, 12 * EDSCALE, 6 * EDSCALE, border_px);
	Ref<StyleBoxFlat> cta_pressed = _make_card_style(
			Color(accent_col.r, accent_col.g, accent_col.b, 0.42f),
			accent_col,
			radius_md, 12 * EDSCALE, 6 * EDSCALE, border_px);
	Ref<StyleBoxFlat> cta_disabled = _make_card_style(
			Color(base_col.r, base_col.g, base_col.b, 0.3f),
			border_subtle,
			radius_md, 12 * EDSCALE, 6 * EDSCALE, border_px);

	Button *primary_btns[] = { open_in_editor_button, onboarding_login_browser_btn, onboarding_open_verify_btn };
	for (Button *btn : primary_btns) {
		btn->add_theme_style_override(CoreStringName(normal), cta_normal);
		btn->add_theme_style_override("hover", cta_hover);
		btn->add_theme_style_override("pressed", cta_pressed);
		btn->add_theme_style_override("disabled", cta_disabled);
		btn->add_theme_font_override(SceneStringName(font), bold_font);
	}

	// Left header stats & empty labels
	projects_stats_label->add_theme_font_size_override(SceneStringName(font_size), small_font_size);
	projects_stats_label->add_theme_color_override(SceneStringName(font_color), muted_col);
	empty_detail_label->add_theme_color_override(SceneStringName(font_color), muted_col);
	empty_repos_label->add_theme_color_override(SceneStringName(font_color), muted_col);
	detail_slug_label->add_theme_font_override(SceneStringName(font), source_font);
	detail_slug_label->add_theme_font_size_override(SceneStringName(font_size), small_font_size);
	detail_slug_label->add_theme_color_override(SceneStringName(font_color), accent_col);
	detail_credits_label->add_theme_font_size_override(SceneStringName(font_size), small_font_size);
	detail_credits_label->add_theme_color_override(SceneStringName(font_color), muted_col);

	// KPI stat cards
	Ref<StyleBoxFlat> kpi_sb = _make_card_style(surface_card, border_subtle, radius_md, 10 * EDSCALE, 8 * EDSCALE, border_px);
	PanelContainer *kpi_cards[] = { kpi_engine_card, kpi_phase_card, kpi_role_card, kpi_tasks_card };
	Label *kpi_titles[] = { kpi_engine_title_lbl, kpi_phase_title_lbl, kpi_role_title_lbl, kpi_tasks_title_lbl };
	Label *kpi_vals[] = { kpi_engine_value_lbl, kpi_phase_value_lbl, kpi_role_value_lbl, kpi_tasks_value_lbl };
	for (int i = 0; i < 4; i++) {
		kpi_cards[i]->add_theme_style_override(SceneStringName(panel), kpi_sb);
		kpi_titles[i]->add_theme_font_size_override(SceneStringName(font_size), MAX(9, int(base_font_size * 0.75f)));
		kpi_titles[i]->add_theme_color_override(SceneStringName(font_color), muted_col);
		kpi_vals[i]->add_theme_font_override(SceneStringName(font), bold_font);
	}

	// Milestone banner
	Ref<StyleBoxFlat> ms_sb = _make_card_style(
			Color(accent_col.r, accent_col.g, accent_col.b, 0.10f),
			Color(accent_col.r, accent_col.g, accent_col.b, 0.38f),
			radius_md, 12 * EDSCALE, 8 * EDSCALE, border_px);
	milestone_banner->add_theme_style_override(SceneStringName(panel), ms_sb);

	// Inspector section cards (Local Workspace & Repositories)
	Ref<StyleBoxFlat> section_sb = _make_card_style(surface_card, border_subtle, radius_lg, 14 * EDSCALE, 12 * EDSCALE, border_px);
	local_workspace_card->add_theme_style_override(SceneStringName(panel), section_sb);
	vcs_card->add_theme_style_override(SceneStringName(panel), section_sb);

	local_path_box->add_theme_style_override(SceneStringName(panel),
			_make_card_style(surface_recessed, border_subtle, radius_md, 10 * EDSCALE, 7 * EDSCALE, border_px));
	detail_local_path_label->add_theme_font_override(SceneStringName(font), source_font);
	detail_local_path_label->add_theme_font_size_override(SceneStringName(font_size), small_font_size);
	lore_summary_label->add_theme_font_size_override(SceneStringName(font_size), small_font_size);
	lore_summary_label->add_theme_color_override(SceneStringName(font_color), muted_col);

	// Icons
	refresh_button->set_button_icon(get_editor_theme_icon(SNAME("Reload")));
	import_studio_config_button->set_button_icon(get_editor_theme_icon(SNAME("Load")));
	export_studio_config_button->set_button_icon(get_editor_theme_icon(SNAME("Save")));
	account_button->set_button_icon(get_editor_theme_icon(SNAME("CryptoKey")));
	search_edit->set_right_icon(get_editor_theme_icon(SNAME("Search")));
	status_dismiss_button->set_button_icon(get_editor_theme_icon(SNAME("Close")));

	onboarding_login_browser_btn->set_button_icon(get_editor_theme_icon(SNAME("ExternalLink")));
	onboarding_login_device_btn->set_button_icon(get_editor_theme_icon(SNAME("Key")));
	onboarding_import_config_btn->set_button_icon(get_editor_theme_icon(SNAME("Load")));
	onboarding_configure_btn->set_button_icon(get_editor_theme_icon(SNAME("Tools")));
	onboarding_copy_url_btn->set_button_icon(get_editor_theme_icon(SNAME("ActionCopy")));
	onboarding_cancel_login_btn->set_button_icon(get_editor_theme_icon(SNAME("Close")));
	onboarding_clear_filters_btn->set_button_icon(get_editor_theme_icon(SNAME("Clear")));
	onboarding_copy_code_btn->set_button_icon(get_editor_theme_icon(SNAME("ActionCopy")));
	onboarding_open_verify_btn->set_button_icon(get_editor_theme_icon(SNAME("ExternalLink")));

	milestone_icon->set_texture(get_editor_theme_icon(SNAME("Time")));
	open_intranet_button->set_button_icon(get_editor_theme_icon(SNAME("ExternalLink")));
	export_project_config_button->set_button_icon(get_editor_theme_icon(SNAME("Save")));

	copy_local_path_button->set_button_icon(get_editor_theme_icon(SNAME("ActionCopy")));
	open_in_editor_button->set_button_icon(get_editor_theme_icon(SNAME("Edit")));
	run_project_button->set_button_icon(get_editor_theme_icon(SNAME("Play")));
	bind_folder_button->set_button_icon(get_editor_theme_icon(SNAME("Folder")));
	show_in_fm_button->set_button_icon(get_editor_theme_icon(SNAME("Load")));
	unbind_folder_button->set_button_icon(get_editor_theme_icon(SNAME("Clear")));

	vcs_header_icon->set_texture(get_editor_theme_icon(SNAME("VcsBranches")));
	clone_repo_button->set_button_icon(get_editor_theme_icon(SNAME("VcsBranches")));
	copy_clone_cmd_button->set_button_icon(get_editor_theme_icon(SNAME("ActionCopy")));
	copy_remote_uri_button->set_button_icon(get_editor_theme_icon(SNAME("ActionCopy")));
}

void StudioProjectsPanel::_connect_auth_signals() {
	if (auth_signals_connected || !Authentik::get_singleton()) {
		return;
	}
	Authentik *auth = Authentik::get_singleton();
	auth->connect("authenticated", callable_mp(this, &StudioProjectsPanel::_on_auth_session_changed));
	auth->connect("session_refreshed", callable_mp(this, &StudioProjectsPanel::_on_auth_session_changed));
	auth->connect("logged_out", callable_mp(this, &StudioProjectsPanel::_on_auth_changed));
	auth->connect("state_changed", callable_mp(this, &StudioProjectsPanel::_on_auth_state_changed));
	auth->connect("authorization_url_ready", callable_mp(this, &StudioProjectsPanel::_on_auth_url_ready));
	auth->connect("device_code_received", callable_mp(this, &StudioProjectsPanel::_on_auth_device_code));
	auth->connect("authentication_failed", callable_mp(this, &StudioProjectsPanel::_on_auth_failed));
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
	auth->disconnect("state_changed", callable_mp(this, &StudioProjectsPanel::_on_auth_state_changed));
	auth->disconnect("authorization_url_ready", callable_mp(this, &StudioProjectsPanel::_on_auth_url_ready));
	auth->disconnect("device_code_received", callable_mp(this, &StudioProjectsPanel::_on_auth_device_code));
	auth->disconnect("authentication_failed", callable_mp(this, &StudioProjectsPanel::_on_auth_failed));
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

void StudioProjectsPanel::_set_transient_status(const String &p_message, bool p_is_error) {
	transient_status_message = p_message;
	transient_status_is_error = p_is_error;
	_update_ui_state();
}

void StudioProjectsPanel::_clear_transient_status() {
	transient_status_message.clear();
	transient_status_is_error = false;
	_update_ui_state();
}

void StudioProjectsPanel::_update_ui_state() {
	if (!is_inside_tree()) {
		return;
	}

	Authentik *auth = Authentik::get_singleton();
	const bool auth_configured = auth && auth->is_configured();
	const bool studio_configured = studio_client.is_valid() && studio_client->is_configured();
	const bool fully_configured = studio_configured;
	const bool has_creds = studio_client.is_valid() && studio_client->has_credentials();
	const bool studio_busy = studio_client.is_valid() && studio_client->is_busy();
	const bool auth_busy = auth && auth->is_busy();
	const bool busy = studio_busy || active_clone_pid != 0;

	// 1. Update Top Identity & Server Header
	String api_url = studio_client.is_valid() ? studio_client->get_api_url() : String();
	if (has_creds && auth && auth->get_session().is_valid()) {
		Ref<AuthentikSession> sess = auth->get_session();
		String user_name = sess->get_display_name();
		if (user_name.is_empty()) {
			user_name = sess->get_username();
		}
		if (user_name.is_empty()) {
			user_name = sess->get_email();
		}
		if (user_name.is_empty()) {
			user_name = TTR("Authenticated User");
		}
		PackedStringArray groups = sess->get_groups();
		String group_badge = !groups.is_empty() ? ("  •  " + groups[0]) : String();
		connection_status_icon->set_texture(get_editor_theme_icon(SNAME("StatusSuccess")));
		connection_title_label->set_text(user_name + group_badge);
		connection_subtitle_label->set_text(api_url.is_empty()
						? TTR("Signed in via Authentik — Studio Server URL not set yet")
						: vformat(TTR("Connected to Studio Server: %s"), api_url));
		account_button->set_text(TTR("Account & Server..."));
		account_button->set_button_icon(get_editor_theme_icon(SNAME("StatusSuccess")));
	} else if (fully_configured || auth_configured) {
		connection_status_icon->set_texture(get_editor_theme_icon(SNAME("StatusWarning")));
		connection_title_label->set_text(TTR("Studio Session Signed Out"));
		connection_subtitle_label->set_text(!api_url.is_empty()
						? vformat(TTR("Server ready: %s — sign in to load projects"), api_url)
						: vformat(TTR("OIDC Issuer: %s"), auth ? auth->get_issuer_url() : String()));
		account_button->set_text(TTR("Sign In / Settings..."));
		account_button->set_button_icon(get_editor_theme_icon(SNAME("CryptoKey")));
	} else {
		connection_status_icon->set_texture(get_editor_theme_icon(SNAME("Info")));
		connection_title_label->set_text(TTR("Studio Integration Not Configured"));
		connection_subtitle_label->set_text(TTR("Import a Studio configuration (.json) or configure your Authentik & Studio Server URLs."));
		account_button->set_text(TTR("Configure Studio..."));
		account_button->set_button_icon(get_editor_theme_icon(SNAME("Tools")));
	}

	refresh_button->set_disabled(busy || !fully_configured || !has_creds);
	export_studio_config_button->set_disabled(busy || !fully_configured || !has_creds);
	if (export_project_config_button) {
		export_project_config_button->set_disabled(busy || !fully_configured || !has_creds || _get_selected_project_data_index() < 0);
	}

	// 2. Transient Status Banner
	const Color base_col = get_theme_color(SNAME("base_color"), EditorStringName(Editor));
	const Color accent_col = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color error_col = get_theme_color(SNAME("error_color"), EditorStringName(Editor));
	const Color font_col = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	const int border_px = MAX(1, int(Math::round(EDSCALE)));

	if (!transient_status_message.is_empty()) {
		status_banner->set_visible(true);
		status_label->set_text(transient_status_message);
		Color tint = transient_status_is_error ? error_col : accent_col;
		Ref<StyleBoxFlat> sb = _make_card_style(
				Color(tint.r, tint.g, tint.b, 0.11f),
				Color(tint.r, tint.g, tint.b, 0.45f),
				int(6 * EDSCALE), 12 * EDSCALE, 8 * EDSCALE, border_px);
		sb->set_border_width(SIDE_LEFT, MAX(3, int(3 * EDSCALE)));
		status_banner->add_theme_style_override(SceneStringName(panel), sb);
		status_icon->set_texture(get_editor_theme_icon(transient_status_is_error ? SNAME("StatusError") : SNAME("Info")));
		status_label->add_theme_color_override(SceneStringName(font_color), transient_status_is_error ? error_col.lightened(0.15f) : font_col);
		status_action_button->set_visible(transient_status_is_error);
		status_dismiss_button->set_visible(!busy);
	} else {
		status_banner->set_visible(false);
		(void)base_col;
	}

	// 3. Decide between Hero Onboarding View vs. Split Workspace View
	const bool has_any_loaded = !loaded_projects.is_empty();
	const bool has_filtered = !filtered_project_indices.is_empty();
	const bool show_workspace = fully_configured && has_creds && has_filtered;

	filter_bar_hb->set_visible(fully_configured && has_creds && (has_any_loaded || initial_fetch_done));
	split_container->set_visible(show_workspace);
	onboarding_outer_panel->set_visible(!show_workspace);

	if (!show_workspace) {
		onboarding_device_panel->set_visible(false);
		onboarding_login_browser_btn->set_visible(false);
		onboarding_login_device_btn->set_visible(false);
		onboarding_import_config_btn->set_visible(false);
		onboarding_configure_btn->set_visible(false);
		onboarding_copy_url_btn->set_visible(false);
		onboarding_cancel_login_btn->set_visible(false);
		onboarding_clear_filters_btn->set_visible(false);
		onboarding_server_pill->set_visible(!api_url.is_empty());
		onboarding_server_pill->set_text(api_url);

		int auth_st = auth ? auth->get_status() : 0;
		Ref<AuthentikClient> client = auth ? auth->get_client() : Ref<AuthentikClient>();

		if (!fully_configured) {
			onboarding_icon->set_texture(get_editor_theme_icon(SNAME("Groups")));
			onboarding_title_label->set_text(TTR("Connect Aetherion to Your Studio"));
			onboarding_desc_label->set_text(TTR("Link your Studio Server and Authentik SSO account to browse assigned game projects, inspect active milestones, and clone Lore VCS or Git repositories straight into Aetherion."));
			onboarding_import_config_btn->set_visible(true);
			onboarding_configure_btn->set_visible(true);
		} else if (!has_creds) {
			onboarding_icon->set_texture(get_editor_theme_icon(SNAME("CryptoKey")));
			if (auth_busy && auth_st == AuthentikClient::STATUS_AWAITING_DEVICE_CODE && client.is_valid()) {
				onboarding_title_label->set_text(TTR("Verify Device Sign-In"));
				onboarding_desc_label->set_text(TTR("Open the verification page in your browser and confirm the code below with your Studio account."));
				onboarding_device_panel->set_visible(true);
				onboarding_device_code_label->set_text(client->get_user_code());
				onboarding_device_url_label->set_text(client->get_verification_uri());
				onboarding_cancel_login_btn->set_visible(true);
			} else if (auth_busy) {
				onboarding_title_label->set_text(TTR("Waiting for Authentik Sign-In..."));
				onboarding_desc_label->set_text(TTR("Complete authentication in your browser window. Aetherion will connect automatically once approved."));
				onboarding_copy_url_btn->set_visible(client.is_valid() && !client->get_last_authorization_url().is_empty());
				onboarding_cancel_login_btn->set_visible(true);
			} else {
				onboarding_title_label->set_text(TTR("Sign In to Studio Workspace"));
				onboarding_desc_label->set_text(auth_configured
								? TTR("Authenticate with your Studio account via Browser (PKCE) or Device Code to access your assigned projects and Lore VCS repositories.")
								: TTR("Studio Server URL is set, but your OIDC Provider (Issuer URL & Client ID) is not configured yet."));
				onboarding_login_browser_btn->set_visible(auth_configured);
				onboarding_login_device_btn->set_visible(auth_configured);
				onboarding_import_config_btn->set_visible(true);
				onboarding_configure_btn->set_visible(true);
			}
		} else if (studio_busy && !initial_fetch_done) {
			onboarding_icon->set_texture(get_editor_theme_icon(SNAME("Reload")));
			onboarding_title_label->set_text(TTR("Synchronizing Studio Projects..."));
			onboarding_desc_label->set_text(TTR("Fetching your assigned projects, permissions, and repository metadata from the Studio Server."));
		} else if (has_any_loaded && !has_filtered) {
			onboarding_icon->set_texture(get_editor_theme_icon(SNAME("Search")));
			onboarding_title_label->set_text(TTR("No Projects Match Your Current Filters"));
			onboarding_desc_label->set_text(TTR("Try clearing the search query or switching the 'Show' filter to include all project kinds."));
			onboarding_clear_filters_btn->set_visible(true);
		} else {
			onboarding_icon->set_texture(get_editor_theme_icon(SNAME("Folder")));
			onboarding_title_label->set_text(TTR("No Assigned Digital Studio Projects Found"));
			onboarding_desc_label->set_text(TTR("No digital or hybrid projects were returned for this account. You can include Analogue / Tabletop projects from the filter bar above or refresh."));
			onboarding_clear_filters_btn->set_visible(kind_filter_option->get_selected_id() != KIND_FILTER_ALL_KINDS || !target_user_edit->get_text().strip_edges().is_empty());
		}
	}
}

void StudioProjectsPanel::refresh_projects() {
	if (studio_client.is_null()) {
		return;
	}
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->reload_settings();
	}
	if (!studio_client->is_configured() || !studio_client->has_credentials()) {
		_update_ui_state();
		return;
	}

	String target = target_user_edit->get_text().strip_edges();
	bool all_kinds = (kind_filter_option->get_selected_id() == KIND_FILTER_ALL_KINDS);
	bool inc_archived = include_archived_check->is_pressed();

	_set_transient_status(TTR("Fetching Studio projects from Studio Server..."), false);
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
	transient_status_message.clear();
	transient_status_is_error = false;

	String who = String(p_user.get("name", String()));
	if (who.is_empty()) {
		who = String(p_user.get("username", String()));
	}
	if (who.is_empty()) {
		who = String(p_user.get("email", String()));
	}
	if (!who.is_empty()) {
		projects_header_label->set_text(vformat(TTR("Projects — %s"), who));
	} else {
		projects_header_label->set_text(TTR("Studio Projects"));
	}

	_rebuild_project_list();
	_update_ui_state();
}

void StudioProjectsPanel::_on_config_loaded(const String &p_tool, const String &p_project_ref, const String &p_format, const String &p_raw_content, const Dictionary &p_parsed_config) {
	(void)p_parsed_config;
	if (pending_export_config_path.is_empty()) {
		_update_ui_state();
		return;
	}
	String target_path = pending_export_config_path;
	pending_export_config_path = String();

	Ref<FileAccess> f = FileAccess::open(target_path, FileAccess::WRITE);
	if (f.is_null()) {
		_set_transient_status(vformat(TTR("Failed to write configuration manifest to %s."), target_path), true);
		return;
	}
	f->store_string(p_raw_content);
	f->flush();

	String scope_label = p_project_ref.is_empty() ? TTR("studio-wide") : p_project_ref;
	_set_transient_status(vformat(TTR("Exported %s (%s, %s) manifest to %s."), p_tool, scope_label, p_format.to_upper(), target_path), false);
}

void StudioProjectsPanel::_on_request_failed(int p_http_code, const String &p_message) {
	(void)p_http_code;
	pending_export_config_path = String();
	_set_transient_status(p_message, true);
}

void StudioProjectsPanel::_on_auth_changed() {
	loaded_projects.clear();
	loaded_user.clear();
	initial_fetch_done = false;
	projects_header_label->set_text(TTR("Studio Projects"));
	_rebuild_project_list();
	_update_ui_state();
}

void StudioProjectsPanel::_on_auth_state_changed(int p_status) {
	(void)p_status;
	_update_ui_state();
}

void StudioProjectsPanel::_on_auth_url_ready(const String &p_url) {
	(void)p_url;
	_update_ui_state();
}

void StudioProjectsPanel::_on_auth_device_code(const String &p_user_code, const String &p_verify_uri, const String &p_verify_complete, int p_expires) {
	(void)p_user_code;
	(void)p_verify_uri;
	(void)p_verify_complete;
	(void)p_expires;
	_update_ui_state();
}

void StudioProjectsPanel::_on_auth_failed(int p_code, const String &p_message) {
	(void)p_code;
	if (!p_message.is_empty()) {
		_set_transient_status(p_message, true);
	} else {
		_update_ui_state();
	}
}

void StudioProjectsPanel::_on_auth_session_changed(const Ref<AuthentikSession> &p_session) {
	(void)p_session;
	transient_status_message.clear();
	transient_status_is_error = false;
	if (is_visible_in_tree() && studio_client.is_valid() && studio_client->is_configured()) {
		refresh_projects();
	} else {
		_update_ui_state();
	}
}

void StudioProjectsPanel::_rebuild_project_list() {
	if (!is_inside_tree()) {
		return;
	}

	String prev_selected_id;
	int cur_idx = _get_selected_project_data_index();
	if (cur_idx >= 0 && cur_idx < loaded_projects.size() && loaded_projects[cur_idx].get_type() == Variant::DICTIONARY) {
		prev_selected_id = String(Dictionary(loaded_projects[cur_idx]).get("id", String()));
	}

	// Clear existing card controls
	project_card_nodes.clear();
	while (projects_cards_vb->get_child_count() > 0) {
		Node *child = projects_cards_vb->get_child(0);
		projects_cards_vb->remove_child(child);
		memdelete(child);
	}
	filtered_project_indices.clear();
	selected_filtered_index = -1;
	hovered_filtered_index = -1;

	const String filter = search_edit->get_text().strip_edges().to_lower();
	const int kind_mode = kind_filter_option->get_selected_id();
	const int sort_mode = sort_option->get_selected_id();

	struct SortEntry {
		int data_idx = 0;
		bool has_godot = false;
		bool has_bound = false;
		int assigned_tasks = 0;
		String name_lower;
		String phase_lower;
	};
	Vector<SortEntry> entries;

	int total_local = 0;
	int total_assigned = 0;

	for (int i = 0; i < loaded_projects.size(); i++) {
		if (loaded_projects[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary proj = loaded_projects[i];
		String pid = String(proj.get("id", String()));
		String name = String(proj.get("name", pid));
		String kind_raw = String(proj.get("kind", "digital")).strip_edges().to_lower();
		String phase = _format_phase_label(String(proj.get("phase", String())));
		String role = String(proj.get("role", String()));
		String engine_raw = String(proj.get("engine", String())).strip_edges().to_lower();
		Dictionary lore = proj.get("lore", Dictionary());
		String slug = String(lore.get("projectSlug", proj.get("slug", String())));

		if (kind_mode == KIND_FILTER_AETHERION_ONLY) {
			bool is_aeth = engine_raw.is_empty() || engine_raw == "aetherion" || bool(proj.get("usesAetherion", false));
			if (!is_aeth || kind_raw == "analogue") {
				continue;
			}
		}

		if (!filter.is_empty()) {
			String haystack = (name + " " + slug + " " + engine_raw + " " + phase + " " + role + " " + kind_raw).to_lower();
			if (!haystack.contains(filter)) {
				continue;
			}
		}

		String bound_dir = _get_bound_local_dir(pid);
		bool has_bound = !bound_dir.is_empty() && DirAccess::exists(bound_dir);
		bool has_godot = has_bound && FileAccess::exists(bound_dir.path_join("project.godot"));
		int assigned = int(proj.get("assignedOpenTaskCount", 0));

		if (has_godot) {
			total_local++;
		}
		total_assigned += assigned;

		SortEntry se;
		se.data_idx = i;
		se.has_godot = has_godot;
		se.has_bound = has_bound;
		se.assigned_tasks = assigned;
		se.name_lower = name.to_lower();
		se.phase_lower = phase.to_lower();
		entries.push_back(se);
	}

	// Sort entries cleanly
	for (int i = 0; i < entries.size(); i++) {
		for (int j = i + 1; j < entries.size(); j++) {
			bool swap_needed = false;
			const SortEntry &a = entries[i];
			const SortEntry &b = entries[j];
			switch (sort_mode) {
				case SORT_LOCAL_FIRST: {
					if (a.has_godot != b.has_godot) {
						swap_needed = !a.has_godot && b.has_godot;
					} else if (a.has_bound != b.has_bound) {
						swap_needed = !a.has_bound && b.has_bound;
					} else if (a.assigned_tasks != b.assigned_tasks) {
						swap_needed = a.assigned_tasks < b.assigned_tasks;
					} else {
						swap_needed = a.name_lower > b.name_lower;
					}
				} break;
				case SORT_NAME_ASC: {
					swap_needed = a.name_lower > b.name_lower;
				} break;
				case SORT_ASSIGNED_TASKS: {
					if (a.assigned_tasks != b.assigned_tasks) {
						swap_needed = a.assigned_tasks < b.assigned_tasks;
					} else {
						swap_needed = a.name_lower > b.name_lower;
					}
				} break;
				case SORT_PHASE: {
					if (a.phase_lower != b.phase_lower) {
						swap_needed = a.phase_lower > b.phase_lower;
					} else {
						swap_needed = a.name_lower > b.name_lower;
					}
				} break;
			}
			if (swap_needed) {
				SWAP(entries.write[i], entries.write[j]);
			}
		}
	}

	// Update left column summary badge
	if (entries.is_empty()) {
		projects_stats_label->set_text(TTR("0 shown"));
	} else {
		String stats = vformat(TTR("%d shown"), entries.size());
		if (total_local > 0) {
			stats += vformat(TTR(" • %d local"), total_local);
		}
		if (total_assigned > 0) {
			stats += vformat(TTR(" • %d tasks"), total_assigned);
		}
		projects_stats_label->set_text(stats);
	}

	const Color base_col = get_theme_color(SNAME("base_color"), EditorStringName(Editor));
	const Color accent_col = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success_col = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color warn_col = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	const Color muted_col = get_theme_color(SNAME("font_placeholder_color"), EditorStringName(Editor));
	const Ref<Font> title_font = get_theme_font(SNAME("title"), EditorStringName(EditorFonts));
	const Ref<Font> bold_font = get_theme_font(SNAME("bold"), EditorStringName(EditorFonts));
	const Ref<Font> source_font = get_theme_font(SNAME("source"), EditorStringName(EditorFonts));
	const int title_size = get_theme_font_size(SNAME("title_size"), EditorStringName(EditorFonts));
	const int main_size = get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts));
	const int small_size = MAX(10, int(main_size * 0.85f));
	const int pill_size = MAX(9, int(main_size * 0.76f));
	const bool is_dark = base_col.get_luminance() < 0.5f;

	int restore_filtered_idx = -1;

	for (int list_idx = 0; list_idx < entries.size(); list_idx++) {
		int data_idx = entries[list_idx].data_idx;
		filtered_project_indices.push_back(data_idx);

		Dictionary proj = loaded_projects[data_idx];
		String pid = String(proj.get("id", String()));
		String name = String(proj.get("name", pid));
		String kind_raw = String(proj.get("kind", "digital")).strip_edges().to_lower();
		String engine_raw = String(proj.get("engine", String())).strip_edges();
		String engine_lbl = engine_raw.is_empty() ? (kind_raw == "analogue" ? String("Tabletop") : String("Aetherion")) : engine_raw.capitalize();
		String phase = _format_phase_label(String(proj.get("phase", String())));
		String role = String(proj.get("role", String())).capitalize();
		bool archived = bool(proj.get("archived", false));
		Dictionary lore = proj.get("lore", Dictionary());
		String slug = String(lore.get("projectSlug", proj.get("slug", String())));
		int assigned_tasks = entries[list_idx].assigned_tasks;
		String bound_dir = _get_bound_local_dir(pid);
		bool has_godot = entries[list_idx].has_godot;
		bool has_bound = entries[list_idx].has_bound;

		PanelContainer *card = memnew(PanelContainer);
		card->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		card->set_mouse_filter(Control::MOUSE_FILTER_STOP);
		card->set_focus_mode(Control::FOCUS_ALL);
		card->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
		card->set_tooltip_text(has_godot
						? vformat(TTR("%s\nDouble-click to open in Aetherion (%s)"), name, bound_dir)
						: vformat(TTR("%s\nClick to inspect repositories and workspace bindings"), name));
		card->connect("gui_input", callable_mp(this, &StudioProjectsPanel::_on_card_gui_input).bind(list_idx));
		card->connect("mouse_entered", callable_mp(this, &StudioProjectsPanel::_on_card_mouse_entered).bind(list_idx));
		card->connect("mouse_exited", callable_mp(this, &StudioProjectsPanel::_on_card_mouse_exited).bind(list_idx));

		HBoxContainer *card_hb = memnew(HBoxContainer);
		card_hb->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		card_hb->add_theme_constant_override("separation", int(10 * EDSCALE));
		card->add_child(card_hb);

		// Left icon badge
		PanelContainer *icon_badge = memnew(PanelContainer);
		icon_badge->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		icon_badge->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		icon_badge->set_custom_minimum_size(Size2(34 * EDSCALE, 34 * EDSCALE));
		Color badge_tint = has_godot ? success_col : (has_bound ? warn_col : accent_col);
		Ref<StyleBoxFlat> badge_sb = _make_card_style(
				Color(badge_tint.r, badge_tint.g, badge_tint.b, is_dark ? 0.14f : 0.12f),
				Color(badge_tint.r, badge_tint.g, badge_tint.b, 0.30f),
				int(6 * EDSCALE), 6 * EDSCALE, 6 * EDSCALE, 1);
		icon_badge->add_theme_style_override(SceneStringName(panel), badge_sb);
		card_hb->add_child(icon_badge);

		TextureRect *icon_tr = memnew(TextureRect);
		icon_tr->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		icon_tr->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
		icon_tr->set_texture(get_editor_theme_icon(has_godot ? SNAME("StatusSuccess") : (has_bound ? SNAME("Folder") : SNAME("VcsBranches"))));
		icon_badge->add_child(icon_tr);

		// Center details VBox
		VBoxContainer *text_vb = memnew(VBoxContainer);
		text_vb->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		text_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		text_vb->add_theme_constant_override("separation", int(3 * EDSCALE));
		card_hb->add_child(text_vb);

		// Row 1: Title + Status Pills
		HBoxContainer *row1_hb = memnew(HBoxContainer);
		row1_hb->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		row1_hb->add_theme_constant_override("separation", int(6 * EDSCALE));
		text_vb->add_child(row1_hb);

		Label *title_lbl = memnew(Label(name));
		title_lbl->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		title_lbl->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		title_lbl->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		title_lbl->add_theme_font_override(SceneStringName(font), title_font);
		title_lbl->add_theme_font_size_override(SceneStringName(font_size), MAX(main_size, int(title_size * 0.92f)));
		row1_hb->add_child(title_lbl);

		if (archived) {
			row1_hb->add_child(_make_pill_badge(TTR("ARCHIVED"), muted_col, bold_font, pill_size));
		}
		if (assigned_tasks > 0) {
			row1_hb->add_child(_make_pill_badge(vformat(TTR("%d task(s)"), assigned_tasks), accent_col, bold_font, pill_size));
		}
		if (has_godot) {
			row1_hb->add_child(_make_pill_badge(TTR("READY"), success_col, bold_font, pill_size));
		} else if (has_bound) {
			row1_hb->add_child(_make_pill_badge(TTR("FOLDER"), warn_col, bold_font, pill_size));
		}

		// Row 2: Subtitle (slug • Engine • Phase • Role)
		String sub_line;
		if (!slug.is_empty()) {
			sub_line += slug + "  •  ";
		}
		sub_line += engine_lbl + "  •  " + phase;
		if (!role.is_empty()) {
			sub_line += "  •  " + role;
		}

		Label *sub_lbl = memnew(Label(sub_line));
		sub_lbl->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		sub_lbl->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		sub_lbl->add_theme_font_size_override(SceneStringName(font_size), small_size);
		sub_lbl->add_theme_color_override(SceneStringName(font_color), muted_col);
		text_vb->add_child(sub_lbl);

		// Optional Row 3: Local bound path preview
		if (!bound_dir.is_empty()) {
			Label *path_lbl = memnew(Label(bound_dir));
			path_lbl->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			path_lbl->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
			path_lbl->add_theme_font_override(SceneStringName(font), source_font);
			path_lbl->add_theme_font_size_override(SceneStringName(font_size), MAX(9, int(small_size * 0.9f)));
			path_lbl->add_theme_color_override(SceneStringName(font_color), has_godot ? accent_col.darkened(0.08f) : muted_col);
			text_vb->add_child(path_lbl);
		}

		projects_cards_vb->add_child(card);
		project_card_nodes.push_back(card);

		if (!prev_selected_id.is_empty() && pid == prev_selected_id) {
			restore_filtered_idx = list_idx;
		}
	}

	if (restore_filtered_idx >= 0) {
		selected_filtered_index = restore_filtered_idx;
	} else if (!project_card_nodes.is_empty()) {
		selected_filtered_index = 0;
	} else {
		selected_filtered_index = -1;
	}

	_refresh_card_selection_styles();
	_update_detail_pane();
}

void StudioProjectsPanel::_refresh_card_selection_styles() {
	if (!is_inside_tree()) {
		return;
	}

	const Color base_col = get_theme_color(SNAME("base_color"), EditorStringName(Editor));
	const Color accent_col = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const bool is_dark = base_col.get_luminance() < 0.5f;

	const Color bg_normal = is_dark ? base_col.lightened(0.02f) : base_col.lightened(0.03f);
	const Color bg_hover = is_dark ? base_col.lightened(0.055f) : base_col.darkened(0.025f);
	const Color bg_selected = is_dark ? base_col.lightened(0.095f) : base_col.darkened(0.055f);
	const Color border_normal = is_dark ? base_col.lightened(0.10f) : base_col.darkened(0.10f);
	const Color border_hover = is_dark ? base_col.lightened(0.20f) : base_col.darkened(0.18f);
	const Color border_selected = Color(accent_col.r, accent_col.g, accent_col.b, 0.68f);

	const int border_px = MAX(1, int(Math::round(EDSCALE)));
	const int accent_bar_px = MAX(3, int(Math::round(3 * EDSCALE)));
	const int radius = int(6 * EDSCALE);

	for (int i = 0; i < project_card_nodes.size(); i++) {
		PanelContainer *card = project_card_nodes[i];
		if (!card) {
			continue;
		}
		const bool is_sel = (i == selected_filtered_index);
		const bool is_hov = (i == hovered_filtered_index);

		Ref<StyleBoxFlat> sb = _make_card_style(
				is_sel ? bg_selected : (is_hov ? bg_hover : bg_normal),
				is_sel ? border_selected : (is_hov ? border_hover : border_normal),
				radius, 11 * EDSCALE, 9 * EDSCALE, border_px);
		if (is_sel) {
			sb->set_border_width(SIDE_LEFT, accent_bar_px);
			sb->set_border_color(accent_col);
		}
		card->add_theme_style_override(SceneStringName(panel), sb);
	}
}

int StudioProjectsPanel::_get_selected_project_data_index() const {
	if (selected_filtered_index < 0 || selected_filtered_index >= filtered_project_indices.size()) {
		return -1;
	}
	return filtered_project_indices[selected_filtered_index];
}

void StudioProjectsPanel::_update_detail_pane() {
	if (!is_inside_tree()) {
		return;
	}

	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size() || loaded_projects[data_idx].get_type() != Variant::DICTIONARY) {
		empty_detail_label->set_visible(true);
		detail_content_vb->set_visible(false);
		return;
	}

	empty_detail_label->set_visible(false);
	detail_content_vb->set_visible(true);

	const Color accent_col = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success_col = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color warn_col = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	const Color error_col = get_theme_color(SNAME("error_color"), EditorStringName(Editor));
	const Color font_col = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	const Color muted_col = get_theme_color(SNAME("font_placeholder_color"), EditorStringName(Editor));
	const Ref<Font> bold_font = get_theme_font(SNAME("bold"), EditorStringName(EditorFonts));
	const int main_size = get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts));
	const int pill_size = MAX(9, int(main_size * 0.78f));

	Dictionary proj = loaded_projects[data_idx];
	String pid = String(proj.get("id", String()));
	String name = String(proj.get("name", pid));
	String kind_raw = String(proj.get("kind", "digital")).strip_edges().to_lower();
	String kind = kind_raw.capitalize();
	String engine_raw = String(proj.get("engine", String())).strip_edges();
	String engine_label = engine_raw.is_empty() ? (kind_raw == "analogue" ? String("Tabletop") : String("Aetherion")) : engine_raw.capitalize();
	String phase = _format_phase_label(String(proj.get("phase", String())));
	String role = String(proj.get("role", String())).capitalize();
	int open_tasks = int(proj.get("openTaskCount", 0));
	int assigned_tasks = int(proj.get("assignedOpenTaskCount", 0));
	String part_roles = _array_to_csv(proj.get("participantRoles", Array()));
	Dictionary lore = proj.get("lore", Dictionary());
	String lore_slug = String(lore.get("projectSlug", proj.get("slug", String())));
	String lore_vis = String(lore.get("visibility", "internal"));

	detail_title_label->set_text(name);
	if (!lore_slug.is_empty()) {
		detail_slug_label->set_text(lore_slug + "  •  " + lore_vis.to_upper());
		detail_slug_label->set_visible(true);
	} else {
		detail_slug_label->set_visible(false);
	}

	// Update KPI strip
	kpi_engine_value_lbl->set_text(engine_label + " (" + kind + ")");
	kpi_phase_value_lbl->set_text(phase);
	kpi_role_value_lbl->set_text(role.is_empty() ? TTR("Viewer") : role);
	kpi_tasks_value_lbl->set_text(vformat(TTR("%d assigned / %d open"), assigned_tasks, open_tasks));
	kpi_tasks_value_lbl->add_theme_color_override(SceneStringName(font_color), assigned_tasks > 0 ? accent_col : font_col);

	if (!part_roles.is_empty()) {
		detail_credits_label->set_text(vformat(TTR("Project Credits & Disciplines: %s"), part_roles));
		detail_credits_label->set_visible(true);
	} else {
		detail_credits_label->set_visible(false);
	}

	// Milestone & Sprint Banner
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
			sched_parts += vformat(TTR("Active Cycle: %s"), cyc_name);
		}
	}
	milestone_banner->set_visible(!sched_parts.is_empty());
	detail_milestone_label->set_text(sched_parts);

	// Local Workspace Card State
	String bound_dir = _get_bound_local_dir(pid);
	bool dir_exists = !bound_dir.is_empty() && DirAccess::exists(bound_dir);
	bool has_project_godot = dir_exists && FileAccess::exists(bound_dir.path_join("project.godot"));

	Color pill_col = muted_col;
	String pill_txt;
	if (bound_dir.is_empty()) {
		pill_col = muted_col;
		pill_txt = TTR("NOT LINKED");
		local_status_icon->set_texture(get_editor_theme_icon(SNAME("Folder")));
		detail_local_path_label->set_text(TTR("No local folder bound yet. Bind an existing checkout or clone a repository below."));
		detail_local_path_label->add_theme_color_override(SceneStringName(font_color), muted_col);
	} else if (has_project_godot) {
		pill_col = success_col;
		pill_txt = TTR("READY TO EDIT");
		local_status_icon->set_texture(get_editor_theme_icon(SNAME("StatusSuccess")));
		detail_local_path_label->set_text(bound_dir);
		detail_local_path_label->add_theme_color_override(SceneStringName(font_color), font_col);
	} else if (dir_exists) {
		pill_col = warn_col;
		pill_txt = TTR("NO PROJECT.GODOT");
		local_status_icon->set_texture(get_editor_theme_icon(SNAME("StatusWarning")));
		detail_local_path_label->set_text(vformat(TTR("%s  (no project.godot found at root)"), bound_dir));
		detail_local_path_label->add_theme_color_override(SceneStringName(font_color), warn_col);
	} else {
		pill_col = error_col;
		pill_txt = TTR("MISSING ON DISK");
		local_status_icon->set_texture(get_editor_theme_icon(SNAME("StatusError")));
		detail_local_path_label->set_text(vformat(TTR("%s  (directory not found on disk)"), bound_dir));
		detail_local_path_label->add_theme_color_override(SceneStringName(font_color), error_col);
	}

	Ref<StyleBoxFlat> status_pill_sb = _make_card_style(
			Color(pill_col.r, pill_col.g, pill_col.b, 0.16f),
			Color(pill_col.r, pill_col.g, pill_col.b, 0.45f),
			int(4 * EDSCALE), 7 * EDSCALE, 2 * EDSCALE, 1);
	local_status_pill->add_theme_style_override(SceneStringName(panel), status_pill_sb);
	local_status_pill_label->set_text(pill_txt);
	local_status_pill_label->add_theme_font_override(SceneStringName(font), bold_font);
	local_status_pill_label->add_theme_font_size_override(SceneStringName(font_size), pill_size);
	local_status_pill_label->add_theme_color_override(SceneStringName(font_color), pill_col.lightened(0.12f));

	copy_local_path_button->set_visible(!bound_dir.is_empty());
	open_in_editor_button->set_disabled(!has_project_godot);
	run_project_button->set_disabled(!has_project_godot);
	bind_folder_button->set_text(bound_dir.is_empty() ? TTR("Bind Local Folder...") : TTR("Change Folder..."));
	show_in_fm_button->set_disabled(!dir_exists);
	unbind_folder_button->set_visible(!bound_dir.is_empty());

	open_intranet_button->set_disabled(String(proj.get("intranetUrl", String())).is_empty());
	if (export_project_config_button) {
		bool can_export = studio_client.is_valid() && studio_client->is_configured() && studio_client->has_credentials() && !studio_client->is_busy() && active_clone_pid == 0;
		export_project_config_button->set_disabled(!can_export);
	}

	// Lore VCS & Git Repositories Card
	String lore_actions = _array_to_csv(lore.get("actions", Array()));
	bool can_create_repo = bool(lore.get("canCreateRepo", false));
	if (!lore_slug.is_empty()) {
		lore_summary_label->set_text(vformat(
				TTR("Lore Namespace: %s/*   •   Visibility: %s   •   Granted Actions: %s%s"),
				lore_slug,
				lore_vis,
				lore_actions.is_empty() ? TTR("none") : lore_actions,
				can_create_repo ? TTR(" (can create repos)") : String()));
		lore_summary_label->set_visible(true);
	} else {
		lore_summary_label->set_visible(false);
	}

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
		if (!r_lore_full.is_empty() && r_lore_full != r_name) {
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
		String remote_uri = String(r.get("remoteUri", String()));

		item->set_text(0, r_name);
		item->set_icon(0, get_editor_theme_icon(SNAME("VcsBranches")));
		item->set_text(1, r_sys);
		if (r_sys == "LORE") {
			item->set_custom_color(1, accent_col);
		}
		item->set_text(2, can_write ? TTR("Read / Write") : TTR("Read-Only"));
		item->set_custom_color(2, can_write ? success_col : muted_col);
		item->set_text(3, ws_locks);
		if (lock_count > 0) {
			item->set_custom_color(3, warn_col);
		}
		item->set_text(4, remote_uri);
		item->set_tooltip_text(4, remote_uri);
		item->set_metadata(0, r);
		if (!first_item) {
			first_item = item;
		}
	}

	const bool has_repos = (first_item != nullptr);
	repos_tree->set_visible(has_repos);
	empty_repos_label->set_visible(!has_repos);

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
	String remote = String(repo.get("remoteUri", String())).strip_edges();
	bool can_clone = !cmd.is_empty() && active_clone_pid == 0;
	clone_repo_button->set_disabled(!can_clone);
	copy_clone_cmd_button->set_disabled(cmd.is_empty());
	copy_remote_uri_button->set_disabled(remote.is_empty());
}

void StudioProjectsPanel::_on_card_gui_input(const Ref<InputEvent> &p_event, int p_filtered_idx) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		_on_card_clicked(p_filtered_idx);
		if (mb->is_double_click()) {
			_on_card_activated(p_filtered_idx);
		}
		accept_event();
		return;
	}
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed() && !k->is_echo()) {
		if (k->get_keycode() == Key::ENTER || k->get_keycode() == Key::KP_ENTER || k->get_keycode() == Key::SPACE) {
			_on_card_clicked(p_filtered_idx);
			if (k->get_keycode() != Key::SPACE) {
				_on_card_activated(p_filtered_idx);
			}
			accept_event();
		} else if (k->get_keycode() == Key::UP && p_filtered_idx > 0) {
			_on_card_clicked(p_filtered_idx - 1);
			if (p_filtered_idx - 1 < project_card_nodes.size()) {
				project_card_nodes[p_filtered_idx - 1]->grab_focus();
			}
			accept_event();
		} else if (k->get_keycode() == Key::DOWN && p_filtered_idx + 1 < project_card_nodes.size()) {
			_on_card_clicked(p_filtered_idx + 1);
			project_card_nodes[p_filtered_idx + 1]->grab_focus();
			accept_event();
		}
	}
}

void StudioProjectsPanel::_on_card_mouse_entered(int p_filtered_idx) {
	hovered_filtered_index = p_filtered_idx;
	_refresh_card_selection_styles();
}

void StudioProjectsPanel::_on_card_mouse_exited(int p_filtered_idx) {
	if (hovered_filtered_index == p_filtered_idx) {
		hovered_filtered_index = -1;
		_refresh_card_selection_styles();
	}
}

void StudioProjectsPanel::_on_card_clicked(int p_filtered_idx) {
	if (p_filtered_idx < 0 || p_filtered_idx >= filtered_project_indices.size()) {
		return;
	}
	if (selected_filtered_index == p_filtered_idx) {
		return;
	}
	selected_filtered_index = p_filtered_idx;
	_refresh_card_selection_styles();
	_update_detail_pane();
}

void StudioProjectsPanel::_on_card_activated(int p_filtered_idx) {
	selected_filtered_index = p_filtered_idx;
	_refresh_card_selection_styles();
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

void StudioProjectsPanel::_on_login_browser_pressed() {
	transient_status_message.clear();
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->login_interactive(true);
		_update_ui_state();
	}
}

void StudioProjectsPanel::_on_login_device_pressed() {
	transient_status_message.clear();
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->login_device_code(false);
		_update_ui_state();
	}
}

void StudioProjectsPanel::_on_cancel_login_pressed() {
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->cancel_login();
		_update_ui_state();
	}
}

void StudioProjectsPanel::_on_copy_login_url_pressed() {
	if (!Authentik::get_singleton() || Authentik::get_singleton()->get_client().is_null() || !DisplayServer::get_singleton()) {
		return;
	}
	String url = Authentik::get_singleton()->get_client()->get_last_authorization_url();
	if (!url.is_empty()) {
		DisplayServer::get_singleton()->clipboard_set(url);
		_set_transient_status(TTR("Sign-in URL copied to clipboard."), false);
	}
}

void StudioProjectsPanel::_on_copy_device_code_pressed() {
	if (!Authentik::get_singleton() || Authentik::get_singleton()->get_client().is_null() || !DisplayServer::get_singleton()) {
		return;
	}
	String code = Authentik::get_singleton()->get_client()->get_user_code();
	if (!code.is_empty()) {
		DisplayServer::get_singleton()->clipboard_set(code);
		_set_transient_status(TTR("Verification code copied to clipboard."), false);
	}
}

void StudioProjectsPanel::_on_open_device_verify_pressed() {
	if (!Authentik::get_singleton() || Authentik::get_singleton()->get_client().is_null() || !OS::get_singleton()) {
		return;
	}
	Ref<AuthentikClient> client = Authentik::get_singleton()->get_client();
	String target = client->get_verification_uri_complete().is_empty()
			? client->get_verification_uri()
			: client->get_verification_uri_complete();
	if (!target.is_empty()) {
		OS::get_singleton()->shell_open(target);
	}
}

void StudioProjectsPanel::_on_clear_filters_pressed() {
	search_edit->clear();
	target_user_edit->clear();
	bool need_server_refetch = (kind_filter_option->get_selected_id() != KIND_FILTER_ALL_KINDS && loaded_projects.is_empty());
	if (need_server_refetch) {
		kind_filter_option->select(kind_filter_option->get_item_index(KIND_FILTER_ALL_KINDS));
		refresh_projects();
	} else {
		if (kind_filter_option->get_selected_id() == KIND_FILTER_AETHERION_ONLY) {
			kind_filter_option->select(kind_filter_option->get_item_index(KIND_FILTER_DIGITAL_HYBRID));
		}
		_rebuild_project_list();
		_update_ui_state();
	}
}

void StudioProjectsPanel::_on_search_changed(const String &p_text) {
	(void)p_text;
	_rebuild_project_list();
	_update_ui_state();
}

void StudioProjectsPanel::_on_target_user_submitted(const String &p_text) {
	(void)p_text;
	refresh_projects();
}

void StudioProjectsPanel::_on_kind_filter_changed(int p_index) {
	(void)p_index;
	int mode = kind_filter_option->get_selected_id();
	if (mode == KIND_FILTER_ALL_KINDS || mode == KIND_FILTER_DIGITAL_HYBRID) {
		refresh_projects();
	} else {
		_rebuild_project_list();
		_update_ui_state();
	}
}

void StudioProjectsPanel::_on_sort_changed(int p_index) {
	(void)p_index;
	_rebuild_project_list();
}

void StudioProjectsPanel::_on_filter_toggled(bool p_pressed) {
	(void)p_pressed;
	refresh_projects();
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

void StudioProjectsPanel::_on_run_project_pressed() {
	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size() || !OS::get_singleton()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	String bound_dir = _get_bound_local_dir(String(proj.get("id", String())));
	if (bound_dir.is_empty() || !FileAccess::exists(bound_dir.path_join("project.godot"))) {
		return;
	}
	List<String> args;
	args.push_back("--path");
	args.push_back(bound_dir);
	Error err = OS::get_singleton()->create_instance(args);
	if (err != OK) {
		_set_transient_status(TTR("Failed to launch bound project instance."), true);
	} else {
		_set_transient_status(vformat(TTR("Launched %s."), String(proj.get("name", bound_dir))), false);
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
		_set_transient_status(vformat(TTR("Linked local Aetherion project: %s"), resolved), false);
	} else {
		_set_transient_status(vformat(TTR("Linked folder %s (no project.godot at root)."), resolved), false);
	}
	_rebuild_project_list();
}

void StudioProjectsPanel::_on_unbind_folder_pressed() {
	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	String pid = String(proj.get("id", String()));
	if (local_bindings.has(pid)) {
		local_bindings.erase(pid);
		_save_local_bindings();
		_rebuild_project_list();
		_set_transient_status(TTR("Local folder binding removed."), false);
	}
}

void StudioProjectsPanel::_on_copy_local_path_pressed() {
	int data_idx = _get_selected_project_data_index();
	if (data_idx < 0 || data_idx >= loaded_projects.size() || !DisplayServer::get_singleton()) {
		return;
	}
	Dictionary proj = loaded_projects[data_idx];
	String bound_dir = _get_bound_local_dir(String(proj.get("id", String())));
	if (!bound_dir.is_empty()) {
		DisplayServer::get_singleton()->clipboard_set(bound_dir);
		_set_transient_status(vformat(TTR("Copied workspace path: %s"), bound_dir), false);
	}
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

void StudioProjectsPanel::_on_import_studio_config_pressed() {
	if (!import_config_dialog) {
		return;
	}
	if (EditorSettings::get_singleton()) {
		String def_dir = EDITOR_GET("filesystem/directories/default_project_path");
		if (!def_dir.is_empty() && DirAccess::exists(def_dir)) {
			import_config_dialog->set_current_dir(def_dir);
		}
	}
	import_config_dialog->popup_file_dialog();
}

void StudioProjectsPanel::_on_import_config_file_selected(const String &p_path) {
	Error file_err = OK;
	String content = FileAccess::get_file_as_string(p_path, &file_err);
	if (file_err != OK || content.is_empty()) {
		_set_transient_status(TTR("Could not read the selected JSON configuration file."), true);
		return;
	}

	Dictionary parsed_cfg;
	Dictionary editor_cfg;
	String parse_err;
	if (StudioApiClient::parse_studio_config_json(content, parsed_cfg, editor_cfg, parse_err) != OK) {
		_set_transient_status(parse_err.is_empty() ? TTR("Invalid Studio configuration JSON file.") : parse_err, true);
		return;
	}

	if (!editor_cfg.has("network/authentik/issuer_url") &&
			!editor_cfg.has("network/authentik/studio_api_url") &&
			!editor_cfg.has("network/authentik/client_id")) {
		_set_transient_status(TTR("Selected JSON file does not contain Authentik or Studio Server settings."), true);
		return;
	}

	EditorSettings *es = EditorSettings::get_singleton();
	if (es) {
		Array keys = editor_cfg.keys();
		for (int i = 0; i < keys.size(); i++) {
			String k = String(keys[i]);
			es->set_setting(k, editor_cfg[k]);
		}
		es->save();
	}

	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->reload_settings();
	}

	String proj_name;
	if (parsed_cfg.get("project", Variant()).get_type() == Variant::DICTIONARY) {
		Dictionary p = parsed_cfg["project"];
		proj_name = String(p.get("name", String())).strip_edges();
	}

	if (studio_client.is_valid() && studio_client->is_configured() && studio_client->has_credentials()) {
		refresh_projects();
	} else {
		String msg = !proj_name.is_empty()
				? vformat(TTR("Imported Studio configuration from '%s' (%s). Ready to sign in."), p_path.get_file(), proj_name)
				: vformat(TTR("Imported Studio configuration from '%s'. Ready to sign in."), p_path.get_file());
		_set_transient_status(msg, false);
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

	_set_transient_status(TTR("Generating configuration manifest from Studio Server..."), false);
	Error err = pending_export_project_ref.is_empty()
			? studio_client->fetch_studio_config("aetherion", fmt)
			: studio_client->fetch_project_config(pending_export_project_ref, "aetherion", fmt);
	if (err != OK) {
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
	_set_transient_status(vformat(TTR("Copied clone command: %s"), joined), false);
}

void StudioProjectsPanel::_on_copy_remote_uri_pressed() {
	Dictionary repo = _get_selected_repo_dict();
	String remote = String(repo.get("remoteUri", String())).strip_edges();
	if (remote.is_empty() || !DisplayServer::get_singleton()) {
		return;
	}
	DisplayServer::get_singleton()->clipboard_set(remote);
	_set_transient_status(vformat(TTR("Copied remote URI: %s"), remote), false);
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

	ProcessID pid = 0;
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
	_set_transient_status(vformat(TTR("Cloning %s into %s..."), remote, target_dir), false);
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
		_set_transient_status(vformat(TTR("Repository cloned successfully to %s."), resolved), false);
		_rebuild_project_list();
	} else {
		_set_transient_status(vformat(TTR("Repository clone exited with code %d."), exit_code), true);
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
