/**************************************************************************/
/*  authentik_editor_plugin.cpp                                           */
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

#include "authentik_editor_plugin.h"

#ifdef TOOLS_ENABLED

#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/translation_server.h"
#include "editor/editor_string_names.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/timer.h"
#include "servers/display/display_server.h"

static String _join_string_array(const PackedStringArray &p_values) {
	if (p_values.is_empty()) {
		return "-";
	}
	String out;
	for (int i = 0; i < p_values.size(); i++) {
		if (i > 0) {
			out += ", ";
		}
		out += p_values[i];
	}
	return out;
}

static String _format_duration_seconds(int64_t p_seconds) {
	if (p_seconds <= 0) {
		return TTR("Expired");
	}
	int64_t hours = p_seconds / 3600;
	int64_t minutes = (p_seconds % 3600) / 60;
	int64_t secs = p_seconds % 60;
	if (hours > 0) {
		return vformat("%dh %02dm %02ds", hours, minutes, secs);
	}
	if (minutes > 0) {
		return vformat("%dm %02ds", minutes, secs);
	}
	return vformat("%ds", secs);
}

// ============================================================================
// AuthentikAccountDialog
// ============================================================================

AuthentikAccountDialog::AuthentikAccountDialog() {
	set_title(TTR("Authentik Account"));
	set_ok_button_text(TTR("Close"));
	set_min_size(Size2(540, 0) * EDSCALE);

	VBoxContainer *root_vb = memnew(VBoxContainer);
	root_vb->add_theme_constant_override("separation", int(10 * EDSCALE));
	add_child(root_vb);

	env_override_label = memnew(Label);
	env_override_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	env_override_label->set_text(TTR("Active environment variables (AETHERION_AUTHENTIK_* / AUTHENTIK_* / OIDC_*) are overriding Editor Settings."));
	env_override_label->set_visible(false);
	root_vb->add_child(env_override_label);

	// Signed-in summary panel.
	signed_in_panel = memnew(PanelContainer);
	signed_in_panel->set_visible(false);
	root_vb->add_child(signed_in_panel);

	VBoxContainer *signed_in_vb = memnew(VBoxContainer);
	signed_in_vb->add_theme_constant_override("separation", int(8 * EDSCALE));
	signed_in_panel->add_child(signed_in_vb);

	HBoxContainer *profile_hb = memnew(HBoxContainer);
	profile_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	signed_in_vb->add_child(profile_hb);

	signed_in_icon = memnew(TextureRect);
	signed_in_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	profile_hb->add_child(signed_in_icon);

	VBoxContainer *identity_vb = memnew(VBoxContainer);
	identity_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	profile_hb->add_child(identity_vb);

	account_name_label = memnew(Label);
	identity_vb->add_child(account_name_label);

	account_email_label = memnew(Label);
	identity_vb->add_child(account_email_label);

	GridContainer *claims_grid = memnew(GridContainer);
	claims_grid->set_columns(2);
	claims_grid->add_theme_constant_override("h_separation", int(12 * EDSCALE));
	claims_grid->add_theme_constant_override("v_separation", int(4 * EDSCALE));
	signed_in_vb->add_child(claims_grid);

	claims_grid->add_child(memnew(Label(TTR("Subject:"))));
	subject_value_label = memnew(Label("-"));
	subject_value_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	subject_value_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	claims_grid->add_child(subject_value_label);

	claims_grid->add_child(memnew(Label(TTR("Groups:"))));
	groups_value_label = memnew(Label("-"));
	groups_value_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	groups_value_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	claims_grid->add_child(groups_value_label);

	claims_grid->add_child(memnew(Label(TTR("Entitlements:"))));
	entitlements_value_label = memnew(Label("-"));
	entitlements_value_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	entitlements_value_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	claims_grid->add_child(entitlements_value_label);

	claims_grid->add_child(memnew(Label(TTR("Token Expiry:"))));
	expiry_value_label = memnew(Label("-"));
	expiry_value_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	claims_grid->add_child(expiry_value_label);

	HBoxContainer *signed_in_actions = memnew(HBoxContainer);
	signed_in_actions->add_theme_constant_override("separation", int(8 * EDSCALE));
	signed_in_vb->add_child(signed_in_actions);

	refresh_token_button = memnew(Button(TTR("Refresh Token")));
	refresh_token_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_refresh_token_pressed));
	signed_in_actions->add_child(refresh_token_button);

	refresh_profile_button = memnew(Button(TTR("Refresh Profile")));
	refresh_profile_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_refresh_profile_pressed));
	signed_in_actions->add_child(refresh_profile_button);

	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	signed_in_actions->add_child(spacer);

	sign_out_button = memnew(Button(TTR("Sign Out")));
	sign_out_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_sign_out_pressed));
	signed_in_actions->add_child(sign_out_button);

	// Signed-out / active login actions.
	signed_out_vbox = memnew(VBoxContainer);
	signed_out_vbox->add_theme_constant_override("separation", int(8 * EDSCALE));
	root_vb->add_child(signed_out_vbox);

	HBoxContainer *login_buttons_hb = memnew(HBoxContainer);
	login_buttons_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	signed_out_vbox->add_child(login_buttons_hb);

	login_browser_button = memnew(Button(TTR("Sign In with Browser")));
	login_browser_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	login_browser_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_login_browser_pressed));
	login_buttons_hb->add_child(login_browser_button);

	login_device_button = memnew(Button(TTR("Sign In with Device Code")));
	login_device_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	login_device_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_login_device_pressed));
	login_buttons_hb->add_child(login_device_button);

	cancel_login_button = memnew(Button(TTR("Cancel")));
	cancel_login_button->set_visible(false);
	cancel_login_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_cancel_login_pressed));
	login_buttons_hb->add_child(cancel_login_button);

	copy_login_url_button = memnew(Button(TTR("Copy Browser Sign-In URL")));
	copy_login_url_button->set_visible(false);
	copy_login_url_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_copy_login_url_pressed));
	signed_out_vbox->add_child(copy_login_url_button);

	device_code_panel = memnew(PanelContainer);
	device_code_panel->set_visible(false);
	signed_out_vbox->add_child(device_code_panel);

	VBoxContainer *device_vb = memnew(VBoxContainer);
	device_vb->add_theme_constant_override("separation", int(6 * EDSCALE));
	device_code_panel->add_child(device_vb);

	Label *device_intro = memnew(Label(TTR("Enter this verification code in your browser:")));
	device_vb->add_child(device_intro);

	device_code_value_label = memnew(Label);
	device_code_value_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	device_vb->add_child(device_code_value_label);

	device_verify_url_label = memnew(Label);
	device_verify_url_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	device_verify_url_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	device_vb->add_child(device_verify_url_label);

	HBoxContainer *device_actions = memnew(HBoxContainer);
	device_actions->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	device_actions->add_theme_constant_override("separation", int(8 * EDSCALE));
	device_vb->add_child(device_actions);

	copy_device_code_button = memnew(Button(TTR("Copy Code")));
	copy_device_code_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_copy_device_code_pressed));
	device_actions->add_child(copy_device_code_button);

	open_device_verify_button = memnew(Button(TTR("Open Verification Page")));
	open_device_verify_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_open_device_verify_pressed));
	device_actions->add_child(open_device_verify_button);

	feedback_label = memnew(Label);
	feedback_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	feedback_label->set_visible(false);
	root_vb->add_child(feedback_label);

	root_vb->add_child(memnew(HSeparator));

	// Provider configuration section.
	Label *config_header = memnew(Label(TTR("OIDC / Authentik Provider Settings")));
	root_vb->add_child(config_header);

	GridContainer *config_grid = memnew(GridContainer);
	config_grid->set_columns(2);
	config_grid->add_theme_constant_override("h_separation", int(10 * EDSCALE));
	config_grid->add_theme_constant_override("v_separation", int(6 * EDSCALE));
	root_vb->add_child(config_grid);

	config_grid->add_child(memnew(Label(TTR("Issuer URL:"))));
	issuer_url_edit = memnew(LineEdit);
	issuer_url_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	issuer_url_edit->set_placeholder("https://auth.example.com/application/o/example-app/");
	config_grid->add_child(issuer_url_edit);

	config_grid->add_child(memnew(Label(TTR("Client ID:"))));
	client_id_edit = memnew(LineEdit);
	client_id_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	client_id_edit->set_placeholder("public-client-id");
	config_grid->add_child(client_id_edit);

	config_grid->add_child(memnew(Label(TTR("Scopes:"))));
	scopes_edit = memnew(LineEdit);
	scopes_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scopes_edit->set_placeholder("openid profile email offline_access");
	config_grid->add_child(scopes_edit);

	config_grid->add_child(memnew(Label(TTR("Studio API URL:"))));
	studio_api_url_edit = memnew(LineEdit);
	studio_api_url_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	studio_api_url_edit->set_placeholder("https://studio.example.com");
	studio_api_url_edit->set_tooltip_text(TTR("Base URL of the Studio Server used by the Project Manager Studio tab."));
	config_grid->add_child(studio_api_url_edit);

	config_grid->add_child(memnew(Label(TTR("Loopback Port:"))));
	redirect_port_spin = memnew(SpinBox);
	redirect_port_spin->set_min(0);
	redirect_port_spin->set_max(65535);
	redirect_port_spin->set_step(1);
	redirect_port_spin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	redirect_port_spin->set_tooltip_text(TTR("0 selects an available ephemeral port on 127.0.0.1."));
	config_grid->add_child(redirect_port_spin);

	config_grid->add_child(memnew(Label(TTR("Custom Redirect URI:"))));
	redirect_uri_edit = memnew(LineEdit);
	redirect_uri_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	redirect_uri_edit->set_placeholder(TTR("Optional (leave empty for automatic 127.0.0.1 callback)"));
	config_grid->add_child(redirect_uri_edit);

	persist_session_check = memnew(CheckBox);
	persist_session_check->set_text(TTR("Remember session on this machine (AES-256 encrypted)"));
	root_vb->add_child(persist_session_check);

	auto_refresh_check = memnew(CheckBox);
	auto_refresh_check->set_text(TTR("Automatically refresh expiring access tokens"));
	root_vb->add_child(auto_refresh_check);

	HBoxContainer *save_hb = memnew(HBoxContainer);
	save_hb->add_theme_constant_override("separation", int(8 * EDSCALE));
	save_hb->set_alignment(BoxContainer::ALIGNMENT_END);
	root_vb->add_child(save_hb);

	import_config_button = memnew(Button(TTR("Import Studio Config (.json)...")));
	import_config_button->set_tooltip_text(TTR("Import Authentik & Studio Server settings from a JSON configuration manifest exported by the Studio Server."));
	import_config_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_import_config_pressed));
	save_hb->add_child(import_config_button);

	save_settings_button = memnew(Button(TTR("Save Provider Settings")));
	save_settings_button->connect(SceneStringName(pressed), callable_mp(this, &AuthentikAccountDialog::_on_save_settings_pressed));
	save_hb->add_child(save_settings_button);

	import_config_dialog = memnew(EditorFileDialog);
	import_config_dialog->set_previews_enabled(false);
	import_config_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	import_config_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	import_config_dialog->set_title(TTR("Import Studio Configuration Manifest (.json)"));
	import_config_dialog->add_filter("*.json", TTR("Studio Configuration Manifest"));
	import_config_dialog->connect("file_selected", callable_mp(this, &AuthentikAccountDialog::_on_import_config_file_selected));
	add_child(import_config_dialog);

	countdown_timer = memnew(Timer);
	countdown_timer->set_wait_time(1.0);
	countdown_timer->set_one_shot(false);
	countdown_timer->connect("timeout", callable_mp(this, &AuthentikAccountDialog::_update_expiry_label));
	add_child(countdown_timer);
}

