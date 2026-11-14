/**************************************************************************/
/*  authentik_client.cpp                                                  */
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

#include "authentik_client.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"

static constexpr uint64_t HTTP_REQUEST_TIMEOUT_MSEC = 20000;
static constexpr int MAX_HTTP_REDIRECTS = 5;
static constexpr int MAX_LOOPBACK_CONNECTIONS = 8;
static constexpr int MAX_LOOPBACK_REQUEST_BYTES = 16384;

AuthentikClient::~AuthentikClient() {
	_stop_loopback_server();
	_close_http();
	if (scene_tree_connected && SceneTree::get_singleton()) {
		Callable cb = callable_mp(this, &AuthentikClient::_on_process_frame);
		if (SceneTree::get_singleton()->is_connected("process_frame", cb)) {
			SceneTree::get_singleton()->disconnect("process_frame", cb);
		}
		scene_tree_connected = false;
	}
}

void AuthentikClient::set_issuer_url(const String &p_issuer_url) {
	String normalized = AuthentikOIDC::normalize_issuer_url(p_issuer_url);
	if (issuer_url == normalized) {
		return;
	}
	issuer_url = normalized;
	discovery_loaded = false;
	discovered_for_issuer = String();
	endpoints = AuthentikEndpoints();
}

void AuthentikClient::set_client_id(const String &p_client_id) {
	client_id = p_client_id.strip_edges();
}

void AuthentikClient::set_scopes(const String &p_scopes) {
	String trimmed = p_scopes.strip_edges();
	scopes = trimmed.is_empty() ? String("openid profile email offline_access") : trimmed;
}

void AuthentikClient::set_redirect_port(int p_port) {
	ERR_FAIL_COND(p_port < 0 || p_port > 65535);
	redirect_port = p_port;
}

void AuthentikClient::set_redirect_uri(const String &p_uri) {
	redirect_uri = p_uri.strip_edges();
}

void AuthentikClient::set_persist_session(bool p_enable) {
	persist_session = p_enable;
}

void AuthentikClient::set_auto_refresh(bool p_enable) {
	auto_refresh = p_enable;
	_update_auto_poll_connection();
}

void AuthentikClient::set_storage_path(const String &p_path) {
	storage_path = p_path.strip_edges();
}

void AuthentikClient::set_tls_verify(bool p_verify) {
	tls_verify = p_verify;
}

void AuthentikClient::set_login_timeout_sec(int p_seconds) {
	login_timeout_sec = MAX(10, p_seconds);
}

void AuthentikClient::set_refresh_leeway_sec(int p_seconds) {
	refresh_leeway_sec = MAX(0, p_seconds);
}

bool AuthentikClient::is_configured() const {
	return !issuer_url.is_empty() && !client_id.is_empty();
}

bool AuthentikClient::is_authenticated() const {
	if (session.is_null()) {
		return false;
	}
	if (!session->is_expired(0)) {
		return true;
	}
	return auto_refresh && session->has_refresh_token();
}

bool AuthentikClient::is_busy() const {
	return status == STATUS_DISCOVERING ||
			status == STATUS_AWAITING_BROWSER_CALLBACK ||
			status == STATUS_AWAITING_DEVICE_CODE ||
			status == STATUS_EXCHANGING_CODE ||
			status == STATUS_FETCHING_USERINFO ||
			status == STATUS_REFRESHING;
}

String AuthentikClient::get_access_token() const {
	if (session.is_null() || session->is_expired(0)) {
		return String();
	}
	return session->get_access_token();
}

String AuthentikClient::get_bearer_token() const {
	if (session.is_null() || session->is_expired(0)) {
		return String();
	}
	return session->get_bearer_token();
}

String AuthentikClient::get_authorization_header() const {
	if (session.is_null() || session->is_expired(0)) {
		return String();
	}
	return session->get_authorization_header();
}

bool AuthentikClient::has_group(const String &p_group) const {
	return session.is_valid() && !session->is_expired(0) && session->has_group(p_group);
}

bool AuthentikClient::has_any_group(const PackedStringArray &p_groups) const {
	return session.is_valid() && !session->is_expired(0) && session->has_any_group(p_groups);
}

bool AuthentikClient::has_entitlement(const String &p_entitlement) const {
	return session.is_valid() && !session->is_expired(0) && session->has_entitlement(p_entitlement);
}

void AuthentikClient::_set_status(Status p_status) {
	if (status == p_status) {
		_update_auto_poll_connection();
		return;
	}
	status = p_status;
	_update_auto_poll_connection();
	emit_signal("state_changed", int(status));
}

void AuthentikClient::_fail(AuthError p_error, const String &p_message) {
	_stop_loopback_server();
	_close_http();
	pending_op = OP_NONE;
	pending_session = Ref<AuthentikSession>();
	last_error = p_error;
	last_error_message = p_message;
	_set_status(STATUS_ERROR);
	emit_signal("authentication_failed", int(p_error), p_message);
}

