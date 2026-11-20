/**************************************************************************/
/*  authentik.cpp                                                         */
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

#include "authentik.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

#ifdef TOOLS_ENABLED
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"
#endif

Authentik *Authentik::singleton = nullptr;

static String _first_non_empty_env(const char *const *p_names, int p_count) {
	if (!OS::get_singleton()) {
		return String();
	}
	for (int i = 0; i < p_count; i++) {
		String val = OS::get_singleton()->get_environment(p_names[i]).strip_edges();
		if (!val.is_empty()) {
			return val;
		}
	}
	return String();
}

Authentik::Authentik() {
	singleton = this;
	default_client.instantiate();
	default_client->connect("authenticated", callable_mp(this, &Authentik::_on_authenticated));
	default_client->connect("authentication_failed", callable_mp(this, &Authentik::_on_authentication_failed));
	default_client->connect("logged_out", callable_mp(this, &Authentik::_on_logged_out));
	default_client->connect("session_refreshed", callable_mp(this, &Authentik::_on_session_refreshed));
	default_client->connect("authorization_url_ready", callable_mp(this, &Authentik::_on_authorization_url_ready));
	default_client->connect("device_code_received", callable_mp(this, &Authentik::_on_device_code_received));
	default_client->connect("state_changed", callable_mp(this, &Authentik::_on_state_changed));
	reload_settings();
}

Authentik::~Authentik() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

#ifdef TOOLS_ENABLED
void Authentik::ensure_editor_settings_registered() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}
	EDITOR_DEF_BASIC("network/authentik/issuer_url", "");
	es->add_property_hint(PropertyInfo(Variant::STRING, "network/authentik/issuer_url"));

	EDITOR_DEF_BASIC("network/authentik/client_id", "");
	es->add_property_hint(PropertyInfo(Variant::STRING, "network/authentik/client_id"));

	EDITOR_DEF_BASIC("network/authentik/scopes", "openid profile email offline_access");
	es->add_property_hint(PropertyInfo(Variant::STRING, "network/authentik/scopes"));

	EDITOR_DEF("network/authentik/redirect_port", 0);
	es->add_property_hint(PropertyInfo(Variant::INT, "network/authentik/redirect_port", PROPERTY_HINT_RANGE, "0,65535,1"));

	EDITOR_DEF("network/authentik/redirect_uri", "");
	es->add_property_hint(PropertyInfo(Variant::STRING, "network/authentik/redirect_uri"));

	EDITOR_DEF_BASIC("network/authentik/persist_session", true);
	es->add_property_hint(PropertyInfo(Variant::BOOL, "network/authentik/persist_session"));

	EDITOR_DEF("network/authentik/auto_refresh", true);
	es->add_property_hint(PropertyInfo(Variant::BOOL, "network/authentik/auto_refresh"));

	EDITOR_DEF_BASIC("network/authentik/studio_api_url", "");
	es->add_property_hint(PropertyInfo(Variant::STRING, "network/authentik/studio_api_url"));
}
#endif

bool Authentik::has_environment_override() const {
	static const char *const vars[] = {
		"AETHERION_AUTHENTIK_ISSUER_URL",
		"AUTHENTIK_ISSUER_URL",
		"OIDC_ISSUER_URL",
		"AETHERION_AUTHENTIK_CLIENT_ID",
		"AUTHENTIK_CLIENT_ID",
		"OIDC_CLIENT_ID",
		"AETHERION_STUDIO_API_URL",
		"STUDIO_API_URL",
		"AETHERION_AUTHENTIK_STUDIO_API_URL",
	};
	return !_first_non_empty_env(vars, 9).is_empty();
}

void Authentik::configure(
		const String &p_issuer_url,
		const String &p_client_id,
		const String &p_scopes,
		const String &p_redirect_uri,
		int p_redirect_port) {
	manual_override = true;
	default_client->set_issuer_url(p_issuer_url);
	default_client->set_client_id(p_client_id);
	default_client->set_scopes(p_scopes);
	default_client->set_redirect_uri(p_redirect_uri);
	default_client->set_redirect_port(p_redirect_port);
}