void AuthentikAccountDialog::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			Authentik::ensure_editor_settings_registered();
			_connect_authentik_signals();
			_load_fields_from_settings();
			_refresh_ui();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_disconnect_authentik_signals();
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			_on_visibility_changed();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			signed_in_icon->set_texture(get_editor_theme_icon(SNAME("StatusSuccess")));
			login_browser_button->set_button_icon(get_editor_theme_icon(SNAME("ExternalLink")));
			login_device_button->set_button_icon(get_editor_theme_icon(SNAME("Key")));
			copy_login_url_button->set_button_icon(get_editor_theme_icon(SNAME("ActionCopy")));
			copy_device_code_button->set_button_icon(get_editor_theme_icon(SNAME("ActionCopy")));
			open_device_verify_button->set_button_icon(get_editor_theme_icon(SNAME("ExternalLink")));
			refresh_token_button->set_button_icon(get_editor_theme_icon(SNAME("Reload")));
			import_config_button->set_button_icon(get_editor_theme_icon(SNAME("Load")));

			const Ref<Font> source_font = get_theme_font(SNAME("source"), EditorStringName(EditorFonts));
			const int source_size = get_theme_font_size(SNAME("source_size"), EditorStringName(EditorFonts));
			device_code_value_label->add_theme_font_override("font", source_font);
			device_code_value_label->add_theme_font_size_override("font_size", int(source_size * 1.4));
			env_override_label->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
			_refresh_ui();
		} break;
	}
}