void AuthentikClient::_update_auto_poll_connection() {
	SceneTree *tree = SceneTree::get_singleton();
	if (!tree) {
		scene_tree_connected = false;
		return;
	}

	bool need_poll = is_busy() ||
			http_purpose != HTTP_NONE ||
			(auto_refresh && session.is_valid() && session->has_refresh_token());

	Callable cb = callable_mp(this, &AuthentikClient::_on_process_frame);
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

void AuthentikClient::_on_process_frame() {
	poll();
}

Error AuthentikClient::_ensure_endpoints_initialized() {
	if (discovered_for_issuer != issuer_url || endpoints.discovery_url.is_empty()) {
		Error err = AuthentikOIDC::derive_authentik_endpoints(issuer_url, endpoints);
		if (err != OK) {
			return err;
		}
		discovered_for_issuer = issuer_url;
		discovery_loaded = false;
	}
	return OK;
}

Error AuthentikClient::_start_http_request(
		HttpPurpose p_purpose,
		HTTPClient::Method p_method,
		const String &p_url,
		const Vector<String> &p_headers,
		const String &p_body,
		int p_redirect_count) {
	_close_http();

	String scheme;
	String host;
	int port = 0;
	String path;
	String fragment;
	Error err = p_url.parse_url(scheme, host, port, path, fragment);
	if (err != OK || (scheme != "https://" && scheme != "http://") || host.is_empty()) {
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
	http_purpose = p_purpose;
	http_method = p_method;
	http_request_path = path;
	http_headers = p_headers;
	http_body = p_body.utf8();
	http_response_body.clear();
	http_response_code = 0;
	http_response_headers.clear();
	http_request_dispatched = false;
	http_redirects = p_redirect_count;
	http_started_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;

	_update_auto_poll_connection();
	return OK;
}

void AuthentikClient::_close_http() {
	if (http_client.is_valid()) {
		http_client->close();
		http_client = Ref<HTTPClient>();
	}
	http_purpose = HTTP_NONE;
	http_response_body.clear();
	http_response_headers.clear();
	http_request_dispatched = false;
}

static String _find_header_value(const List<String> &p_headers, const String &p_name_lower) {
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

void AuthentikClient::_poll_http() {
	if (http_client.is_null() || http_purpose == HTTP_NONE) {
		return;
	}

	uint64_t now_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;
	if (http_started_msec > 0 && now_msec > http_started_msec + HTTP_REQUEST_TIMEOUT_MSEC) {
		HttpPurpose timed_out_purpose = http_purpose;
		_close_http();
		if (timed_out_purpose == HTTP_DISCOVERY && endpoints.is_valid()) {
			discovery_loaded = true;
			_continue_pending_after_discovery();
			return;
		}
		if (timed_out_purpose == HTTP_REVOKE) {
			return;
		}
		_fail(AUTH_ERROR_TIMEOUT, "OIDC HTTP request timed out.");
		return;
	}

	auto finish_http_response = [this]() {
		HttpPurpose done_purpose = http_purpose;
		int code = http_response_code;
		List<String> resp_headers = http_response_headers;
		PackedByteArray body_bytes = http_response_body;
		HTTPClient::Method orig_method = http_method;
		Vector<String> orig_headers = http_headers;
		String orig_body = String::utf8(http_body.get_data(), http_body.length());
		int redirects = http_redirects;
		_close_http();

		if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && redirects < MAX_HTTP_REDIRECTS) {
			String location = _find_header_value(resp_headers, "location");
			if (!location.is_empty()) {
				HTTPClient::Method next_method = (code == 303) ? HTTPClient::METHOD_GET : orig_method;
				String next_body = (code == 303) ? String() : orig_body;
				if (_start_http_request(done_purpose, next_method, location, orig_headers, next_body, redirects + 1) == OK) {
					return;
				}
			}
		}

		String body_text;
		if (!body_bytes.is_empty()) {
			body_text.append_utf8((const char *)body_bytes.ptr(), body_bytes.size());
		}
		_handle_http_complete(done_purpose, code, body_text);
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
				int body_len = http_body.length();
				const uint8_t *body_ptr = body_len > 0 ? (const uint8_t *)http_body.get_data() : nullptr;
				Error req_err = http_client->request(http_method, http_request_path, http_headers, body_ptr, body_len);
				if (req_err != OK) {
					HttpPurpose failed_purpose = http_purpose;
					_close_http();
					if (failed_purpose == HTTP_DISCOVERY && endpoints.is_valid()) {
						discovery_loaded = true;
						_continue_pending_after_discovery();
						return;
					}
					if (failed_purpose != HTTP_REVOKE) {
						_fail(AUTH_ERROR_NETWORK, "Failed to dispatch HTTP request to Authentik.");
					}
					return;
				}
				http_request_dispatched = true;
			} else {
				if (http_response_code == 0) {
					http_response_code = http_client->get_response_code();
					http_response_headers.clear();
					http_client->get_response_headers(&http_response_headers);
				}
				finish_http_response();
			}
		} break;

		case HTTPClient::STATUS_BODY: {
			if (http_response_code == 0) {
				http_response_code = http_client->get_response_code();
				http_response_headers.clear();
				http_client->get_response_headers(&http_response_headers);
				if (!http_client->is_response_chunked() && http_client->get_response_body_length() == 0) {
					finish_http_response();
					return;
				}
			}

			int64_t expected_len = http_client->get_response_body_length();
			http_client->poll();
			if (http_client->get_status() != HTTPClient::STATUS_BODY) {
				if (http_client->get_status() == HTTPClient::STATUS_CONNECTED || http_client->get_status() == HTTPClient::STATUS_DISCONNECTED) {
					finish_http_response();
				}
				return;
			}

			PackedByteArray chunk = http_client->read_response_body_chunk();
			if (!chunk.is_empty()) {
				if (http_response_body.size() + chunk.size() > 1024 * 1024) {
					_fail(AUTH_ERROR_NETWORK, "OIDC HTTP response exceeded maximum allowed size.");
					return;
				}
				http_response_body.append_array(chunk);
			}

			HTTPClient::Status post_read_status = http_client->get_status();
			if ((expected_len >= 0 && http_response_body.size() >= expected_len) ||
					post_read_status == HTTPClient::STATUS_CONNECTED ||
					post_read_status == HTTPClient::STATUS_DISCONNECTED) {
				finish_http_response();
			}
		} break;

		case HTTPClient::STATUS_DISCONNECTED: {
			if (http_request_dispatched && http_response_code > 0) {
				finish_http_response();
			} else {
				HttpPurpose failed_purpose = http_purpose;
				_close_http();
				if (failed_purpose == HTTP_DISCOVERY && endpoints.is_valid()) {
					discovery_loaded = true;
					_continue_pending_after_discovery();
					return;
				}
				if (failed_purpose != HTTP_REVOKE) {
					_fail(AUTH_ERROR_NETWORK, "Connection closed unexpectedly by OIDC provider.");
				}
			}
		} break;

		case HTTPClient::STATUS_CANT_RESOLVE:
		case HTTPClient::STATUS_CANT_CONNECT:
		case HTTPClient::STATUS_CONNECTION_ERROR:
		case HTTPClient::STATUS_TLS_HANDSHAKE_ERROR: {
			HttpPurpose failed_purpose = http_purpose;
			_close_http();
			if (failed_purpose == HTTP_DISCOVERY && endpoints.is_valid()) {
				discovery_loaded = true;
				_continue_pending_after_discovery();
				return;
			}
			if (failed_purpose != HTTP_REVOKE) {
				String reason = (st == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR)
						? "TLS handshake failed when connecting to Authentik."
						: "Unable to connect to Authentik OIDC endpoint.";
				_fail(AUTH_ERROR_NETWORK, reason);
			}
		} break;
	}
}

Error AuthentikClient::login_interactive(bool p_open_browser) {
	if (!is_configured()) {
		_fail(AUTH_ERROR_NOT_CONFIGURED, "Authentik issuer_url and client_id must be configured before signing in.");
		return ERR_UNCONFIGURED;
	}

	Error err = _ensure_endpoints_initialized();
	if (err != OK) {
		_fail(AUTH_ERROR_NOT_CONFIGURED, "Invalid Authentik issuer_url.");
		return err;
	}

	cancel_login();
	last_error = AUTH_ERROR_NONE;
	last_error_message = String();
	pending_op = OP_LOGIN_PKCE;
	pending_open_browser = p_open_browser;
	operation_started_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;

	if (!discovery_loaded) {
		_set_status(STATUS_DISCOVERING);
		Vector<String> headers;
		headers.push_back("Accept: application/json");
		err = _start_http_request(HTTP_DISCOVERY, HTTPClient::METHOD_GET, endpoints.discovery_url, headers);
		if (err != OK) {
			if (endpoints.is_valid()) {
				discovery_loaded = true;
				return _begin_pkce_authorization();
			}
			_fail(AUTH_ERROR_DISCOVERY_FAILED, "Could not start OIDC discovery request.");
			return err;
		}
		return OK;
	}

	return _begin_pkce_authorization();
}