void Authentik::clear_manual_override() {
	manual_override = false;
	manual_studio_override = false;
	reload_settings();
}

void Authentik::reload_settings() {
	static const char *const studio_url_vars[] = { "AETHERION_STUDIO_API_URL", "STUDIO_API_URL", "AETHERION_AUTHENTIK_STUDIO_API_URL" };
	if (!manual_studio_override) {
		String resolved_studio_url = _first_non_empty_env(studio_url_vars, 3);
#ifdef TOOLS_ENABLED
		EditorSettings *es_studio = EditorSettings::get_singleton();
		if (es_studio) {
			ensure_editor_settings_registered();
			if (resolved_studio_url.is_empty()) {
				resolved_studio_url = String(es_studio->get_setting("network/authentik/studio_api_url")).strip_edges();
			}
		}
#endif
		ProjectSettings *ps_studio = ProjectSettings::get_singleton();
		if (ps_studio && resolved_studio_url.is_empty() && ps_studio->has_setting("network/authentik/studio_api_url")) {
			resolved_studio_url = String(ps_studio->get_setting("network/authentik/studio_api_url")).strip_edges();
		}
		studio_api_url = StudioApiClient::clean_api_url_string(resolved_studio_url);
	}

	if (manual_override || default_client.is_null()) {
		return;
	}

	static const char *const issuer_vars[] = { "AETHERION_AUTHENTIK_ISSUER_URL", "AUTHENTIK_ISSUER_URL", "OIDC_ISSUER_URL" };
	static const char *const client_vars[] = { "AETHERION_AUTHENTIK_CLIENT_ID", "AUTHENTIK_CLIENT_ID", "OIDC_CLIENT_ID" };
	static const char *const scope_vars[] = { "AETHERION_AUTHENTIK_SCOPES", "AUTHENTIK_SCOPES", "OIDC_SCOPES" };
	static const char *const redir_uri_vars[] = { "AETHERION_AUTHENTIK_REDIRECT_URI", "AUTHENTIK_REDIRECT_URI", "OIDC_REDIRECT_URI" };
	static const char *const redir_port_vars[] = { "AETHERION_AUTHENTIK_REDIRECT_PORT", "AUTHENTIK_REDIRECT_PORT", "OIDC_REDIRECT_PORT" };

	String resolved_issuer = _first_non_empty_env(issuer_vars, 3);
	String resolved_client = _first_non_empty_env(client_vars, 3);
	String resolved_scopes = _first_non_empty_env(scope_vars, 3);
	String resolved_redir_uri = _first_non_empty_env(redir_uri_vars, 3);
	String env_port = _first_non_empty_env(redir_port_vars, 3);
	int resolved_port = env_port.is_valid_int() ? env_port.to_int() : -1;

	bool resolved_persist = true;
	bool resolved_auto_refresh = true;
	String resolved_storage = "user://authentik_session.dat";

#ifdef TOOLS_ENABLED
	EditorSettings *es = EditorSettings::get_singleton();
	if (es) {
		ensure_editor_settings_registered();
		if (resolved_issuer.is_empty()) {
			resolved_issuer = String(es->get_setting("network/authentik/issuer_url")).strip_edges();
		}
		if (resolved_client.is_empty()) {
			resolved_client = String(es->get_setting("network/authentik/client_id")).strip_edges();
		}
		if (resolved_scopes.is_empty()) {
			resolved_scopes = String(es->get_setting("network/authentik/scopes")).strip_edges();
		}
		if (resolved_redir_uri.is_empty()) {
			resolved_redir_uri = String(es->get_setting("network/authentik/redirect_uri")).strip_edges();
		}
		if (resolved_port < 0) {
			resolved_port = int(es->get_setting("network/authentik/redirect_port"));
		}
		resolved_persist = bool(es->get_setting("network/authentik/persist_session"));
		resolved_auto_refresh = bool(es->get_setting("network/authentik/auto_refresh"));

		if (EditorPaths::get_singleton() && EditorPaths::get_singleton()->are_paths_valid()) {
			resolved_storage = EditorPaths::get_singleton()->get_config_dir().path_join("authentik_session.dat");
		}
	}
#endif

	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps) {
		if (resolved_issuer.is_empty() && ps->has_setting("network/authentik/issuer_url")) {
			resolved_issuer = String(ps->get_setting("network/authentik/issuer_url")).strip_edges();
		}
		if (resolved_client.is_empty() && ps->has_setting("network/authentik/client_id")) {
			resolved_client = String(ps->get_setting("network/authentik/client_id")).strip_edges();
		}
		if (resolved_scopes.is_empty() && ps->has_setting("network/authentik/scopes")) {
			resolved_scopes = String(ps->get_setting("network/authentik/scopes")).strip_edges();
		}
		if (resolved_redir_uri.is_empty() && ps->has_setting("network/authentik/redirect_uri")) {
			resolved_redir_uri = String(ps->get_setting("network/authentik/redirect_uri")).strip_edges();
		}
		if (resolved_port < 0 && ps->has_setting("network/authentik/redirect_port")) {
			resolved_port = int(ps->get_setting("network/authentik/redirect_port"));
		}
#ifdef TOOLS_ENABLED
		if (!EditorSettings::get_singleton())
#endif
		{
			if (ps->has_setting("network/authentik/persist_session")) {
				resolved_persist = bool(ps->get_setting("network/authentik/persist_session"));
			}
			if (ps->has_setting("network/authentik/auto_refresh")) {
				resolved_auto_refresh = bool(ps->get_setting("network/authentik/auto_refresh"));
			}
		}
	}

	if (resolved_scopes.is_empty()) {
		resolved_scopes = "openid profile email offline_access";
	}
	if (resolved_port < 0 || resolved_port > 65535) {
		resolved_port = 0;
	}

	default_client->set_issuer_url(resolved_issuer);
	default_client->set_client_id(resolved_client);
	default_client->set_scopes(resolved_scopes);
	default_client->set_redirect_uri(resolved_redir_uri);
	default_client->set_redirect_port(resolved_port);
	default_client->set_persist_session(resolved_persist);
	default_client->set_auto_refresh(resolved_auto_refresh);
	default_client->set_storage_path(resolved_storage);

	String storage_identity = resolved_storage + "|" + resolved_issuer + "|" + resolved_client;
	if (resolved_persist && default_client->is_configured() && !default_client->is_authenticated() && last_loaded_storage_key != storage_identity) {
		last_loaded_storage_key = storage_identity;
		default_client->load_session();
	}
}

