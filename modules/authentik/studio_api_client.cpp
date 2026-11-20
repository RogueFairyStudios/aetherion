/**************************************************************************/
/*  studio_api_client.cpp                                                 */
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

#include "studio_api_client.h"

#include "authentik.h"
#include "authentik_oidc.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"

static constexpr uint64_t STUDIO_HTTP_TIMEOUT_MSEC = 20000;
static constexpr int STUDIO_MAX_HTTP_REDIRECTS = 5;
static constexpr int STUDIO_MAX_RESPONSE_BYTES = 2 * 1024 * 1024;

static bool _is_loopback_host(const String &p_host) {
	String h = p_host.strip_edges().to_lower();
	return h == "localhost" || h == "127.0.0.1" || h == "::1" || h == "[::1]";
}

static String _find_response_header(const List<String> &p_headers, const String &p_name_lower) {
	for (const String &h : p_headers) {
		int colon = h.find_char(':');
		if (colon > 0) {
			if (h.substr(0, colon).strip_edges().to_lower() == p_name_lower) {
				return h.substr(colon + 1).strip_edges();
			}
		}
	}
	return String();
}

StudioApiClient::~StudioApiClient() {
	_close_http();
	if (scene_tree_connected && SceneTree::get_singleton()) {
		Callable cb = callable_mp(this, &StudioApiClient::_on_process_frame);
		if (SceneTree::get_singleton()->is_connected("process_frame", cb)) {
			SceneTree::get_singleton()->disconnect("process_frame", cb);
		}
		scene_tree_connected = false;
	}
}

String StudioApiClient::clean_api_url_string(const String &p_url) {
	String trimmed = p_url.strip_edges();
	while (trimmed.ends_with("/")) {
		trimmed = trimmed.substr(0, trimmed.length() - 1);
	}
	if (trimmed.ends_with("/v1")) {
		trimmed = trimmed.substr(0, trimmed.length() - 3);
		while (trimmed.ends_with("/")) {
			trimmed = trimmed.substr(0, trimmed.length() - 1);
		}
	}
	return trimmed;
}

Error StudioApiClient::validate_api_url(const String &p_url, String &r_normalized, String &r_error) {
	r_normalized = String();
	r_error = String();

	String cleaned = clean_api_url_string(p_url);
	if (cleaned.is_empty()) {
		r_error = "Studio API URL is not configured.";
		return ERR_UNCONFIGURED;
	}

	String scheme;
	String host;
	int port = 0;
	String path;
	String fragment;
	Error err = cleaned.parse_url(scheme, host, port, path, fragment);
	if (err != OK || host.is_empty() || (scheme != "https://" && scheme != "http://")) {
		r_error = "Studio API URL must be a valid http(s):// URL.";
		return ERR_INVALID_PARAMETER;
	}

	if (scheme == "http://" && !_is_loopback_host(host)) {
		r_error = "Studio API URL must use HTTPS unless targeting localhost or 127.0.0.1.";
		return ERR_INVALID_PARAMETER;
	}

	String norm = scheme + host;
	if (port > 0 && !((scheme == "https://" && port == 443) || (scheme == "http://" && port == 80))) {
		norm += ":" + itos(port);
	}
	if (!path.is_empty() && path != "/") {
		int qpos = path.find_char('?');
		String path_only = (qpos >= 0) ? path.substr(0, qpos) : path;
		while (path_only.ends_with("/")) {
			path_only = path_only.substr(0, path_only.length() - 1);
		}
		if (!path_only.is_empty()) {
			norm += path_only;
		}
	}

	r_normalized = norm;
	return OK;
}

Error StudioApiClient::parse_projects_json(const String &p_json, Array &r_projects, Dictionary &r_user, String &r_error) {
	r_projects = Array();
	r_user = Dictionary();
	r_error = String();

	JSON json;
	Error err = json.parse(p_json);
	if (err != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		r_error = "Invalid JSON response from Studio Server.";
		return ERR_PARSE_ERROR;
	}

	Dictionary root = json.get_data();
	if (root.has("error") && !root.has("projects")) {
		r_error = String(root["error"]);
		return FAILED;
	}

	Variant projects_var = root.get("projects", Variant());
	if (projects_var.get_type() != Variant::ARRAY) {
		r_error = "Studio Server response did not contain a 'projects' array.";
		return ERR_INVALID_DATA;
	}

	r_projects = projects_var;
	Variant user_var = root.get("user", Variant());
	if (user_var.get_type() == Variant::DICTIONARY) {
		r_user = user_var;
	}
	return OK;
}