Error AuthentikClient::login_device_code(bool p_open_browser) {
	if (!is_configured()) {
		_fail(AUTH_ERROR_NOT_CONFIGURED, "Authentik issuer_url and client_id must be configured before signing in.");
		return ERR_UNCONFIGURED;
	}

	Error err = _ensure_endpoints_initialized();
	if (err != OK) {
		_fail(AUTH_ERROR_NOT_CONFIGURED, "Invalid Authentik issuer_url.");
		return err;
	}

	cancel_login();
	last_error = AUTH_ERROR_NONE;
	last_error_message = String();
	pending_op = OP_LOGIN_DEVICE;
	pending_open_browser = p_open_browser;
	operation_started_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;

	if (!discovery_loaded) {
		_set_status(STATUS_DISCOVERING);
		Vector<String> headers;
		headers.push_back("Accept: application/json");
		err = _start_http_request(HTTP_DISCOVERY, HTTPClient::METHOD_GET, endpoints.discovery_url, headers);
		if (err != OK) {
			if (!endpoints.device_authorization_endpoint.is_empty() && !endpoints.token_endpoint.is_empty()) {
				discovery_loaded = true;
				return _begin_device_authorization();
			}
			_fail(AUTH_ERROR_DISCOVERY_FAILED, "Could not start OIDC discovery request.");
			return err;
		}
		return OK;
	}

	return _begin_device_authorization();
}

void AuthentikClient::_continue_pending_after_discovery() {
	switch (pending_op) {
		case OP_LOGIN_PKCE: {
			_begin_pkce_authorization();
		} break;
		case OP_LOGIN_DEVICE: {
			_begin_device_authorization();
		} break;
		case OP_REFRESH: {
			refresh_session();
		} break;
		case OP_USERINFO: {
			fetch_userinfo();
		} break;
		default: {
			_set_status(session.is_valid() && !session->is_expired(0) ? STATUS_AUTHENTICATED : STATUS_IDLE);
		} break;
	}
}

Error AuthentikClient::_begin_pkce_authorization() {
	if (!endpoints.is_valid()) {
		_fail(AUTH_ERROR_DISCOVERY_FAILED, "OIDC discovery did not provide authorization_endpoint and token_endpoint.");
		return ERR_UNAVAILABLE;
	}

	_stop_loopback_server();

	bool bind_loopback = true;
	int bind_port = redirect_port;
	active_callback_path = "/callback";

	if (!redirect_uri.is_empty()) {
		String scheme;
		String host;
		int parsed_port = 0;
		String path;
		String fragment;
		if (redirect_uri.parse_url(scheme, host, parsed_port, path, fragment) == OK &&
				scheme == "http://" && (host == "127.0.0.1" || host == "localhost")) {
			bind_loopback = true;
			bind_port = parsed_port > 0 ? parsed_port : redirect_port;
			active_callback_path = path.is_empty() ? String("/") : path;
			active_redirect_uri = redirect_uri;
		} else {
			// Custom external/mobile redirect scheme (caller completes via complete_authorization_callback).
			bind_loopback = false;
			active_redirect_uri = redirect_uri;
		}
	}

	if (bind_loopback) {
		tcp_server.instantiate();
		Error bind_err = tcp_server->listen(uint16_t(bind_port), IPAddress("127.0.0.1"));
		if (bind_err != OK) {
			tcp_server = Ref<TCPServer>();
			_fail(AUTH_ERROR_LOOPBACK_BIND_FAILED, vformat("Failed to bind local OIDC callback listener on 127.0.0.1:%d.", bind_port));
			return bind_err;
		}
		int bound_port = tcp_server->get_local_port();
		if (redirect_uri.is_empty()) {
			active_redirect_uri = vformat("http://127.0.0.1:%d%s", bound_port, active_callback_path);
		}
	}

	if (AuthentikOIDC::generate_random_token(24, pkce_state) != OK ||
			AuthentikOIDC::generate_random_token(24, pkce_nonce) != OK ||
			AuthentikOIDC::generate_random_token(48, pkce_verifier) != OK) {
		_fail(AUTH_ERROR_NETWORK, "Failed to generate cryptographic PKCE parameters.");
		return ERR_CANT_CREATE;
	}

	String challenge = AuthentikOIDC::compute_pkce_s256_challenge(pkce_verifier);
	Dictionary query;
	query["client_id"] = client_id;
	query["redirect_uri"] = active_redirect_uri;
	query["response_type"] = "code";
	query["scope"] = scopes;
	query["state"] = pkce_state;
	query["nonce"] = pkce_nonce;
	query["code_challenge"] = challenge;
	query["code_challenge_method"] = "S256";

	last_authorization_url = AuthentikOIDC::build_url_with_query(endpoints.authorization_endpoint, query);
	_set_status(STATUS_AWAITING_BROWSER_CALLBACK);
	emit_signal("authorization_url_ready", last_authorization_url);

	if (pending_open_browser && OS::get_singleton()) {
		Error open_err = OS::get_singleton()->shell_open(last_authorization_url);
		if (open_err != OK) {
			_fail(AUTH_ERROR_BROWSER_OPEN_FAILED, "Could not launch system browser for Authentik sign-in.");
			return open_err;
		}
	}

	return OK;
}

Error AuthentikClient::_begin_device_authorization() {
	if (endpoints.device_authorization_endpoint.is_empty() || endpoints.token_endpoint.is_empty()) {
		_fail(AUTH_ERROR_DISCOVERY_FAILED, "OIDC provider does not expose a device_authorization_endpoint.");
		return ERR_UNAVAILABLE;
	}

	Dictionary form;
	form["client_id"] = client_id;
	form["scope"] = scopes;

	Vector<String> headers;
	headers.push_back("Content-Type: application/x-www-form-urlencoded");
	headers.push_back("Accept: application/json");

	_set_status(STATUS_DISCOVERING);
	Error err = _start_http_request(
			HTTP_DEVICE_AUTHORIZE,
			HTTPClient::METHOD_POST,
			endpoints.device_authorization_endpoint,
			headers,
			AuthentikOIDC::form_urlencode(form));
	if (err != OK) {
		_fail(AUTH_ERROR_NETWORK, "Failed to initiate OIDC device authorization request.");
		return err;
	}
	return OK;
}