void AuthentikAccountDialog::_connect_authentik_signals() {
	if (signals_connected || !Authentik::get_singleton()) {
		return;
	}
	Authentik *auth = Authentik::get_singleton();
	auth->connect("authenticated", callable_mp(this, &AuthentikAccountDialog::_on_authenticated));
	auth->connect("authentication_failed", callable_mp(this, &AuthentikAccountDialog::_on_authentication_failed));
	auth->connect("logged_out", callable_mp(this, &AuthentikAccountDialog::_on_logged_out));
	auth->connect("session_refreshed", callable_mp(this, &AuthentikAccountDialog::_on_session_refreshed));
	auth->connect("authorization_url_ready", callable_mp(this, &AuthentikAccountDialog::_on_authorization_url_ready));
	auth->connect("device_code_received", callable_mp(this, &AuthentikAccountDialog::_on_device_code_received));
	auth->connect("state_changed", callable_mp(this, &AuthentikAccountDialog::_on_state_changed));
	signals_connected = true;
}

void AuthentikAccountDialog::_disconnect_authentik_signals() {
	if (!signals_connected || !Authentik::get_singleton()) {
		signals_connected = false;
		return;
	}
	Authentik *auth = Authentik::get_singleton();
	auth->disconnect("authenticated", callable_mp(this, &AuthentikAccountDialog::_on_authenticated));
	auth->disconnect("authentication_failed", callable_mp(this, &AuthentikAccountDialog::_on_authentication_failed));
	auth->disconnect("logged_out", callable_mp(this, &AuthentikAccountDialog::_on_logged_out));
	auth->disconnect("session_refreshed", callable_mp(this, &AuthentikAccountDialog::_on_session_refreshed));
	auth->disconnect("authorization_url_ready", callable_mp(this, &AuthentikAccountDialog::_on_authorization_url_ready));
	auth->disconnect("device_code_received", callable_mp(this, &AuthentikAccountDialog::_on_device_code_received));
	auth->disconnect("state_changed", callable_mp(this, &AuthentikAccountDialog::_on_state_changed));
	signals_connected = false;
}