Error StudioApiClient::parse_workspaces_json(const String &p_json, Array &r_workspaces, Dictionary &r_user, String &r_error) {
	r_workspaces = Array();
	r_user = Dictionary();
	r_error = String();

	JSON json;
	Error err = json.parse(p_json);
	if (err != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		r_error = "Invalid JSON response from Studio Server.";
		return ERR_PARSE_ERROR;
	}

	Dictionary root = json.get_data();
	if (root.has("error") && !root.has("workspaces")) {
		r_error = String(root["error"]);
		return FAILED;
	}

	Variant ws_var = root.get("workspaces", Variant());
	if (ws_var.get_type() != Variant::ARRAY) {
		r_error = "Studio Server response did not contain a 'workspaces' array.";
		return ERR_INVALID_DATA;
	}

	r_workspaces = ws_var;
	Variant user_var = root.get("user", Variant());
	if (user_var.get_type() == Variant::DICTIONARY) {
		r_user = user_var;
	}
	return OK;
}

static String _scopes_variant_to_string(const Variant &p_val) {
	if (p_val.get_type() == Variant::STRING) {
		return String(p_val).strip_edges();
	}
	if (p_val.get_type() == Variant::ARRAY) {
		Array arr = p_val;
		String out;
		for (int i = 0; i < arr.size(); i++) {
			String item = String(arr[i]).strip_edges();
			if (item.is_empty()) {
				continue;
			}
			if (!out.is_empty()) {
				out += " ";
			}
			out += item;
		}
		return out;
	}
	return String();
}

Error StudioApiClient::parse_studio_config_json(const String &p_json, Dictionary &r_config, Dictionary &r_editor_settings, String &r_error) {
	r_config = Dictionary();
	r_editor_settings = Dictionary();
	r_error = String();

	JSON json;
	Error err = json.parse(p_json);
	if (err != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		r_error = "Invalid JSON configuration manifest.";
		return ERR_PARSE_ERROR;
	}

	Dictionary root = json.get_data();
	if (root.has("error") && !root.has("schemaVersion") && !root.has("editorSettings") && !root.has("authentik") && !root.has("profiles")) {
		r_error = String(root["error"]);
		return FAILED;
	}

	r_config = root;

	Dictionary eff = root;
	if (root.get("tools", Variant()).get_type() == Variant::DICTIONARY) {
		Dictionary tools = root["tools"];
		if (tools.get("aetherion", Variant()).get_type() == Variant::DICTIONARY) {
			eff = tools["aetherion"];
		}
	}

	if (eff.get("editorSettings", Variant()).get_type() == Variant::DICTIONARY) {
		Dictionary es = eff["editorSettings"];
		if (es.has("network/authentik/issuer_url")) {
			String v = String(es["network/authentik/issuer_url"]).strip_edges();
			if (!v.is_empty()) {
				r_editor_settings["network/authentik/issuer_url"] = v;
			}
		}
		if (es.has("network/authentik/client_id")) {
			String v = String(es["network/authentik/client_id"]).strip_edges();
			if (!v.is_empty()) {
				r_editor_settings["network/authentik/client_id"] = v;
			}
		}
		if (es.has("network/authentik/scopes")) {
			String v = _scopes_variant_to_string(es["network/authentik/scopes"]);
			if (!v.is_empty()) {
				r_editor_settings["network/authentik/scopes"] = v;
			}
		}
		if (es.has("network/authentik/studio_api_url")) {
			String v = clean_api_url_string(String(es["network/authentik/studio_api_url"]));
			if (!v.is_empty()) {
				r_editor_settings["network/authentik/studio_api_url"] = v;
			}
		}
		if (es.has("network/authentik/redirect_port")) {
			r_editor_settings["network/authentik/redirect_port"] = int(es["network/authentik/redirect_port"]);
		}
		if (es.has("network/authentik/redirect_uri")) {
			r_editor_settings["network/authentik/redirect_uri"] = String(es["network/authentik/redirect_uri"]).strip_edges();
		}
		if (es.has("network/authentik/persist_session")) {
			r_editor_settings["network/authentik/persist_session"] = bool(es["network/authentik/persist_session"]);
		}
		if (es.has("network/authentik/auto_refresh")) {
			r_editor_settings["network/authentik/auto_refresh"] = bool(es["network/authentik/auto_refresh"]);
		}
	}

	Dictionary provider_aeth;
	if (eff.get("providers", Variant()).get_type() == Variant::DICTIONARY) {
		Dictionary provs = eff["providers"];
		if (provs.get("aetherion", Variant()).get_type() == Variant::DICTIONARY) {
			provider_aeth = provs["aetherion"];
		}
	} else if (root.get("providers", Variant()).get_type() == Variant::DICTIONARY) {
		Dictionary provs = root["providers"];
		if (provs.get("aetherion", Variant()).get_type() == Variant::DICTIONARY) {
			provider_aeth = provs["aetherion"];
		}
	}

	Dictionary auth_dict;
	if (eff.get("authentik", Variant()).get_type() == Variant::DICTIONARY) {
		auth_dict = eff["authentik"];
	} else if (root.get("authentik", Variant()).get_type() == Variant::DICTIONARY) {
		auth_dict = root["authentik"];
	}

	Dictionary studio_dict;
	if (eff.get("studio", Variant()).get_type() == Variant::DICTIONARY) {
		studio_dict = eff["studio"];
	} else if (root.get("studio", Variant()).get_type() == Variant::DICTIONARY) {
		studio_dict = root["studio"];
	}

	if (!r_editor_settings.has("network/authentik/issuer_url")) {
		String iss = String(provider_aeth.get("issuerUrl", auth_dict.get("issuerUrl", String()))).strip_edges();
		if (!iss.is_empty()) {
			r_editor_settings["network/authentik/issuer_url"] = iss;
		}
	}
	if (!r_editor_settings.has("network/authentik/client_id")) {
		String cid = String(provider_aeth.get("clientId", auth_dict.get("clientId", String()))).strip_edges();
		if (!cid.is_empty()) {
			r_editor_settings["network/authentik/client_id"] = cid;
		}
	}
	if (!r_editor_settings.has("network/authentik/scopes")) {
		String sc = _scopes_variant_to_string(provider_aeth.has("scopes") ? provider_aeth["scopes"] : auth_dict.get("scopes", Variant()));
		if (!sc.is_empty()) {
			r_editor_settings["network/authentik/scopes"] = sc;
		}
	}
	if (!r_editor_settings.has("network/authentik/studio_api_url")) {
		String api = clean_api_url_string(String(studio_dict.get("studioApiUrl", String())));
		if (!api.is_empty()) {
			r_editor_settings["network/authentik/studio_api_url"] = api;
		}
	}
	if (!r_editor_settings.has("network/authentik/redirect_port") && auth_dict.has("loopbackRedirectPort")) {
		r_editor_settings["network/authentik/redirect_port"] = int(auth_dict["loopbackRedirectPort"]);
	}

	return OK;
}