void AuthentikClient::_stop_loopback_server() {
	for (int i = 0; i < loopback_connections.size(); i++) {
		if (loopback_connections[i].peer.is_valid()) {
			loopback_connections[i].peer->disconnect_from_host();
		}
	}
	loopback_connections.clear();
	if (tcp_server.is_valid()) {
		tcp_server->stop();
		tcp_server = Ref<TCPServer>();
	}
}

void AuthentikClient::_poll_loopback_server() {
	if (status != STATUS_AWAITING_BROWSER_CALLBACK) {
		return;
	}

	uint64_t now_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;
	if (operation_started_msec > 0 && now_msec > operation_started_msec + uint64_t(login_timeout_sec) * 1000ULL) {
		_fail(AUTH_ERROR_TIMEOUT, "Timed out waiting for Authentik sign-in in browser.");
		return;
	}

	if (tcp_server.is_null() || !tcp_server->is_listening()) {
		return;
	}

	while (tcp_server->is_connection_available() && loopback_connections.size() < MAX_LOOPBACK_CONNECTIONS) {
		Ref<StreamPeerTCP> peer = tcp_server->take_connection();
		if (peer.is_valid()) {
			LoopbackConn conn;
			conn.peer = peer;
			conn.accepted_msec = now_msec;
			loopback_connections.push_back(conn);
		}
	}

	for (int i = loopback_connections.size() - 1; i >= 0; i--) {
		LoopbackConn &conn = loopback_connections.write[i];
		if (conn.peer.is_null()) {
			loopback_connections.remove_at(i);
			continue;
		}

		conn.peer->poll();
		StreamPeerTCP::Status peer_status = conn.peer->get_status();
		if (peer_status == StreamPeerTCP::STATUS_NONE || peer_status == StreamPeerTCP::STATUS_ERROR) {
			loopback_connections.remove_at(i);
			continue;
		}
		if (peer_status != StreamPeerTCP::STATUS_CONNECTED) {
			continue;
		}

		int avail = conn.peer->get_available_bytes();
		if (avail > 0) {
			int to_read = MIN(avail, MAX_LOOPBACK_REQUEST_BYTES - conn.buffer.length());
			if (to_read <= 0) {
				conn.peer->disconnect_from_host();
				loopback_connections.remove_at(i);
				continue;
			}
			PackedByteArray chunk;
			chunk.resize(to_read);
			if (conn.peer->get_data(chunk.ptrw(), to_read) == OK) {
				String piece;
				piece.append_utf8((const char *)chunk.ptr(), chunk.size());
				conn.buffer += piece;
			}
		}

		if (!conn.buffer.contains("\r\n\r\n") && !conn.buffer.contains("\n\n")) {
			if (now_msec > conn.accepted_msec + 5000) {
				conn.peer->disconnect_from_host();
				loopback_connections.remove_at(i);
			}
			continue;
		}

		String req_path;
		Dictionary query;
		Error parse_err = AuthentikOIDC::parse_http_get_request(conn.buffer, req_path, query);
		if (parse_err != OK || req_path != active_callback_path) {
			CharString not_found = String("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").utf8();
			conn.peer->put_data((const uint8_t *)not_found.get_data(), not_found.length());
			conn.peer->disconnect_from_host();
			loopback_connections.remove_at(i);
			continue;
		}

		bool has_code = query.has("code") && !String(query["code"]).is_empty();
		String html_resp = has_code
				? AuthentikOIDC::build_loopback_http_response(true, "Sign-in complete", "Authentication succeeded. You may close this browser tab and return to Aetherion.")
				: AuthentikOIDC::build_loopback_http_response(false, "Sign-in failed", "The authentication request was denied or returned an error. Return to Aetherion for details.");
		CharString utf8_resp = html_resp.utf8();
		conn.peer->put_data((const uint8_t *)utf8_resp.get_data(), utf8_resp.length());
		conn.peer->disconnect_from_host();

		_stop_loopback_server();
		_process_callback_query(query);
		return;
	}
}

Error AuthentikClient::complete_authorization_callback(const String &p_callback_url_or_query) {
	if (status != STATUS_AWAITING_BROWSER_CALLBACK) {
		return ERR_INVALID_PARAMETER;
	}

	String raw = p_callback_url_or_query.strip_edges();
	String query_part = raw;
	int qpos = raw.find_char('?');
	if (qpos != -1) {
		query_part = raw.substr(qpos + 1);
	}
	Dictionary query = AuthentikOIDC::parse_query_string(query_part);
	_stop_loopback_server();
	_process_callback_query(query);
	if (status == STATUS_ERROR) {
		return (last_error == AUTH_ERROR_STATE_MISMATCH || last_error == AUTH_ERROR_PROVIDER_DENIED) ? ERR_UNAUTHORIZED : FAILED;
	}
	return OK;
}

void AuthentikClient::_process_callback_query(const Dictionary &p_query) {
	if (p_query.has("error")) {
		String err_code = p_query.get("error", String("access_denied"));
		String err_desc = p_query.get("error_description", String());
		String msg = err_desc.is_empty() ? vformat("Authentik rejected sign-in: %s", err_code) : err_desc;
		_fail(AUTH_ERROR_PROVIDER_DENIED, msg);
		return;
	}

	String returned_state = p_query.get("state", String());
	if (returned_state.is_empty() || pkce_state.is_empty() || !AuthentikOIDC::constant_time_equals(returned_state, pkce_state)) {
		pkce_state = String();
		pkce_verifier = String();
		_fail(AUTH_ERROR_STATE_MISMATCH, "OAuth2 state parameter mismatch.");
		return;
	}

	String code = p_query.get("code", String());
	if (code.is_empty()) {
		_fail(AUTH_ERROR_TOKEN_EXCHANGE_FAILED, "Callback did not include an authorization code.");
		return;
	}

	Dictionary form;
	form["grant_type"] = "authorization_code";
	form["client_id"] = client_id;
	form["code"] = code;
	form["redirect_uri"] = active_redirect_uri;
	form["code_verifier"] = pkce_verifier;

	pkce_state = String();
	pkce_verifier = String();

	Vector<String> headers;
	headers.push_back("Content-Type: application/x-www-form-urlencoded");
	headers.push_back("Accept: application/json");

	_set_status(STATUS_EXCHANGING_CODE);
	Error err = _start_http_request(
			HTTP_TOKEN_EXCHANGE,
			HTTPClient::METHOD_POST,
			endpoints.token_endpoint,
			headers,
			AuthentikOIDC::form_urlencode(form));
	if (err != OK) {
		_fail(AUTH_ERROR_NETWORK, "Failed to start OIDC token exchange.");
	}
}

