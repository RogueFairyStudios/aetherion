/**************************************************************************/
/*  authentik.h                                                           */
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

#include "authentik_client.h"
#include "studio_api_client.h"

#include "core/object/object.h"

class Authentik : public Object {
	GDCLASS(Authentik, Object);

	static Authentik *singleton;

	Ref<AuthentikClient> default_client;
	bool manual_override = false;
	bool manual_studio_override = false;
	String studio_api_url;
	String last_loaded_storage_key;

	void _on_authenticated(const Ref<AuthentikSession> &p_session);
	void _on_authentication_failed(int p_error_code, const String &p_error_message);
	void _on_logged_out();
	void _on_session_refreshed(const Ref<AuthentikSession> &p_session);
	void _on_authorization_url_ready(const String &p_url);
	void _on_device_code_received(const String &p_user_code, const String &p_verification_uri, const String &p_verification_uri_complete, int p_expires_in);
	void _on_state_changed(int p_status);

protected:
	static void _bind_methods();

public:
	static Authentik *get_singleton() { return singleton; }

#ifdef TOOLS_ENABLED
	static void ensure_editor_settings_registered();
#endif

	void configure(
			const String &p_issuer_url,
			const String &p_client_id,
			const String &p_scopes = "openid profile email offline_access",
			const String &p_redirect_uri = String(),
			int p_redirect_port = 0);
	void clear_manual_override();
	void reload_settings();
	bool has_environment_override() const;

	Ref<AuthentikClient> get_client() const { return default_client; }
	Ref<AuthentikClient> create_client(
			const String &p_issuer_url = String(),
			const String &p_client_id = String(),
			const String &p_scopes = "openid profile email offline_access") const;
	Ref<StudioApiClient> create_studio_client(const String &p_api_url = String()) const;

	void set_studio_api_url(const String &p_url);
	String get_studio_api_url() const { return studio_api_url; }

	String get_issuer_url() const;
	String get_client_id() const;
	String get_scopes() const;
	bool is_configured() const;
	bool is_authenticated() const;
	bool is_busy() const;
	AuthentikClient::Status get_status() const;
	AuthentikClient::AuthError get_last_error() const;
	String get_last_error_message() const;

	Ref<AuthentikSession> get_session() const;
	String get_access_token() const;
	String get_bearer_token() const;
	String get_authorization_header() const;
	bool has_group(const String &p_group) const;
	bool has_any_group(const PackedStringArray &p_groups) const;
	bool has_entitlement(const String &p_entitlement) const;

	Error login_interactive(bool p_open_browser = true);
	Error login_device_code(bool p_open_browser = false);
	Error complete_authorization_callback(const String &p_callback_url_or_query);
	void cancel_login();
	Error refresh_session();
	Error fetch_userinfo();
	void logout(bool p_revoke_remote = false, bool p_open_end_session = false);
	void poll();

	Authentik();
	~Authentik() override;
};