void AuthentikAccountDialog::open_dialog() {
	transient_notice.clear();
	transient_notice_is_error = false;
	Authentik::ensure_editor_settings_registered();
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->reload_settings();
	}
	_load_fields_from_settings();
	_refresh_ui();
	popup_centered();
}

void AuthentikAccountDialog::_on_visibility_changed() {
	if (!countdown_timer) {
		return;
	}
	if (is_visible()) {
		countdown_timer->start();
		_update_expiry_label();
	} else {
		countdown_timer->stop();
	}
}

void AuthentikAccountDialog::_load_fields_from_settings() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}
	Authentik::ensure_editor_settings_registered();
	issuer_url_edit->set_text(String(es->get_setting("network/authentik/issuer_url")));
	client_id_edit->set_text(String(es->get_setting("network/authentik/client_id")));
	scopes_edit->set_text(String(es->get_setting("network/authentik/scopes")));
	studio_api_url_edit->set_text(String(es->get_setting("network/authentik/studio_api_url")));
	redirect_port_spin->set_value_no_signal(double(int(es->get_setting("network/authentik/redirect_port"))));
	redirect_uri_edit->set_text(String(es->get_setting("network/authentik/redirect_uri")));
	persist_session_check->set_pressed_no_signal(bool(es->get_setting("network/authentik/persist_session")));
	auto_refresh_check->set_pressed_no_signal(bool(es->get_setting("network/authentik/auto_refresh")));
}

void AuthentikAccountDialog::_save_fields_to_settings() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}
	Authentik::ensure_editor_settings_registered();
	String scopes_val = scopes_edit->get_text().strip_edges();
	if (scopes_val.is_empty()) {
		scopes_val = "openid profile email offline_access";
		scopes_edit->set_text(scopes_val);
	}
	es->set_setting("network/authentik/issuer_url", issuer_url_edit->get_text().strip_edges());
	es->set_setting("network/authentik/client_id", client_id_edit->get_text().strip_edges());
	es->set_setting("network/authentik/scopes", scopes_val);
	es->set_setting("network/authentik/studio_api_url", StudioApiClient::clean_api_url_string(studio_api_url_edit->get_text()));
	es->set_setting("network/authentik/redirect_port", int(redirect_port_spin->get_value()));
	es->set_setting("network/authentik/redirect_uri", redirect_uri_edit->get_text().strip_edges());
	es->set_setting("network/authentik/persist_session", persist_session_check->is_pressed());
	es->set_setting("network/authentik/auto_refresh", auto_refresh_check->is_pressed());
	EditorSettings::save();
	es->notify_changes();

	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->reload_settings();
	}
}