Ref<AuthentikClient> Authentik::create_client(const String &p_issuer_url, const String &p_client_id, const String &p_scopes) const {
	Ref<AuthentikClient> c;
	c.instantiate();
	c->set_issuer_url(p_issuer_url.is_empty() ? get_issuer_url() : p_issuer_url);
	c->set_client_id(p_client_id.is_empty() ? get_client_id() : p_client_id);
	c->set_scopes(p_scopes);
	return c;
}

Ref<StudioApiClient> Authentik::create_studio_client(const String &p_api_url) const {
	Ref<StudioApiClient> c;
	c.instantiate();
	c->set_api_url(p_api_url.is_empty() ? studio_api_url : p_api_url);
	return c;
}

void Authentik::set_studio_api_url(const String &p_url) {
	manual_studio_override = true;
	studio_api_url = StudioApiClient::clean_api_url_string(p_url);
}

String Authentik::get_issuer_url() const { return default_client->get_issuer_url(); }
String Authentik::get_client_id() const { return default_client->get_client_id(); }
String Authentik::get_scopes() const { return default_client->get_scopes(); }
bool Authentik::is_configured() const { return default_client->is_configured(); }
bool Authentik::is_authenticated() const { return default_client->is_authenticated(); }
bool Authentik::is_busy() const { return default_client->is_busy(); }
AuthentikClient::Status Authentik::get_status() const { return default_client->get_status(); }
AuthentikClient::AuthError Authentik::get_last_error() const { return default_client->get_last_error(); }
String Authentik::get_last_error_message() const { return default_client->get_last_error_message(); }