String StudioApiClient::extract_error_message(const String &p_body_text, int p_http_code) {
	String trimmed = p_body_text.strip_edges();
	if (!trimmed.is_empty()) {
		JSON json;
		if (json.parse(trimmed) == OK && json.get_data().get_type() == Variant::DICTIONARY) {
			Dictionary d = json.get_data();
			if (d.has("error") && !String(d["error"]).strip_edges().is_empty()) {
				return String(d["error"]).strip_edges();
			}
			if (d.has("message") && !String(d["message"]).strip_edges().is_empty()) {
				return String(d["message"]).strip_edges();
			}
		}
	}
	if (p_http_code == 401) {
		return "Authentication rejected by Studio Server (HTTP 401). Please sign in again.";
	}
	if (p_http_code == 403) {
		return "Access denied by Studio Server (HTTP 403).";
	}
	if (p_http_code == 404) {
		return "Requested Studio resource or user was not found (HTTP 404).";
	}
	return vformat("Studio Server returned HTTP %d.", p_http_code);
}

void StudioApiClient::set_api_url(const String &p_api_url) {
	api_url = clean_api_url_string(p_api_url);
}

String StudioApiClient::get_api_url() const {
	return _resolve_effective_api_url();
}

void StudioApiClient::set_client_slug(const String &p_client_slug) {
	String trimmed = p_client_slug.strip_edges().to_lower();
	client_slug = trimmed.is_empty() ? String("aetherion") : trimmed;
}

void StudioApiClient::set_bearer_token(const String &p_token) {
	bearer_token = p_token.strip_edges();
}

void StudioApiClient::set_service_key(const String &p_key) {
	service_key = p_key.strip_edges();
}

void StudioApiClient::set_tls_verify(bool p_verify) {
	tls_verify = p_verify;
}

String StudioApiClient::_resolve_effective_api_url() const {
	if (!api_url.is_empty()) {
		return api_url;
	}
	if (Authentik::get_singleton()) {
		return Authentik::get_singleton()->get_studio_api_url();
	}
	return String();
}

String StudioApiClient::_resolve_effective_bearer_token() const {
	if (!bearer_token.is_empty()) {
		return bearer_token;
	}
	if (Authentik::get_singleton()) {
		String tok = Authentik::get_singleton()->get_bearer_token();
		if (!tok.is_empty()) {
			return tok;
		}
		return Authentik::get_singleton()->get_access_token();
	}
	return String();
}