void AuthentikAccountDialog::_refresh_ui() {
	Authentik *auth = Authentik::get_singleton();
	if (!auth) {
		return;
	}

	env_override_label->set_visible(auth->has_environment_override());

	const bool authenticated = auth->is_authenticated();
	const bool busy = auth->is_busy();
	const AuthentikClient::Status st = auth->get_status();
	Ref<AuthentikClient> client = auth->get_client();

	signed_in_panel->set_visible(authenticated);
	signed_out_vbox->set_visible(!authenticated);

	if (authenticated) {
		Ref<AuthentikSession> session = auth->get_session();
		if (session.is_valid()) {
			String primary = session->get_display_name();
			if (primary.is_empty()) {
				primary = session->get_username();
			}
			if (primary.is_empty()) {
				primary = session->get_email();
			}
			if (primary.is_empty()) {
				primary = TTR("Authenticated User");
			}
			account_name_label->set_text(primary);

			String secondary = session->get_email();
			if (!session->get_username().is_empty() && session->get_username() != primary) {
				secondary = secondary.is_empty() ? ("@" + session->get_username()) : ("@" + session->get_username() + "  •  " + secondary);
			}
			account_email_label->set_text(secondary);
			account_email_label->set_visible(!secondary.is_empty());

			subject_value_label->set_text(session->get_subject().is_empty() ? "-" : session->get_subject());
			groups_value_label->set_text(_join_string_array(session->get_groups()));
			entitlements_value_label->set_text(_join_string_array(session->get_entitlements()));
			refresh_token_button->set_disabled(busy || !session->has_refresh_token());
			refresh_profile_button->set_disabled(busy);
			_update_expiry_label();
		}
	} else {
		login_browser_button->set_disabled(busy);
		login_device_button->set_disabled(busy);
		cancel_login_button->set_visible(busy);

		const bool waiting_browser = (st == AuthentikClient::STATUS_AWAITING_BROWSER_CALLBACK) && client.is_valid() && !client->get_last_authorization_url().is_empty();
		copy_login_url_button->set_visible(waiting_browser);

		const bool waiting_device = (st == AuthentikClient::STATUS_AWAITING_DEVICE_CODE) && client.is_valid() && !client->get_user_code().is_empty();
		device_code_panel->set_visible(waiting_device);
		if (waiting_device) {
			device_code_value_label->set_text(client->get_user_code());
			device_verify_url_label->set_text(client->get_verification_uri());
		}
	}

	String status_msg;
	bool is_error = false;
	if (!transient_notice.is_empty()) {
		status_msg = transient_notice;
		is_error = transient_notice_is_error;
	} else {
		switch (st) {
			case AuthentikClient::STATUS_DISCOVERING:
				status_msg = TTR("Discovering OpenID Connect provider configuration...");
				break;
			case AuthentikClient::STATUS_AWAITING_BROWSER_CALLBACK:
				status_msg = TTR("Waiting for sign-in confirmation in your web browser...");
				break;
			case AuthentikClient::STATUS_AWAITING_DEVICE_CODE:
				status_msg = TTR("Waiting for device verification approval...");
				break;
			case AuthentikClient::STATUS_EXCHANGING_CODE:
				status_msg = TTR("Exchanging authorization code for tokens...");
				break;
			case AuthentikClient::STATUS_FETCHING_USERINFO:
				status_msg = TTR("Loading account profile and group memberships...");
				break;
			case AuthentikClient::STATUS_REFRESHING:
				status_msg = TTR("Refreshing session token...");
				break;
			case AuthentikClient::STATUS_ERROR:
				status_msg = auth->get_last_error_message();
				is_error = true;
				break;
			default:
				break;
		}
	}

	feedback_label->set_visible(!status_msg.is_empty());
	feedback_label->set_text(status_msg);
	if (is_inside_tree()) {
		Color col = is_error
				? get_theme_color(SNAME("error_color"), EditorStringName(Editor))
				: get_theme_color(SceneStringName(font_color), SNAME("Label"));
		feedback_label->add_theme_color_override(SceneStringName(font_color), col);
	}

	reset_size();
}

void AuthentikAccountDialog::_update_expiry_label() {
	Authentik *auth = Authentik::get_singleton();
	if (!auth || !auth->is_authenticated() || auth->get_session().is_null()) {
		return;
	}
	Ref<AuthentikSession> session = auth->get_session();
	if (session->get_expires_at() <= 0) {
		expiry_value_label->set_text(TTR("Active (no expiration claim)"));
		return;
	}
	expiry_value_label->set_text(_format_duration_seconds(session->get_seconds_until_expiry()));
}

void AuthentikAccountDialog::_on_login_browser_pressed() {
	transient_notice.clear();
	_save_fields_to_settings();
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->login_interactive(true);
		_refresh_ui();
	}
}