void AuthentikClient::_poll_device_flow() {
	if (status != STATUS_AWAITING_DEVICE_CODE || http_purpose != HTTP_NONE) {
		return;
	}

	uint64_t now_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;
	if (device_expires_msec > 0 && now_msec >= device_expires_msec) {
		_fail(AUTH_ERROR_TIMEOUT, "Device authorization code expired before completion.");
		return;
	}

	if (now_msec < next_device_poll_msec) {
		return;
	}

	Dictionary form;
	form["grant_type"] = "urn:ietf:params:oauth:grant-type:device_code";
	form["client_id"] = client_id;
	form["device_code"] = device_code;

	Vector<String> headers;
	headers.push_back("Content-Type: application/x-www-form-urlencoded");
	headers.push_back("Accept: application/json");

	next_device_poll_msec = now_msec + uint64_t(device_poll_interval_sec) * 1000ULL;
	_start_http_request(
			HTTP_DEVICE_POLL,
			HTTPClient::METHOD_POST,
			endpoints.token_endpoint,
			headers,
			AuthentikOIDC::form_urlencode(form));
}

void AuthentikClient::cancel_login() {
	bool was_busy = is_busy();
	_stop_loopback_server();
	_close_http();
	pending_op = OP_NONE;
	pending_session = Ref<AuthentikSession>();
	pkce_state = String();
	pkce_nonce = String();
	pkce_verifier = String();
	device_code = String();
	user_code = String();
	verification_uri = String();
	verification_uri_complete = String();

	if (session.is_valid() && !session->is_expired(0)) {
		_set_status(STATUS_AUTHENTICATED);
	} else {
		_set_status(STATUS_IDLE);
	}

	if (was_busy) {
		last_error = AUTH_ERROR_CANCELLED;
		last_error_message = "Sign-in cancelled.";
	}
}

Error AuthentikClient::refresh_session() {
	if (session.is_null() || !session->has_refresh_token()) {
		return ERR_UNAVAILABLE;
	}
	if (!is_configured()) {
		return ERR_UNCONFIGURED;
	}

	Error err = _ensure_endpoints_initialized();
	if (err != OK) {
		return err;
	}

	pending_op = OP_REFRESH;
	if (!discovery_loaded) {
		_set_status(STATUS_DISCOVERING);
		Vector<String> headers;
		headers.push_back("Accept: application/json");
		err = _start_http_request(HTTP_DISCOVERY, HTTPClient::METHOD_GET, endpoints.discovery_url, headers);
		if (err != OK && endpoints.is_valid()) {
			discovery_loaded = true;
		} else {
			return err;
		}
	}

	if (endpoints.token_endpoint.is_empty()) {
		_fail(AUTH_ERROR_REFRESH_FAILED, "Missing token_endpoint for token refresh.");
		return ERR_UNAVAILABLE;
	}

	Dictionary form;
	form["grant_type"] = "refresh_token";
	form["client_id"] = client_id;
	form["refresh_token"] = session->get_refresh_token();

	Vector<String> headers;
	headers.push_back("Content-Type: application/x-www-form-urlencoded");
	headers.push_back("Accept: application/json");

	_set_status(STATUS_REFRESHING);
	return _start_http_request(
			HTTP_TOKEN_REFRESH,
			HTTPClient::METHOD_POST,
			endpoints.token_endpoint,
			headers,
			AuthentikOIDC::form_urlencode(form));
}

Error AuthentikClient::fetch_userinfo() {
	if (session.is_null() || session->get_access_token().is_empty()) {
		return ERR_UNAVAILABLE;
	}
	if (!is_configured()) {
		return ERR_UNCONFIGURED;
	}

	Error err = _ensure_endpoints_initialized();
	if (err != OK) {
		return err;
	}

	pending_op = OP_USERINFO;
	if (!discovery_loaded) {
		_set_status(STATUS_DISCOVERING);
		Vector<String> headers;
		headers.push_back("Accept: application/json");
		err = _start_http_request(HTTP_DISCOVERY, HTTPClient::METHOD_GET, endpoints.discovery_url, headers);
		if (err != OK && !endpoints.userinfo_endpoint.is_empty()) {
			discovery_loaded = true;
		} else {
			return err;
		}
	}

	if (endpoints.userinfo_endpoint.is_empty()) {
		return ERR_UNAVAILABLE;
	}

	Vector<String> headers;
	headers.push_back("Authorization: Bearer " + session->get_access_token());
	headers.push_back("Accept: application/json");

	_set_status(STATUS_FETCHING_USERINFO);
	return _start_http_request(HTTP_USERINFO, HTTPClient::METHOD_GET, endpoints.userinfo_endpoint, headers);
}

void AuthentikClient::logout(bool p_revoke_remote, bool p_open_end_session) {
	String token_to_revoke;
	String id_token_hint;
	if (session.is_valid()) {
		token_to_revoke = session->has_refresh_token() ? session->get_refresh_token() : session->get_access_token();
		id_token_hint = session->get_id_token();
	}

	_stop_loopback_server();
	_close_http();
	pending_op = OP_NONE;
	pending_session = Ref<AuthentikSession>();
	session = Ref<AuthentikSession>();
	last_error = AUTH_ERROR_NONE;
	last_error_message = String();
	clear_stored_session();
	_set_status(STATUS_IDLE);

	if (p_revoke_remote && !token_to_revoke.is_empty() && is_configured()) {
		if (_ensure_endpoints_initialized() == OK && !endpoints.revocation_endpoint.is_empty()) {
			Dictionary form;
			form["client_id"] = client_id;
			form["token"] = token_to_revoke;
			Vector<String> headers;
			headers.push_back("Content-Type: application/x-www-form-urlencoded");
			_start_http_request(
					HTTP_REVOKE,
					HTTPClient::METHOD_POST,
					endpoints.revocation_endpoint,
					headers,
					AuthentikOIDC::form_urlencode(form));
		}
	}

	if (p_open_end_session && OS::get_singleton() && is_configured()) {
		if (_ensure_endpoints_initialized() == OK && !endpoints.end_session_endpoint.is_empty()) {
			Dictionary params;
			if (!id_token_hint.is_empty()) {
				params["id_token_hint"] = id_token_hint;
			}
			OS::get_singleton()->shell_open(AuthentikOIDC::build_url_with_query(endpoints.end_session_endpoint, params));
		}
	}

	emit_signal("logged_out");
}