String StudioApiClient::_resolve_effective_service_key() const {
	if (!service_key.is_empty()) {
		return service_key;
	}
	if (OS::get_singleton()) {
		String env_key = OS::get_singleton()->get_environment("AETHERION_STUDIO_KEY").strip_edges();
		if (!env_key.is_empty()) {
			return env_key;
		}
		env_key = OS::get_singleton()->get_environment("STUDIO_KEY").strip_edges();
		if (!env_key.is_empty()) {
			return env_key;
		}
	}
	return String();
}

bool StudioApiClient::is_configured() const {
	String norm;
	String err;
	return validate_api_url(_resolve_effective_api_url(), norm, err) == OK;
}

bool StudioApiClient::has_credentials() const {
	return !_resolve_effective_bearer_token().is_empty() || !_resolve_effective_service_key().is_empty();
}

Error StudioApiClient::fetch_my_projects(bool p_include_all_kinds, bool p_include_archived) {
	Dictionary q;
	q["client"] = client_slug;
	if (p_include_all_kinds) {
		q["includeAllKinds"] = "1";
	}
	if (p_include_archived) {
		q["includeArchived"] = "1";
	}
	return _start_get_request(REQ_PROJECTS, "/v1/me/projects", q);
}

Error StudioApiClient::fetch_user_projects(const String &p_identifier, bool p_include_all_kinds, bool p_include_archived) {
	String trimmed = p_identifier.strip_edges();
	if (trimmed.is_empty() || trimmed.to_lower() == "me") {
		return fetch_my_projects(p_include_all_kinds, p_include_archived);
	}
	Dictionary q;
	q["client"] = client_slug;
	if (p_include_all_kinds) {
		q["includeAllKinds"] = "1";
	}
	if (p_include_archived) {
		q["includeArchived"] = "1";
	}
	String encoded = AuthentikOIDC::percent_encode_component(trimmed);
	return _start_get_request(REQ_PROJECTS, "/v1/users/" + encoded + "/projects", q);
}

Error StudioApiClient::fetch_my_workspaces(const String &p_vcs, const String &p_track, bool p_only_writable) {
	Dictionary q;
	q["client"] = client_slug;
	if (!p_vcs.strip_edges().is_empty()) {
		q["vcs"] = p_vcs.strip_edges().to_lower();
	}
	if (!p_track.strip_edges().is_empty()) {
		q["track"] = p_track.strip_edges().to_lower();
	}
	if (p_only_writable) {
		q["onlyWritable"] = "1";
	}
	return _start_get_request(REQ_WORKSPACES, "/v1/me/workspaces", q);
}

Error StudioApiClient::fetch_user_workspaces(const String &p_identifier, const String &p_vcs, const String &p_track, bool p_only_writable) {
	String trimmed = p_identifier.strip_edges();
	if (trimmed.is_empty() || trimmed.to_lower() == "me") {
		return fetch_my_workspaces(p_vcs, p_track, p_only_writable);
	}
	Dictionary q;
	q["client"] = client_slug;
	if (!p_vcs.strip_edges().is_empty()) {
		q["vcs"] = p_vcs.strip_edges().to_lower();
	}
	if (!p_track.strip_edges().is_empty()) {
		q["track"] = p_track.strip_edges().to_lower();
	}
	if (p_only_writable) {
		q["onlyWritable"] = "1";
	}
	String encoded = AuthentikOIDC::percent_encode_component(trimmed);
	return _start_get_request(REQ_WORKSPACES, "/v1/users/" + encoded + "/workspaces", q);
}

String StudioApiClient::_build_config_endpoint(const String &p_base_norm, const String &p_project_ref, const String &p_tool) const {
	String prefix = p_base_norm.ends_with("/p/studio") ? String() : String("/v1");
	String clean_tool = p_tool.strip_edges().to_lower();
	if (clean_tool == "catalog") {
		clean_tool = String();
	}
	if (p_project_ref.is_empty()) {
		if (clean_tool.is_empty()) {
			return prefix + "/configs";
		}
		return prefix + "/configs/" + AuthentikOIDC::percent_encode_component(clean_tool);
	}
	String enc_proj = AuthentikOIDC::percent_encode_component(p_project_ref);
	if (clean_tool.is_empty()) {
		return prefix + "/projects/" + enc_proj + "/configs";
	}
	return prefix + "/projects/" + enc_proj + "/configs/" + AuthentikOIDC::percent_encode_component(clean_tool);
}