void AuthentikAccountDialog::_on_login_device_pressed() {
	transient_notice.clear();
	_save_fields_to_settings();
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->login_device_code(false);
		_refresh_ui();
	}
}

void AuthentikAccountDialog::_on_cancel_login_pressed() {
	transient_notice.clear();
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->cancel_login();
		_refresh_ui();
	}
}

void AuthentikAccountDialog::_on_copy_login_url_pressed() {
	if (!Authentik::get_singleton() || Authentik::get_singleton()->get_client().is_null()) {
		return;
	}
	String url = Authentik::get_singleton()->get_client()->get_last_authorization_url();
	if (!url.is_empty() && DisplayServer::get_singleton()) {
		DisplayServer::get_singleton()->clipboard_set(url);
		transient_notice = TTR("Sign-in URL copied to clipboard.");
		_refresh_ui();
	}
}

void AuthentikAccountDialog::_on_copy_device_code_pressed() {
	if (!Authentik::get_singleton() || Authentik::get_singleton()->get_client().is_null()) {
		return;
	}
	String code = Authentik::get_singleton()->get_client()->get_user_code();
	if (!code.is_empty() && DisplayServer::get_singleton()) {
		DisplayServer::get_singleton()->clipboard_set(code);
		transient_notice = TTR("Verification code copied to clipboard.");
		_refresh_ui();
	}
}

void AuthentikAccountDialog::_on_open_device_verify_pressed() {
	if (!Authentik::get_singleton() || Authentik::get_singleton()->get_client().is_null()) {
		return;
	}
	Ref<AuthentikClient> client = Authentik::get_singleton()->get_client();
	String target = client->get_verification_uri_complete().is_empty()
			? client->get_verification_uri()
			: client->get_verification_uri_complete();
	if (!target.is_empty() && OS::get_singleton()) {
		OS::get_singleton()->shell_open(target);
	}
}

void AuthentikAccountDialog::_on_refresh_token_pressed() {
	transient_notice.clear();
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->refresh_session();
		_refresh_ui();
	}
}

void AuthentikAccountDialog::_on_refresh_profile_pressed() {
	transient_notice.clear();
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->fetch_userinfo();
		_refresh_ui();
	}
}

void AuthentikAccountDialog::_on_sign_out_pressed() {
	transient_notice.clear();
	if (Authentik::get_singleton()) {
		Authentik::get_singleton()->logout(true, false);
		_refresh_ui();
	}
}

void AuthentikAccountDialog::_on_import_config_pressed() {
	if (!import_config_dialog) {
		return;
	}
	if (EditorSettings::get_singleton()) {
		String def_dir = EDITOR_GET("filesystem/directories/default_project_path");
		if (!def_dir.is_empty()) {
			import_config_dialog->set_current_dir(def_dir);
		}
	}
	import_config_dialog->popup_file_dialog();
}

void AuthentikAccountDialog::_on_import_config_file_selected(const String &p_path) {
	Error file_err = OK;
	String content = FileAccess::get_file_as_string(p_path, &file_err);
	if (file_err != OK || content.is_empty()) {
		transient_notice = TTR("Could not read the selected JSON configuration file.");
		transient_notice_is_error = true;
		_refresh_ui();
		return;
	}

	Dictionary parsed_cfg;
	Dictionary editor_cfg;
	String parse_err;
	if (StudioApiClient::parse_studio_config_json(content, parsed_cfg, editor_cfg, parse_err) != OK) {
		transient_notice = parse_err.is_empty() ? TTR("Invalid Studio configuration JSON file.") : parse_err;
		transient_notice_is_error = true;
		_refresh_ui();
		return;
	}

	if (!editor_cfg.has("network/authentik/issuer_url") &&
			!editor_cfg.has("network/authentik/studio_api_url") &&
			!editor_cfg.has("network/authentik/client_id")) {
		transient_notice = TTR("Selected JSON file does not contain Authentik or Studio Server settings.");
		transient_notice_is_error = true;
		_refresh_ui();
		return;
	}

	if (editor_cfg.has("network/authentik/issuer_url")) {
		issuer_url_edit->set_text(String(editor_cfg["network/authentik/issuer_url"]));
	}
	if (editor_cfg.has("network/authentik/client_id")) {
		client_id_edit->set_text(String(editor_cfg["network/authentik/client_id"]));
	}
	if (editor_cfg.has("network/authentik/scopes")) {
		scopes_edit->set_text(String(editor_cfg["network/authentik/scopes"]));
	}
	if (editor_cfg.has("network/authentik/studio_api_url")) {
		studio_api_url_edit->set_text(String(editor_cfg["network/authentik/studio_api_url"]));
	}
	if (editor_cfg.has("network/authentik/redirect_port")) {
		redirect_port_spin->set_value_no_signal(double(int(editor_cfg["network/authentik/redirect_port"])));
	}
	if (editor_cfg.has("network/authentik/redirect_uri")) {
		redirect_uri_edit->set_text(String(editor_cfg["network/authentik/redirect_uri"]));
	}
	if (editor_cfg.has("network/authentik/persist_session")) {
		persist_session_check->set_pressed_no_signal(bool(editor_cfg["network/authentik/persist_session"]));
	}
	if (editor_cfg.has("network/authentik/auto_refresh")) {
		auto_refresh_check->set_pressed_no_signal(bool(editor_cfg["network/authentik/auto_refresh"]));
	}

	_save_fields_to_settings();
	transient_notice_is_error = false;

	String proj_name;
	if (parsed_cfg.get("project", Variant()).get_type() == Variant::DICTIONARY) {
		Dictionary p = parsed_cfg["project"];
		proj_name = String(p.get("name", String())).strip_edges();
	}
	if (!proj_name.is_empty()) {
		transient_notice = vformat(TTR("Imported Studio settings from '%s' (project: %s)."), p_path.get_file(), proj_name);
	} else {
		transient_notice = vformat(TTR("Imported Studio settings from '%s'."), p_path.get_file());
	}
	_refresh_ui();
}