Ref<AuthentikSession> Authentik::get_session() const { return default_client->get_session(); }
String Authentik::get_access_token() const { return default_client->get_access_token(); }
String Authentik::get_bearer_token() const { return default_client->get_bearer_token(); }
String Authentik::get_authorization_header() const { return default_client->get_authorization_header(); }
bool Authentik::has_group(const String &p_group) const { return default_client->has_group(p_group); }
bool Authentik::has_any_group(const PackedStringArray &p_groups) const { return default_client->has_any_group(p_groups); }
bool Authentik::has_entitlement(const String &p_entitlement) const { return default_client->has_entitlement(p_entitlement); }

Error Authentik::login_interactive(bool p_open_browser) {
	if (!manual_override) {
		reload_settings();
	}
	return default_client->login_interactive(p_open_browser);
}

Error Authentik::login_device_code(bool p_open_browser) {
	if (!manual_override) {
		reload_settings();
	}
	return default_client->login_device_code(p_open_browser);
}

Error Authentik::complete_authorization_callback(const String &p_callback_url_or_query) {
	return default_client->complete_authorization_callback(p_callback_url_or_query);
}

void Authentik::cancel_login() { default_client->cancel_login(); }
Error Authentik::refresh_session() { return default_client->refresh_session(); }
Error Authentik::fetch_userinfo() { return default_client->fetch_userinfo(); }
void Authentik::logout(bool p_revoke_remote, bool p_open_end_session) { default_client->logout(p_revoke_remote, p_open_end_session); }
void Authentik::poll() { default_client->poll(); }

void Authentik::_on_authenticated(const Ref<AuthentikSession> &p_session) { emit_signal("authenticated", p_session); }
void Authentik::_on_authentication_failed(int p_error_code, const String &p_error_message) { emit_signal("authentication_failed", p_error_code, p_error_message); }
void Authentik::_on_logged_out() { emit_signal("logged_out"); }
void Authentik::_on_session_refreshed(const Ref<AuthentikSession> &p_session) { emit_signal("session_refreshed", p_session); }
void Authentik::_on_authorization_url_ready(const String &p_url) { emit_signal("authorization_url_ready", p_url); }
void Authentik::_on_device_code_received(const String &p_user_code, const String &p_verification_uri, const String &p_verification_uri_complete, int p_expires_in) {
	emit_signal("device_code_received", p_user_code, p_verification_uri, p_verification_uri_complete, p_expires_in);
}
void Authentik::_on_state_changed(int p_status) { emit_signal("state_changed", p_status); }