void AuthentikClient::_handle_http_complete(HttpPurpose p_purpose, int p_code, const String &p_body_text) {
	JSON json;
	Dictionary data;
	if (!p_body_text.is_empty() && json.parse(p_body_text) == OK && json.get_data().get_type() == Variant::DICTIONARY) {
		data = json.get_data();
	}

	switch (p_purpose) {
		case HTTP_DISCOVERY: {
			if (p_code >= 200 && p_code < 300 && !data.is_empty()) {
				AuthentikOIDC::merge_discovery_document(data, endpoints);
				discovery_loaded = true;
				_continue_pending_after_discovery();
			} else if (endpoints.is_valid()) {
				// Fall back to canonical Authentik endpoints when discovery is blocked/unavailable.
				discovery_loaded = true;
				_continue_pending_after_discovery();
			} else {
				_fail(AUTH_ERROR_DISCOVERY_FAILED, vformat("OIDC discovery failed with HTTP %d.", p_code));
			}
		} break;

		case HTTP_TOKEN_EXCHANGE: {
			if (p_code < 200 || p_code >= 300 || data.is_empty()) {
				String desc = data.get("error_description", data.get("error", vformat("Token exchange failed (HTTP %d).", p_code)));
				pkce_nonce = String();
				_fail(AUTH_ERROR_TOKEN_EXCHANGE_FAILED, desc);
				return;
			}
			String expected_nonce = pkce_nonce;
			pkce_nonce = String();
			_handle_token_response(data, expected_nonce, String(), false);
		} break;

		case HTTP_DEVICE_AUTHORIZE: {
			if (p_code < 200 || p_code >= 300 || data.is_empty()) {
				String desc = data.get("error_description", data.get("error", vformat("Device authorization failed (HTTP %d).", p_code)));
				_fail(AUTH_ERROR_TOKEN_EXCHANGE_FAILED, desc);
				return;
			}
			device_code = data.get("device_code", String());
			user_code = data.get("user_code", String());
			verification_uri = data.get("verification_uri", data.get("verification_url", String()));
			verification_uri_complete = data.get("verification_uri_complete", String());
			int expires_in = int(data.get("expires_in", 600));
			device_poll_interval_sec = MAX(1, int(data.get("interval", 5)));

			if (device_code.is_empty() || user_code.is_empty() || verification_uri.is_empty()) {
				_fail(AUTH_ERROR_TOKEN_EXCHANGE_FAILED, "Incomplete device authorization response from provider.");
				return;
			}

			uint64_t now_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;
			device_expires_msec = now_msec + uint64_t(MAX(30, expires_in)) * 1000ULL;
			next_device_poll_msec = now_msec + uint64_t(device_poll_interval_sec) * 1000ULL;

			_set_status(STATUS_AWAITING_DEVICE_CODE);
			emit_signal("device_code_received", user_code, verification_uri, verification_uri_complete, expires_in);

			if (pending_open_browser && OS::get_singleton()) {
				String target_url = !verification_uri_complete.is_empty() ? verification_uri_complete : verification_uri;
				OS::get_singleton()->shell_open(target_url);
			}
		} break;

		case HTTP_DEVICE_POLL: {
			if (p_code >= 200 && p_code < 300 && data.has("access_token")) {
				device_code = String();
				_handle_token_response(data, String(), String(), false);
				return;
			}

			String err_code = data.get("error", String());
			if (err_code == "authorization_pending") {
				// Normal polling state; keep waiting.
				return;
			}
			if (err_code == "slow_down") {
				device_poll_interval_sec += 5;
				uint64_t now_msec = OS::get_singleton() ? OS::get_singleton()->get_ticks_msec() : 0;
				next_device_poll_msec = now_msec + uint64_t(device_poll_interval_sec) * 1000ULL;
				return;
			}
			if (err_code == "expired_token") {
				_fail(AUTH_ERROR_TIMEOUT, "Device authorization code expired.");
				return;
			}
			if (err_code == "access_denied") {
				_fail(AUTH_ERROR_PROVIDER_DENIED, "User denied device authorization request.");
				return;
			}

			String desc = data.get("error_description", err_code.is_empty() ? vformat("Device token polling failed (HTTP %d).", p_code) : err_code);
			_fail(AUTH_ERROR_TOKEN_EXCHANGE_FAILED, desc);
		} break;

		case HTTP_TOKEN_REFRESH: {
			if (p_code < 200 || p_code >= 300 || data.is_empty()) {
				session = Ref<AuthentikSession>();
				clear_stored_session();
				String desc = data.get("error_description", data.get("error", vformat("Session refresh failed (HTTP %d).", p_code)));
				_fail(AUTH_ERROR_REFRESH_FAILED, desc);
				return;
			}
			String prev_refresh = session.is_valid() ? session->get_refresh_token() : String();
			_handle_token_response(data, String(), prev_refresh, true);
		} break;

		case HTTP_USERINFO: {
			Ref<AuthentikSession> target = pending_session.is_valid() ? pending_session : session;
			bool was_login_enrichment = pending_session.is_valid();
			pending_session = Ref<AuthentikSession>();

			if (p_code >= 200 && p_code < 300 && !data.is_empty() && target.is_valid()) {
				target->apply_userinfo_claims(data);
				emit_signal("userinfo_received", data);
			}

			if (was_login_enrichment && target.is_valid()) {
				_finalize_authenticated_session(target, false);
			} else if (session.is_valid()) {
				if (persist_session) {
					save_session();
				}
				pending_op = OP_NONE;
				_set_status(STATUS_AUTHENTICATED);
			}
		} break;

		case HTTP_REVOKE:
		case HTTP_NONE: {
			// Best-effort cleanup complete.
		} break;
	}
}

void AuthentikClient::_handle_token_response(
		const Dictionary &p_data,
		const String &p_expected_nonce,
		const String &p_prev_refresh,
		bool p_is_refresh) {
	int64_t now_unix = OS::get_singleton() ? int64_t(OS::get_singleton()->get_unix_time()) : 0;
	String expected_iss = !endpoints.issuer.is_empty() ? endpoints.issuer : issuer_url;

	String token_err;
	Ref<AuthentikSession> built = AuthentikSession::create_from_token_response(
			p_data,
			expected_iss,
			client_id,
			p_expected_nonce,
			p_prev_refresh,
			now_unix,
			token_err);
	if (built.is_null()) {
		_fail(AUTH_ERROR_INVALID_TOKEN, token_err);
		return;
	}

	// OIDC Core 1.0 Section 12.2 allows refresh responses to omit id_token or UserInfo-only claims.
	// Carry forward prior UserInfo profile/group/entitlement claims for the same subject on refresh.
	if (p_is_refresh && session.is_valid() && (built->get_subject() == session->get_subject() || built->get_subject() == "authenticated-user")) {
		Dictionary carry;
		Dictionary prev_claims = session->get_claims();
		Dictionary new_claims = built->get_claims();
		Array prev_keys = prev_claims.keys();
		for (int i = 0; i < prev_keys.size(); i++) {
			String k = prev_keys[i];
			if (k == "exp" || k == "iat" || k == "nbf" || k == "jti" || k == "nonce" || k == "at_hash") {
				continue;
			}
			if (!new_claims.has(k)) {
				carry[k] = prev_claims[k];
			}
		}
		if (session->get_groups().size() > built->get_groups().size()) {
			carry["groups"] = session->get_groups();
		}
		if (session->get_entitlements().size() > built->get_entitlements().size()) {
			carry["entitlements"] = session->get_entitlements();
		}
		if (!carry.is_empty()) {
			built->apply_userinfo_claims(carry);
		}
	}

	// Always enrich initial sign-ins via OIDC UserInfo when available so groups and entitlements
	// are populated even if the provider omits them from the ID token.
	if (!p_is_refresh && !endpoints.userinfo_endpoint.is_empty() && !built->get_access_token().is_empty()) {
		pending_session = built;
		Vector<String> headers;
		headers.push_back("Authorization: Bearer " + built->get_access_token());
		headers.push_back("Accept: application/json");
		_set_status(STATUS_FETCHING_USERINFO);
		if (_start_http_request(HTTP_USERINFO, HTTPClient::METHOD_GET, endpoints.userinfo_endpoint, headers) == OK) {
			return;
		}
		pending_session = Ref<AuthentikSession>();
	}

	_finalize_authenticated_session(built, p_is_refresh);
}