Error StudioApiClient::fetch_studio_config(const String &p_tool, const String &p_format) {
	String tool_norm = p_tool.strip_edges().to_lower();
	if (tool_norm.is_empty()) {
		tool_norm = "aetherion";
	}
	String fmt = p_format.strip_edges().to_lower();
	if (fmt == "yml") {
		fmt = "yaml";
	}
	if (fmt != "yaml") {
		fmt = "json";
	}

	pending_config_tool = tool_norm;
	pending_config_project = String();
	pending_config_format = fmt;

	Dictionary q;
	q["client"] = client_slug;
	q["format"] = fmt;

	String base_clean = clean_api_url_string(_resolve_effective_api_url());
	String endpoint = _build_config_endpoint(base_clean, String(), tool_norm);
	String accept = (fmt == "yaml") ? "application/yaml" : "application/json";
	return _start_get_request(REQ_CONFIG, endpoint, q, accept);
}

Error StudioApiClient::fetch_project_config(const String &p_project_ref, const String &p_tool, const String &p_format) {
	String proj_trimmed = p_project_ref.strip_edges();
	if (proj_trimmed.is_empty()) {
		_fail_request(0, "Project identifier or slug is required.");
		return ERR_INVALID_PARAMETER;
	}
	String tool_norm = p_tool.strip_edges().to_lower();
	if (tool_norm.is_empty()) {
		tool_norm = "aetherion";
	}
	String fmt = p_format.strip_edges().to_lower();
	if (fmt == "yml") {
		fmt = "yaml";
	}
	if (fmt != "yaml") {
		fmt = "json";
	}

	pending_config_tool = tool_norm;
	pending_config_project = proj_trimmed;
	pending_config_format = fmt;

	Dictionary q;
	q["client"] = client_slug;
	q["format"] = fmt;

	String base_clean = clean_api_url_string(_resolve_effective_api_url());
	String endpoint = _build_config_endpoint(base_clean, proj_trimmed, tool_norm);
	String accept = (fmt == "yaml") ? "application/yaml" : "application/json";
	return _start_get_request(REQ_CONFIG, endpoint, q, accept);
}

Error StudioApiClient::_start_get_request(RequestKind p_kind, const String &p_endpoint_path, const Dictionary &p_query, const String &p_accept) {
	String base_norm;
	String url_err;
	Error err = validate_api_url(_resolve_effective_api_url(), base_norm, url_err);
	if (err != OK) {
		_fail_request(0, url_err);
		return err;
	}

	String tok = _resolve_effective_bearer_token();
	String skey = _resolve_effective_service_key();
	if (tok.is_empty() && skey.is_empty()) {
		_fail_request(0, "Not signed in with Authentik (and no STUDIO_KEY configured).");
		return ERR_UNAUTHORIZED;
	}

	Vector<String> headers;
	headers.push_back("Accept: " + (p_accept.is_empty() ? String("application/json") : p_accept));
	headers.push_back("X-Studio-Client: " + client_slug);
	if (!tok.is_empty()) {
		headers.push_back("Authorization: Bearer " + tok);
	} else if (!skey.is_empty()) {
		headers.push_back("X-Studio-Key: " + skey);
	}

	String full_url = AuthentikOIDC::build_url_with_query(base_norm + p_endpoint_path, p_query);
	last_error = String();
	last_http_code = 0;

	err = _connect_url(p_kind, full_url, headers, 0);
	if (err != OK) {
		_fail_request(0, "Unable to initiate connection to Studio Server.");
		return err;
	}
	return OK;
}

Error StudioApiClient::_connect_url(RequestKind p_kind, const String &p_full_url, const Vector<String> &p_headers, int p_redirect_count) {
	_close_http();

	String scheme;
	String host;
	int port = 0;
	String path;
	String fragment;
	Error err = p_full_url.parse_url(scheme, host, port, path, fragment);
	if (err != OK || host.is_empty() || (scheme != "https://" && scheme != "http://")) {
		return ERR_INVALID_PARAMETER;
	}
	if (scheme == "http://" && !_is_loopback_host(host)) {
		return ERR_INVALID_PARAMETER;
	}

	bool use_tls = (scheme == "https://");
	if (port == 0) {
		port = use_tls ? 443 : 80;
	}
	if (path.is_empty()) {
		path = "/";
	}

	Ref<HTTPClient> client = Ref<HTTPClient>(HTTPClient::create());
	ERR_FAIL_COND_V(client.is_null(), ERR_CANT_CREATE);

	Ref<TLSOptions> tls_opts;
	if (use_tls) {
		tls_opts = tls_verify ? TLSOptions::client() : TLSOptions::client_unsafe(Ref<X509Certificate>());
	}

	err = client->connect_to_host(host, port, tls_opts);
	if (err != OK) {
		return err;
	}

	http_client = client;
	active_kind = p_kind;
	http_request_path = path;
	http_headers = p_headers;
	http_response_body.clear();
	http_response_code = 0;
	http_response_headers.clear();
	http_request_dispatched = false;
	http_redirects = p_redirect_count;
	http_started_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;

	_update_auto_poll_connection();
	return OK;
}