void Authentik::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "issuer_url", "client_id", "scopes", "redirect_uri", "redirect_port"), &Authentik::configure, DEFVAL("openid profile email offline_access"), DEFVAL(String()), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("clear_manual_override"), &Authentik::clear_manual_override);
	ClassDB::bind_method(D_METHOD("reload_settings"), &Authentik::reload_settings);
	ClassDB::bind_method(D_METHOD("has_environment_override"), &Authentik::has_environment_override);

	ClassDB::bind_method(D_METHOD("get_client"), &Authentik::get_client);
	ClassDB::bind_method(D_METHOD("create_client", "issuer_url", "client_id", "scopes"), &Authentik::create_client, DEFVAL(String()), DEFVAL(String()), DEFVAL("openid profile email offline_access"));
	ClassDB::bind_method(D_METHOD("create_studio_client", "api_url"), &Authentik::create_studio_client, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("set_studio_api_url", "url"), &Authentik::set_studio_api_url);
	ClassDB::bind_method(D_METHOD("get_studio_api_url"), &Authentik::get_studio_api_url);

	ClassDB::bind_method(D_METHOD("get_issuer_url"), &Authentik::get_issuer_url);
	ClassDB::bind_method(D_METHOD("get_client_id"), &Authentik::get_client_id);
	ClassDB::bind_method(D_METHOD("get_scopes"), &Authentik::get_scopes);
	ClassDB::bind_method(D_METHOD("is_configured"), &Authentik::is_configured);
	ClassDB::bind_method(D_METHOD("is_authenticated"), &Authentik::is_authenticated);
	ClassDB::bind_method(D_METHOD("is_busy"), &Authentik::is_busy);
	ClassDB::bind_method(D_METHOD("get_status"), &Authentik::get_status);
	ClassDB::bind_method(D_METHOD("get_last_error"), &Authentik::get_last_error);
	ClassDB::bind_method(D_METHOD("get_last_error_message"), &Authentik::get_last_error_message);

	ClassDB::bind_method(D_METHOD("get_session"), &Authentik::get_session);
	ClassDB::bind_method(D_METHOD("get_access_token"), &Authentik::get_access_token);
	ClassDB::bind_method(D_METHOD("get_bearer_token"), &Authentik::get_bearer_token);
	ClassDB::bind_method(D_METHOD("get_authorization_header"), &Authentik::get_authorization_header);
	ClassDB::bind_method(D_METHOD("has_group", "group"), &Authentik::has_group);
	ClassDB::bind_method(D_METHOD("has_any_group", "groups"), &Authentik::has_any_group);
	ClassDB::bind_method(D_METHOD("has_entitlement", "entitlement"), &Authentik::has_entitlement);

	ClassDB::bind_method(D_METHOD("login_interactive", "open_browser"), &Authentik::login_interactive, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("login_device_code", "open_browser"), &Authentik::login_device_code, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("complete_authorization_callback", "callback_url_or_query"), &Authentik::complete_authorization_callback);
	ClassDB::bind_method(D_METHOD("cancel_login"), &Authentik::cancel_login);
	ClassDB::bind_method(D_METHOD("refresh_session"), &Authentik::refresh_session);
	ClassDB::bind_method(D_METHOD("fetch_userinfo"), &Authentik::fetch_userinfo);
	ClassDB::bind_method(D_METHOD("logout", "revoke_remote", "open_end_session"), &Authentik::logout, DEFVAL(false), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("poll"), &Authentik::poll);

	ADD_SIGNAL(MethodInfo("authenticated", PropertyInfo(Variant::OBJECT, "session", PROPERTY_HINT_RESOURCE_TYPE, "AuthentikSession")));
	ADD_SIGNAL(MethodInfo("authentication_failed", PropertyInfo(Variant::INT, "error_code"), PropertyInfo(Variant::STRING, "error_message")));
	ADD_SIGNAL(MethodInfo("logged_out"));
	ADD_SIGNAL(MethodInfo("session_refreshed", PropertyInfo(Variant::OBJECT, "session", PROPERTY_HINT_RESOURCE_TYPE, "AuthentikSession")));
	ADD_SIGNAL(MethodInfo("authorization_url_ready", PropertyInfo(Variant::STRING, "url")));
	ADD_SIGNAL(MethodInfo(
			"device_code_received",
			PropertyInfo(Variant::STRING, "user_code"),
			PropertyInfo(Variant::STRING, "verification_uri"),
			PropertyInfo(Variant::STRING, "verification_uri_complete"),
			PropertyInfo(Variant::INT, "expires_in")));
	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "status")));
}