void AuthentikClient::_finalize_authenticated_session(const Ref<AuthentikSession> &p_session, bool p_is_refresh) {
	session = p_session;
	pending_op = OP_NONE;
	last_error = AUTH_ERROR_NONE;
	last_error_message = String();

	if (persist_session) {
		save_session();
	}

	_set_status(STATUS_AUTHENTICATED);
	if (p_is_refresh) {
		emit_signal("session_refreshed", session);
	}
	emit_signal("authenticated", session);
}

String AuthentikClient::_resolve_effective_storage_path() const {
	if (!storage_path.is_empty()) {
		return storage_path;
	}
	return "user://authentik_session.dat";
}

Error AuthentikClient::save_session() const {
	if (session.is_null()) {
		return ERR_UNAVAILABLE;
	}
	String path = _resolve_effective_storage_path();
	Vector<uint8_t> key = AuthentikOIDC::derive_storage_encryption_key(client_id);
	return AuthentikOIDC::save_encrypted_dictionary(path, session->to_storage_dictionary(), key);
}

Error AuthentikClient::load_session() {
	String path = _resolve_effective_storage_path();
	Vector<uint8_t> key = AuthentikOIDC::derive_storage_encryption_key(client_id);
	Dictionary dict;
	Error err = AuthentikOIDC::load_encrypted_dictionary(path, dict, key);
	if (err != OK) {
		return err;
	}

	Ref<AuthentikSession> loaded = AuthentikSession::from_storage_dictionary(dict);
	if (loaded.is_null()) {
		return ERR_INVALID_DATA;
	}

	// Verify stored session belongs to the currently configured provider when configured.
	if (!issuer_url.is_empty() && !loaded->get_issuer().is_empty() && !AuthentikOIDC::issuers_match(issuer_url, loaded->get_issuer())) {
		return ERR_INVALID_DATA;
	}
	if (!client_id.is_empty() && !loaded->get_client_id().is_empty() && loaded->get_client_id() != client_id) {
		return ERR_INVALID_DATA;
	}

	if (loaded->is_expired(0) && !loaded->has_refresh_token()) {
		clear_stored_session();
		return ERR_TIMEOUT;
	}

	session = loaded;
	if (!session->is_expired(0)) {
		_set_status(STATUS_AUTHENTICATED);
		emit_signal("authenticated", session);
	} else if (auto_refresh && is_configured() && session->has_refresh_token()) {
		refresh_session();
	}
	return OK;
}

void AuthentikClient::clear_stored_session() const {
	String path = _resolve_effective_storage_path();
	if (FileAccess::exists(path)) {
		Ref<DirAccess> da = DirAccess::create_for_path(path.get_base_dir());
		if (da.is_valid()) {
			da->remove(path);
		}
	}
}

void AuthentikClient::poll() {
	_poll_loopback_server();
	_poll_http();
	_poll_device_flow();

	if (auto_refresh && status == STATUS_AUTHENTICATED && http_purpose == HTTP_NONE && session.is_valid()) {
		if (session->is_expired(refresh_leeway_sec) && session->has_refresh_token() && is_configured()) {
			refresh_session();
		} else if (session->is_expired(0) && !session->has_refresh_token()) {
			session = Ref<AuthentikSession>();
			clear_stored_session();
			_set_status(STATUS_IDLE);
			emit_signal("logged_out");
		}
	}
}

