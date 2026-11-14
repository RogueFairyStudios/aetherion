/**************************************************************************/
/*  authentik_editor_plugin.h                                             */
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

#include "editor/plugins/editor_plugin.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"

class CheckBox;
class EditorFileDialog;
class GridContainer;
class HBoxContainer;
class Label;
class LineEdit;
class PanelContainer;
class SpinBox;
class TextureRect;
class Timer;
class VBoxContainer;

class AuthentikAccountDialog : public AcceptDialog {
	GDCLASS(AuthentikAccountDialog, AcceptDialog);

	Label *env_override_label = nullptr;

	// Signed-in UI
	PanelContainer *signed_in_panel = nullptr;
	TextureRect *signed_in_icon = nullptr;
	Label *account_name_label = nullptr;
	Label *account_email_label = nullptr;
	Label *subject_value_label = nullptr;
	Label *groups_value_label = nullptr;
	Label *entitlements_value_label = nullptr;
	Label *expiry_value_label = nullptr;
	Button *refresh_token_button = nullptr;
	Button *refresh_profile_button = nullptr;
	Button *sign_out_button = nullptr;

	// Signed-out / active login UI
	VBoxContainer *signed_out_vbox = nullptr;
	Button *login_browser_button = nullptr;
	Button *login_device_button = nullptr;
	Button *cancel_login_button = nullptr;
	Button *copy_login_url_button = nullptr;

	PanelContainer *device_code_panel = nullptr;
	Label *device_code_value_label = nullptr;
	Label *device_verify_url_label = nullptr;
	Button *copy_device_code_button = nullptr;
	Button *open_device_verify_button = nullptr;

	Label *feedback_label = nullptr;

	// Provider configuration fields
	LineEdit *issuer_url_edit = nullptr;
	LineEdit *client_id_edit = nullptr;
	LineEdit *scopes_edit = nullptr;
	LineEdit *studio_api_url_edit = nullptr;
	SpinBox *redirect_port_spin = nullptr;
	LineEdit *redirect_uri_edit = nullptr;
	CheckBox *persist_session_check = nullptr;
	CheckBox *auto_refresh_check = nullptr;
	Button *import_config_button = nullptr;
	Button *save_settings_button = nullptr;
	EditorFileDialog *import_config_dialog = nullptr;

	Timer *countdown_timer = nullptr;
	bool signals_connected = false;
	String transient_notice;
	bool transient_notice_is_error = false;

	void _connect_authentik_signals();
	void _disconnect_authentik_signals();

	void _load_fields_from_settings();
	void _save_fields_to_settings();
	void _refresh_ui();
	void _update_expiry_label();

	void _on_login_browser_pressed();
	void _on_login_device_pressed();
	void _on_cancel_login_pressed();
	void _on_copy_login_url_pressed();
	void _on_copy_device_code_pressed();
	void _on_open_device_verify_pressed();
	void _on_refresh_token_pressed();
	void _on_refresh_profile_pressed();
	void _on_sign_out_pressed();
	void _on_import_config_pressed();
	void _on_import_config_file_selected(const String &p_path);
	void _on_save_settings_pressed();

	void _on_authenticated(const Ref<AuthentikSession> &p_session);
	void _on_authentication_failed(int p_error_code, const String &p_error_message);
	void _on_logged_out();
	void _on_session_refreshed(const Ref<AuthentikSession> &p_session);
	void _on_authorization_url_ready(const String &p_url);
	void _on_device_code_received(const String &p_user_code, const String &p_verification_uri, const String &p_verification_uri_complete, int p_expires_in);
	void _on_state_changed(int p_status);
	void _on_visibility_changed();

protected:
	void _notification(int p_what);

public:
	void open_dialog();

	AuthentikAccountDialog();
};

class AuthentikAccountButton : public Button {
	GDCLASS(AuthentikAccountButton, Button);

	AuthentikAccountDialog *account_dialog = nullptr;
	bool signals_connected = false;

	void _connect_authentik_signals();
	void _disconnect_authentik_signals();
	void _update_appearance();

	void _on_authenticated(const Ref<AuthentikSession> &p_session);
	void _on_authentication_failed(int p_error_code, const String &p_error_message);
	void _on_logged_out();
	void _on_session_refreshed(const Ref<AuthentikSession> &p_session);
	void _on_state_changed(int p_status);

protected:
	void _notification(int p_what);
	virtual void pressed() override;

public:
	void open_account_dialog();

	AuthentikAccountButton();
};

class AuthentikEditorPlugin : public EditorPlugin {
	GDCLASS(AuthentikEditorPlugin, EditorPlugin);

	AuthentikAccountButton *toolbar_button = nullptr;

	void _open_account_dialog();

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "Authentik"; }
	virtual bool has_main_screen() const override { return false; }

	AuthentikEditorPlugin() {}
};

#endif // TOOLS_ENABLED