void StudioApiClient::_close_http() {
	if (http_client.is_valid()) {
		http_client->close();
		http_client = Ref<HTTPClient>();
	}
	active_kind = REQ_NONE;
	http_response_body.clear();
	http_response_headers.clear();
	http_request_dispatched = false;
	_update_auto_poll_connection();
}

void StudioApiClient::cancel() {
	_close_http();
}

void StudioApiClient::_update_auto_poll_connection() {
	SceneTree *tree = SceneTree::get_singleton();
	if (!tree) {
		scene_tree_connected = false;
		return;
	}

	bool need_poll = is_busy();
	Callable cb = callable_mp(this, &StudioApiClient::_on_process_frame);
	if (need_poll && !scene_tree_connected) {
		if (!tree->is_connected("process_frame", cb)) {
			tree->connect("process_frame", cb);
		}
		scene_tree_connected = true;
	} else if (!need_poll && scene_tree_connected) {
		if (tree->is_connected("process_frame", cb)) {
			tree->disconnect("process_frame", cb);
		}
		scene_tree_connected = false;
	}
}

void StudioApiClient::_on_process_frame() {
	poll();
}

void StudioApiClient::_fail_request(int p_http_code, const String &p_message) {
	_close_http();
	last_http_code = p_http_code;
	last_error = p_message;
	emit_signal("request_failed", p_http_code, p_message);
}

void StudioApiClient::_complete_request(RequestKind p_kind, int p_http_code, const String &p_body_text, const String &p_cfg_tool, const String &p_cfg_project, const String &p_cfg_format) {
	last_http_code = p_http_code;
	if (p_http_code < 200 || p_http_code >= 300) {
		String msg = extract_error_message(p_body_text, p_http_code);
		last_error = msg;
		emit_signal("request_failed", p_http_code, msg);
		return;
	}

	if (p_kind == REQ_PROJECTS) {
		Array parsed_projects;
		Dictionary parsed_user;
		String parse_err;
		if (parse_projects_json(p_body_text, parsed_projects, parsed_user, parse_err) != OK) {
			last_error = parse_err;
			emit_signal("request_failed", p_http_code, parse_err);
			return;
		}
		last_projects = parsed_projects;
		last_user = parsed_user;
		last_error = String();
		emit_signal("projects_loaded", last_projects, last_user);
	} else if (p_kind == REQ_WORKSPACES) {
		Array parsed_ws;
		Dictionary parsed_user;
		String parse_err;
		if (parse_workspaces_json(p_body_text, parsed_ws, parsed_user, parse_err) != OK) {
			last_error = parse_err;
			emit_signal("request_failed", p_http_code, parse_err);
			return;
		}
		last_workspaces = parsed_ws;
		last_user = parsed_user;
		last_error = String();
		emit_signal("workspaces_loaded", last_workspaces, last_user);
	} else if (p_kind == REQ_CONFIG) {
		if (p_cfg_format == "yaml") {
			last_config = Dictionary();
			last_config_raw = p_body_text;
			last_error = String();
			emit_signal("config_loaded", p_cfg_tool, p_cfg_project, p_cfg_format, last_config_raw, last_config);
			return;
		}
		Dictionary parsed_cfg;
		Dictionary dummy_es;
		String parse_err;
		if (parse_studio_config_json(p_body_text, parsed_cfg, dummy_es, parse_err) != OK) {
			last_error = parse_err;
			emit_signal("request_failed", p_http_code, parse_err);
			return;
		}
		last_config = parsed_cfg;
		last_config_raw = p_body_text;
		last_error = String();
		emit_signal("config_loaded", p_cfg_tool, p_cfg_project, p_cfg_format, last_config_raw, last_config);
	}
}

