/**************************************************************************/
/*  studio_api_client.h                                                   */
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

#include "core/io/http_client.h"
#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class StudioApiClient : public RefCounted {
	GDCLASS(StudioApiClient, RefCounted);

private:
	enum RequestKind {
		REQ_NONE = 0,
		REQ_PROJECTS = 1,
		REQ_WORKSPACES = 2,
		REQ_CONFIG = 3,
	};

	String api_url;
	String client_slug = "aetherion";
	String bearer_token;
	String service_key;
	bool tls_verify = true;

	RequestKind active_kind = REQ_NONE;
	String pending_config_tool;
	String pending_config_project;
	String pending_config_format;
	Ref<HTTPClient> http_client;
	String http_request_path;
	Vector<String> http_headers;
	PackedByteArray http_response_body;
	int http_response_code = 0;
	List<String> http_response_headers;
	bool http_request_dispatched = false;
	int http_redirects = 0;
	uint64_t http_started_msec = 0;
	bool scene_tree_connected = false;

	Array last_projects;
	Array last_workspaces;
	Dictionary last_user;
	String last_config_raw;
	Dictionary last_config;
	int last_http_code = 0;
	String last_error;

	String _resolve_effective_api_url() const;
	String _resolve_effective_bearer_token() const;
	String _resolve_effective_service_key() const;
	String _build_config_endpoint(const String &p_base_norm, const String &p_project_ref, const String &p_tool) const;

	Error _start_get_request(RequestKind p_kind, const String &p_endpoint_path, const Dictionary &p_query, const String &p_accept = "application/json");
	Error _connect_url(RequestKind p_kind, const String &p_full_url, const Vector<String> &p_headers, int p_redirect_count);
	void _close_http();
	void _update_auto_poll_connection();
	void _on_process_frame();
	void _fail_request(int p_http_code, const String &p_message);
	void _complete_request(RequestKind p_kind, int p_http_code, const String &p_body_text, const String &p_cfg_tool = String(), const String &p_cfg_project = String(), const String &p_cfg_format = String());

protected:
	static void _bind_methods();

public:
	static String clean_api_url_string(const String &p_url);
	static Error validate_api_url(const String &p_url, String &r_normalized, String &r_error);
	static Error parse_projects_json(const String &p_json, Array &r_projects, Dictionary &r_user, String &r_error);
	static Error parse_workspaces_json(const String &p_json, Array &r_workspaces, Dictionary &r_user, String &r_error);
	static Error parse_studio_config_json(const String &p_json, Dictionary &r_config, Dictionary &r_editor_settings, String &r_error);
	static String extract_error_message(const String &p_body_text, int p_http_code);

	void set_api_url(const String &p_api_url);
	String get_api_url() const;

	void set_client_slug(const String &p_client_slug);
	String get_client_slug() const { return client_slug; }

	void set_bearer_token(const String &p_token);
	String get_bearer_token() const { return bearer_token; }

	void set_service_key(const String &p_key);
	String get_service_key() const { return service_key; }

	void set_tls_verify(bool p_verify);
	bool is_tls_verify_enabled() const { return tls_verify; }

	bool is_configured() const;
	bool has_credentials() const;
	bool is_busy() const { return active_kind != REQ_NONE && http_client.is_valid(); }

	Array get_last_projects() const { return last_projects; }
	Array get_last_workspaces() const { return last_workspaces; }
	Dictionary get_last_user() const { return last_user; }
	String get_last_config_raw() const { return last_config_raw; }
	Dictionary get_last_config() const { return last_config; }
	int get_last_http_code() const { return last_http_code; }
	String get_last_error() const { return last_error; }

	Error fetch_my_projects(bool p_include_all_kinds = false, bool p_include_archived = false);
	Error fetch_user_projects(const String &p_identifier, bool p_include_all_kinds = false, bool p_include_archived = false);
	Error fetch_my_workspaces(const String &p_vcs = String(), const String &p_track = String(), bool p_only_writable = false);
	Error fetch_user_workspaces(const String &p_identifier, const String &p_vcs = "lore", const String &p_track = String(), bool p_only_writable = false);
	Error fetch_studio_config(const String &p_tool = "aetherion", const String &p_format = "json");
	Error fetch_project_config(const String &p_project_ref, const String &p_tool = "aetherion", const String &p_format = "json");

	void cancel();
	void poll();

	StudioApiClient() {}
	~StudioApiClient() override;
};