void AuthentikAccountDialog::_on_save_settings_pressed() {
	_save_fields_to_settings();
	transient_notice = TTR("Provider settings saved.");
	transient_notice_is_error = false;
	_refresh_ui();
}

void AuthentikAccountDialog::_on_authenticated(const Ref<AuthentikSession> &p_session) {
	transient_notice.clear();
	_refresh_ui();
}

void AuthentikAccountDialog::_on_authentication_failed(int p_error_code, const String &p_error_message) {
	transient_notice.clear();
	_refresh_ui();
}

void AuthentikAccountDialog::_on_logged_out() {
	transient_notice.clear();
	_refresh_ui();
}

void AuthentikAccountDialog::_on_session_refreshed(const Ref<AuthentikSession> &p_session) {
	transient_notice = TTR("Session refreshed.");
	_refresh_ui();
}

void AuthentikAccountDialog::_on_authorization_url_ready(const String &p_url) {
	transient_notice.clear();
	_refresh_ui();
}

void AuthentikAccountDialog::_on_device_code_received(const String &p_user_code, const String &p_verification_uri, const String &p_verification_uri_complete, int p_expires_in) {
	transient_notice.clear();
	_refresh_ui();
}

void AuthentikAccountDialog::_on_state_changed(int p_status) {
	if (p_status != AuthentikClient::STATUS_AUTHENTICATED) {
		transient_notice.clear();
	}
	_refresh_ui();
}

// ============================================================================
// AuthentikAccountButton
// ============================================================================

AuthentikAccountButton::AuthentikAccountButton() {
	set_flat(true);
	set_focus_mode(FOCUS_NONE);

	account_dialog = memnew(AuthentikAccountDialog);
	add_child(account_dialog);
}

void AuthentikAccountButton::pressed() {
	open_account_dialog();
}

void AuthentikAccountButton::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			Authentik::ensure_editor_settings_registered();
			if (Authentik::get_singleton()) {
				Authentik::get_singleton()->reload_settings();
			}
			_connect_authentik_signals();
			_update_appearance();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_disconnect_authentik_signals();
		} break;

		case NOTIFICATION_THEME_CHANGED:
		case NOTIFICATION_TRANSLATION_CHANGED: {
			_update_appearance();
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			EditorSettings *es = EditorSettings::get_singleton();
			if (es && es->check_changed_settings_in_group("network/authentik")) {
				if (Authentik::get_singleton()) {
					Authentik::get_singleton()->reload_settings();
				}
				_update_appearance();
			}
		} break;
	}
}

void AuthentikAccountButton::_connect_authentik_signals() {
	if (signals_connected || !Authentik::get_singleton()) {
		return;
	}
	Authentik *auth = Authentik::get_singleton();
	auth->connect("authenticated", callable_mp(this, &AuthentikAccountButton::_on_authenticated));
	auth->connect("authentication_failed", callable_mp(this, &AuthentikAccountButton::_on_authentication_failed));
	auth->connect("logged_out", callable_mp(this, &AuthentikAccountButton::_on_logged_out));
	auth->connect("session_refreshed", callable_mp(this, &AuthentikAccountButton::_on_session_refreshed));
	auth->connect("state_changed", callable_mp(this, &AuthentikAccountButton::_on_state_changed));
	signals_connected = true;
}