void StudioApiClient::poll() {
	if (http_client.is_null() || active_kind == REQ_NONE) {
		return;
	}

	uint64_t now_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;
	if (http_started_msec > 0 && now_msec > http_started_msec + STUDIO_HTTP_TIMEOUT_MSEC) {
		_fail_request(0, "Studio Server request timed out.");
		return;
	}

	auto finish_response = [this]() {
		RequestKind done_kind = active_kind;
		String done_tool = pending_config_tool;
		String done_proj = pending_config_project;
		String done_fmt = pending_config_format;
		int code = http_response_code;
		List<String> resp_headers(http_response_headers);
		PackedByteArray body_bytes = http_response_body;
		Vector<String> orig_headers = http_headers;
		int redirects = http_redirects;
		_close_http();

		if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && redirects < STUDIO_MAX_HTTP_REDIRECTS) {
			String location = _find_response_header(resp_headers, "location");
			if (!location.is_empty()) {
				// Only follow redirects that stay on the same origin so credentials are never leaked cross-origin.
				String base_norm;
				String dummy_err;
				if (validate_api_url(_resolve_effective_api_url(), base_norm, dummy_err) == OK) {
					String target_url = location;
					if (location.begins_with("/")) {
						String b_scheme, b_host, b_path, b_frag;
						int b_port = 0;
						if (base_norm.parse_url(b_scheme, b_host, b_port, b_path, b_frag) == OK) {
							target_url = b_scheme + b_host + (b_port > 0 ? (":" + itos(b_port)) : String()) + location;
						}
					}
					String b_scheme, b_host, b_path, b_frag;
					int b_port = 0;
					String t_scheme, t_host, t_path, t_frag;
					int t_port = 0;
					if (base_norm.parse_url(b_scheme, b_host, b_port, b_path, b_frag) == OK &&
							target_url.parse_url(t_scheme, t_host, t_port, t_path, t_frag) == OK) {
						int eff_b_port = b_port > 0 ? b_port : (b_scheme == "https://" ? 443 : 80);
						int eff_t_port = t_port > 0 ? t_port : (t_scheme == "https://" ? 443 : 80);
						if (b_scheme == t_scheme && b_host.to_lower() == t_host.to_lower() && eff_b_port == eff_t_port) {
							if (_connect_url(done_kind, target_url, orig_headers, redirects + 1) == OK) {
								return;
							}
						}
					}
				}
			}
		}

		String body_text;
		if (!body_bytes.is_empty()) {
			body_text.append_utf8((const char *)body_bytes.ptr(), body_bytes.size());
		}
		_complete_request(done_kind, code, body_text, done_tool, done_proj, done_fmt);
	};

	HTTPClient::Status st = http_client->get_status();
	switch (st) {
		case HTTPClient::STATUS_RESOLVING:
		case HTTPClient::STATUS_CONNECTING:
		case HTTPClient::STATUS_REQUESTING: {
			http_client->poll();
		} break;

		case HTTPClient::STATUS_CONNECTED: {
			if (!http_request_dispatched) {
				Error req_err = http_client->request(HTTPClient::METHOD_GET, http_request_path, http_headers, nullptr, 0);
				if (req_err != OK) {
					_fail_request(0, "Failed to dispatch HTTP request to Studio Server.");
					return;
				}
				http_request_dispatched = true;
			} else {
				if (http_response_code == 0) {
					http_response_code = http_client->get_response_code();
					http_response_headers.clear();
					http_client->get_response_headers(&http_response_headers);
				}
				finish_response();
			}
		} break;

		case HTTPClient::STATUS_BODY: {
			if (http_response_code == 0) {
				http_response_code = http_client->get_response_code();
				http_response_headers.clear();
				http_client->get_response_headers(&http_response_headers);
				if (!http_client->is_response_chunked() && http_client->get_response_body_length() == 0) {
					finish_response();
					return;
				}
			}

			int64_t expected_len = http_client->get_response_body_length();
			http_client->poll();
			if (http_client->get_status() != HTTPClient::STATUS_BODY) {
				if (http_client->get_status() == HTTPClient::STATUS_CONNECTED || http_client->get_status() == HTTPClient::STATUS_DISCONNECTED) {
					finish_response();
				}
				return;
			}

			PackedByteArray chunk = http_client->read_response_body_chunk();
			if (!chunk.is_empty()) {
				if (http_response_body.size() + chunk.size() > STUDIO_MAX_RESPONSE_BYTES) {
					_fail_request(0, "Studio Server HTTP response exceeded maximum allowed size.");
					return;
				}
				http_response_body.append_array(chunk);
			}

			HTTPClient::Status post_st = http_client->get_status();
			if ((expected_len >= 0 && http_response_body.size() >= expected_len) ||
					post_st == HTTPClient::STATUS_CONNECTED ||
					post_st == HTTPClient::STATUS_DISCONNECTED) {
				finish_response();
			}
		} break;

		case HTTPClient::STATUS_DISCONNECTED: {
			if (http_request_dispatched && http_response_code > 0) {
				finish_response();
			} else {
				_fail_request(0, "Connection closed unexpectedly by Studio Server.");
			}
		} break;

		case HTTPClient::STATUS_CANT_RESOLVE:
		case HTTPClient::STATUS_CANT_CONNECT:
		case HTTPClient::STATUS_CONNECTION_ERROR:
		case HTTPClient::STATUS_TLS_HANDSHAKE_ERROR: {
			String reason = (st == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR)
					? "TLS handshake failed when connecting to Studio Server."
					: "Unable to connect to Studio Server.";
			_fail_request(0, reason);
		} break;
	}
}