void AuthentikClient::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_issuer_url", "issuer_url"), &AuthentikClient::set_issuer_url);
	ClassDB::bind_method(D_METHOD("get_issuer_url"), &AuthentikClient::get_issuer_url);

	ClassDB::bind_method(D_METHOD("set_client_id", "client_id"), &AuthentikClient::set_client_id);
	ClassDB::bind_method(D_METHOD("get_client_id"), &AuthentikClient::get_client_id);

	ClassDB::bind_method(D_METHOD("set_scopes", "scopes"), &AuthentikClient::set_scopes);
	ClassDB::bind_method(D_METHOD("get_scopes"), &AuthentikClient::get_scopes);

	ClassDB::bind_method(D_METHOD("set_redirect_port", "port"), &AuthentikClient::set_redirect_port);
	ClassDB::bind_method(D_METHOD("get_redirect_port"), &AuthentikClient::get_redirect_port);

	ClassDB::bind_method(D_METHOD("set_redirect_uri", "redirect_uri"), &AuthentikClient::set_redirect_uri);
	ClassDB::bind_method(D_METHOD("get_redirect_uri"), &AuthentikClient::get_redirect_uri);

	ClassDB::bind_method(D_METHOD("set_persist_session", "enable"), &AuthentikClient::set_persist_session);
	ClassDB::bind_method(D_METHOD("is_persist_session_enabled"), &AuthentikClient::is_persist_session_enabled);

	ClassDB::bind_method(D_METHOD("set_auto_refresh", "enable"), &AuthentikClient::set_auto_refresh);
	ClassDB::bind_method(D_METHOD("is_auto_refresh_enabled"), &AuthentikClient::is_auto_refresh_enabled);

	ClassDB::bind_method(D_METHOD("set_storage_path", "path"), &AuthentikClient::set_storage_path);
	ClassDB::bind_method(D_METHOD("get_storage_path"), &AuthentikClient::get_storage_path);

	ClassDB::bind_method(D_METHOD("set_tls_verify", "verify"), &AuthentikClient::set_tls_verify);
	ClassDB::bind_method(D_METHOD("is_tls_verify_enabled"), &AuthentikClient::is_tls_verify_enabled);

	ClassDB::bind_method(D_METHOD("set_login_timeout_sec", "seconds"), &AuthentikClient::set_login_timeout_sec);
	ClassDB::bind_method(D_METHOD("get_login_timeout_sec"), &AuthentikClient::get_login_timeout_sec);

	ClassDB::bind_method(D_METHOD("set_refresh_leeway_sec", "seconds"), &AuthentikClient::set_refresh_leeway_sec);
	ClassDB::bind_method(D_METHOD("get_refresh_leeway_sec"), &AuthentikClient::get_refresh_leeway_sec);

	ClassDB::bind_method(D_METHOD("is_configured"), &AuthentikClient::is_configured);
	ClassDB::bind_method(D_METHOD("get_status"), &AuthentikClient::get_status);
	ClassDB::bind_method(D_METHOD("get_last_error"), &AuthentikClient::get_last_error);
	ClassDB::bind_method(D_METHOD("get_last_error_message"), &AuthentikClient::get_last_error_message);
	ClassDB::bind_method(D_METHOD("get_last_authorization_url"), &AuthentikClient::get_last_authorization_url);
	ClassDB::bind_method(D_METHOD("get_user_code"), &AuthentikClient::get_user_code);
	ClassDB::bind_method(D_METHOD("get_verification_uri"), &AuthentikClient::get_verification_uri);
	ClassDB::bind_method(D_METHOD("get_verification_uri_complete"), &AuthentikClient::get_verification_uri_complete);
	ClassDB::bind_method(D_METHOD("get_endpoints"), &AuthentikClient::get_endpoints);

	ClassDB::bind_method(D_METHOD("is_authenticated"), &AuthentikClient::is_authenticated);
	ClassDB::bind_method(D_METHOD("is_busy"), &AuthentikClient::is_busy);
	ClassDB::bind_method(D_METHOD("get_session"), &AuthentikClient::get_session);
	ClassDB::bind_method(D_METHOD("get_access_token"), &AuthentikClient::get_access_token);
	ClassDB::bind_method(D_METHOD("get_bearer_token"), &AuthentikClient::get_bearer_token);
	ClassDB::bind_method(D_METHOD("get_authorization_header"), &AuthentikClient::get_authorization_header);
	ClassDB::bind_method(D_METHOD("has_group", "group"), &AuthentikClient::has_group);
	ClassDB::bind_method(D_METHOD("has_any_group", "groups"), &AuthentikClient::has_any_group);
	ClassDB::bind_method(D_METHOD("has_entitlement", "entitlement"), &AuthentikClient::has_entitlement);

	ClassDB::bind_method(D_METHOD("login_interactive", "open_browser"), &AuthentikClient::login_interactive, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("login_device_code", "open_browser"), &AuthentikClient::login_device_code, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("complete_authorization_callback", "callback_url_or_query"), &AuthentikClient::complete_authorization_callback);
	ClassDB::bind_method(D_METHOD("cancel_login"), &AuthentikClient::cancel_login);

	ClassDB::bind_method(D_METHOD("refresh_session"), &AuthentikClient::refresh_session);
	ClassDB::bind_method(D_METHOD("fetch_userinfo"), &AuthentikClient::fetch_userinfo);
	ClassDB::bind_method(D_METHOD("logout", "revoke_remote", "open_end_session"), &AuthentikClient::logout, DEFVAL(false), DEFVAL(false));

	ClassDB::bind_method(D_METHOD("save_session"), &AuthentikClient::save_session);
	ClassDB::bind_method(D_METHOD("load_session"), &AuthentikClient::load_session);
	ClassDB::bind_method(D_METHOD("clear_stored_session"), &AuthentikClient::clear_stored_session);

	ClassDB::bind_method(D_METHOD("poll"), &AuthentikClient::poll);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "issuer_url"), "set_issuer_url", "get_issuer_url");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "client_id"), "set_client_id", "get_client_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "scopes"), "set_scopes", "get_scopes");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "redirect_port", PROPERTY_HINT_RANGE, "0,65535,1"), "set_redirect_port", "get_redirect_port");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "redirect_uri"), "set_redirect_uri", "get_redirect_uri");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "persist_session"), "set_persist_session", "is_persist_session_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_refresh"), "set_auto_refresh", "is_auto_refresh_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "storage_path"), "set_storage_path", "get_storage_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tls_verify"), "set_tls_verify", "is_tls_verify_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "login_timeout_sec", PROPERTY_HINT_RANGE, "10,1800,1"), "set_login_timeout_sec", "get_login_timeout_sec");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "refresh_leeway_sec", PROPERTY_HINT_RANGE, "0,600,1"), "set_refresh_leeway_sec", "get_refresh_leeway_sec");

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
	ADD_SIGNAL(MethodInfo("userinfo_received", PropertyInfo(Variant::DICTIONARY, "claims")));
	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "status")));

	BIND_ENUM_CONSTANT(STATUS_IDLE);
	BIND_ENUM_CONSTANT(STATUS_DISCOVERING);
	BIND_ENUM_CONSTANT(STATUS_AWAITING_BROWSER_CALLBACK);
	BIND_ENUM_CONSTANT(STATUS_AWAITING_DEVICE_CODE);
	BIND_ENUM_CONSTANT(STATUS_EXCHANGING_CODE);
	BIND_ENUM_CONSTANT(STATUS_FETCHING_USERINFO);
	BIND_ENUM_CONSTANT(STATUS_REFRESHING);
	BIND_ENUM_CONSTANT(STATUS_AUTHENTICATED);
	BIND_ENUM_CONSTANT(STATUS_ERROR);

	BIND_ENUM_CONSTANT(AUTH_ERROR_NONE);
	BIND_ENUM_CONSTANT(AUTH_ERROR_NOT_CONFIGURED);
	BIND_ENUM_CONSTANT(AUTH_ERROR_DISCOVERY_FAILED);
	BIND_ENUM_CONSTANT(AUTH_ERROR_LOOPBACK_BIND_FAILED);
	BIND_ENUM_CONSTANT(AUTH_ERROR_BROWSER_OPEN_FAILED);
	BIND_ENUM_CONSTANT(AUTH_ERROR_STATE_MISMATCH);
	BIND_ENUM_CONSTANT(AUTH_ERROR_PROVIDER_DENIED);
	BIND_ENUM_CONSTANT(AUTH_ERROR_TOKEN_EXCHANGE_FAILED);
	BIND_ENUM_CONSTANT(AUTH_ERROR_INVALID_TOKEN);
	BIND_ENUM_CONSTANT(AUTH_ERROR_REFRESH_FAILED);
	BIND_ENUM_CONSTANT(AUTH_ERROR_TIMEOUT);
	BIND_ENUM_CONSTANT(AUTH_ERROR_NETWORK);
	BIND_ENUM_CONSTANT(AUTH_ERROR_CANCELLED);
}