void AuthentikAccountButton::_disconnect_authentik_signals() {
	if (!signals_connected || !Authentik::get_singleton()) {
		signals_connected = false;
		return;
	}
	Authentik *auth = Authentik::get_singleton();
	auth->disconnect("authenticated", callable_mp(this, &AuthentikAccountButton::_on_authenticated));
	auth->disconnect("authentication_failed", callable_mp(this, &AuthentikAccountButton::_on_authentication_failed));
	auth->disconnect("logged_out", callable_mp(this, &AuthentikAccountButton::_on_logged_out));
	auth->disconnect("session_refreshed", callable_mp(this, &AuthentikAccountButton::_on_session_refreshed));
	auth->disconnect("state_changed", callable_mp(this, &AuthentikAccountButton::_on_state_changed));
	signals_connected = false;
}

void AuthentikAccountButton::_update_appearance() {
	if (!is_inside_tree()) {
		return;
	}
	Authentik *auth = Authentik::get_singleton();
	if (!auth) {
		set_text(TTR("Sign In"));
		set_button_icon(get_editor_theme_icon(SNAME("CryptoKey")));
		return;
	}

	if (auth->is_authenticated() && auth->get_session().is_valid()) {
		Ref<AuthentikSession> session = auth->get_session();
		String label = session->get_display_name();
		if (label.is_empty()) {
			label = session->get_username();
		}
		if (label.is_empty()) {
			label = session->get_email();
		}
		if (label.is_empty()) {
			label = TTR("Account");
		}
		if (label.length() > 24) {
			label = label.substr(0, 21) + "...";
		}
		set_text(label);
		set_button_icon(get_editor_theme_icon(SNAME("StatusSuccess")));

		String tip = vformat(TTR("Signed in as %s"), session->get_display_name().is_empty() ? label : session->get_display_name());
		if (!session->get_email().is_empty()) {
			tip += "\n" + session->get_email();
		}
		if (!session->get_groups().is_empty()) {
			tip += "\n" + vformat(TTR("Groups: %s"), _join_string_array(session->get_groups()));
		}
		set_tooltip_text(tip);
	} else if (auth->is_busy()) {
		set_text(TTR("Signing in..."));
		set_button_icon(get_editor_theme_icon(SNAME("Reload")));
		set_tooltip_text(TTR("Authentik sign-in in progress. Click to view details or cancel."));
	} else {
		set_text(TTR("Sign In"));
		set_button_icon(get_editor_theme_icon(SNAME("CryptoKey")));
		set_tooltip_text(auth->is_configured()
						? TTR("Sign in with your Authentik / OpenID Connect account.")
						: TTR("Configure and sign in with an Authentik / OpenID Connect account."));
	}
}

void AuthentikAccountButton::open_account_dialog() {
	if (account_dialog) {
		account_dialog->open_dialog();
	}
}

void AuthentikAccountButton::_on_authenticated(const Ref<AuthentikSession> &p_session) {
	_update_appearance();
}

void AuthentikAccountButton::_on_authentication_failed(int p_error_code, const String &p_error_message) {
	_update_appearance();
}

void AuthentikAccountButton::_on_logged_out() {
	_update_appearance();
}

void AuthentikAccountButton::_on_session_refreshed(const Ref<AuthentikSession> &p_session) {
	_update_appearance();
}

void AuthentikAccountButton::_on_state_changed(int p_status) {
	_update_appearance();
}

// ============================================================================
// AuthentikEditorPlugin
// ============================================================================

void AuthentikEditorPlugin::_open_account_dialog() {
	if (toolbar_button) {
		toolbar_button->open_account_dialog();
	}
}

void AuthentikEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			Authentik::ensure_editor_settings_registered();
			toolbar_button = memnew(AuthentikAccountButton);
			add_control_to_container(CONTAINER_TOOLBAR, toolbar_button);
			add_tool_menu_item(TTR("Authentik Account..."), callable_mp(this, &AuthentikEditorPlugin::_open_account_dialog));
		} break;

		case NOTIFICATION_EXIT_TREE: {
			remove_tool_menu_item(TTR("Authentik Account..."));
			if (toolbar_button) {
				remove_control_from_container(CONTAINER_TOOLBAR, toolbar_button);
				memdelete(toolbar_button);
				toolbar_button = nullptr;
			}
		} break;
	}
}

#endif // TOOLS_ENABLED