void StudioApiClient::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_api_url", "api_url"), &StudioApiClient::set_api_url);
	ClassDB::bind_method(D_METHOD("get_api_url"), &StudioApiClient::get_api_url);
	ClassDB::bind_method(D_METHOD("set_client_slug", "client_slug"), &StudioApiClient::set_client_slug);
	ClassDB::bind_method(D_METHOD("get_client_slug"), &StudioApiClient::get_client_slug);
	ClassDB::bind_method(D_METHOD("set_bearer_token", "bearer_token"), &StudioApiClient::set_bearer_token);
	ClassDB::bind_method(D_METHOD("get_bearer_token"), &StudioApiClient::get_bearer_token);
	ClassDB::bind_method(D_METHOD("set_service_key", "service_key"), &StudioApiClient::set_service_key);
	ClassDB::bind_method(D_METHOD("get_service_key"), &StudioApiClient::get_service_key);
	ClassDB::bind_method(D_METHOD("set_tls_verify", "verify"), &StudioApiClient::set_tls_verify);
	ClassDB::bind_method(D_METHOD("is_tls_verify_enabled"), &StudioApiClient::is_tls_verify_enabled);

	ClassDB::bind_method(D_METHOD("is_configured"), &StudioApiClient::is_configured);
	ClassDB::bind_method(D_METHOD("has_credentials"), &StudioApiClient::has_credentials);
	ClassDB::bind_method(D_METHOD("is_busy"), &StudioApiClient::is_busy);
	ClassDB::bind_method(D_METHOD("get_last_projects"), &StudioApiClient::get_last_projects);
	ClassDB::bind_method(D_METHOD("get_last_workspaces"), &StudioApiClient::get_last_workspaces);
	ClassDB::bind_method(D_METHOD("get_last_user"), &StudioApiClient::get_last_user);
	ClassDB::bind_method(D_METHOD("get_last_config_raw"), &StudioApiClient::get_last_config_raw);
	ClassDB::bind_method(D_METHOD("get_last_config"), &StudioApiClient::get_last_config);
	ClassDB::bind_method(D_METHOD("get_last_http_code"), &StudioApiClient::get_last_http_code);
	ClassDB::bind_method(D_METHOD("get_last_error"), &StudioApiClient::get_last_error);

	ClassDB::bind_method(D_METHOD("fetch_my_projects", "include_all_kinds", "include_archived"), &StudioApiClient::fetch_my_projects, DEFVAL(false), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("fetch_user_projects", "identifier", "include_all_kinds", "include_archived"), &StudioApiClient::fetch_user_projects, DEFVAL(false), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("fetch_my_workspaces", "vcs", "track", "only_writable"), &StudioApiClient::fetch_my_workspaces, DEFVAL(String()), DEFVAL(String()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("fetch_user_workspaces", "identifier", "vcs", "track", "only_writable"), &StudioApiClient::fetch_user_workspaces, DEFVAL("lore"), DEFVAL(String()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("fetch_studio_config", "tool", "format"), &StudioApiClient::fetch_studio_config, DEFVAL("aetherion"), DEFVAL("json"));
	ClassDB::bind_method(D_METHOD("fetch_project_config", "project_ref", "tool", "format"), &StudioApiClient::fetch_project_config, DEFVAL("aetherion"), DEFVAL("json"));
	ClassDB::bind_method(D_METHOD("cancel"), &StudioApiClient::cancel);
	ClassDB::bind_method(D_METHOD("poll"), &StudioApiClient::poll);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "api_url"), "set_api_url", "get_api_url");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "client_slug"), "set_client_slug", "get_client_slug");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tls_verify"), "set_tls_verify", "is_tls_verify_enabled");

	ADD_SIGNAL(MethodInfo("projects_loaded", PropertyInfo(Variant::ARRAY, "projects"), PropertyInfo(Variant::DICTIONARY, "user")));
	ADD_SIGNAL(MethodInfo("workspaces_loaded", PropertyInfo(Variant::ARRAY, "workspaces"), PropertyInfo(Variant::DICTIONARY, "user")));
	ADD_SIGNAL(MethodInfo("config_loaded", PropertyInfo(Variant::STRING, "tool"), PropertyInfo(Variant::STRING, "project_ref"), PropertyInfo(Variant::STRING, "format"), PropertyInfo(Variant::STRING, "raw_content"), PropertyInfo(Variant::DICTIONARY, "parsed_config")));
	ADD_SIGNAL(MethodInfo("request_failed", PropertyInfo(Variant::INT, "http_code"), PropertyInfo(Variant::STRING, "error_message")));
}
