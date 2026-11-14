/**************************************************************************/
/*  authentik_client.h                                                    */
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

#include "authentik_oidc.h"
#include "authentik_session.h"

#include "core/io/http_client.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/object/ref_counted.h"

class AuthentikClient : public RefCounted {
	GDCLASS(AuthentikClient, RefCounted);

public:
	enum Status {
		STATUS_IDLE = 0,
		STATUS_DISCOVERING = 1,
		STATUS_AWAITING_BROWSER_CALLBACK = 2,
		STATUS_AWAITING_DEVICE_CODE = 3,
		STATUS_EXCHANGING_CODE = 4,
		STATUS_FETCHING_USERINFO = 5,
		STATUS_REFRESHING = 6,
		STATUS_AUTHENTICATED = 7,
		STATUS_ERROR = 8,
	};

	enum AuthError {
		AUTH_ERROR_NONE = 0,
		AUTH_ERROR_NOT_CONFIGURED = 1,
		AUTH_ERROR_DISCOVERY_FAILED = 2,
		AUTH_ERROR_LOOPBACK_BIND_FAILED = 3,
		AUTH_ERROR_BROWSER_OPEN_FAILED = 4,
		AUTH_ERROR_STATE_MISMATCH = 5,
		AUTH_ERROR_PROVIDER_DENIED = 6,
		AUTH_ERROR_TOKEN_EXCHANGE_FAILED = 7,
		AUTH_ERROR_INVALID_TOKEN = 8,
		AUTH_ERROR_REFRESH_FAILED = 9,
		AUTH_ERROR_TIMEOUT = 10,
		AUTH_ERROR_NETWORK = 11,
		AUTH_ERROR_CANCELLED = 12,
	};

private:
	enum PendingOperation {
		OP_NONE = 0,
		OP_LOGIN_PKCE = 1,
		OP_LOGIN_DEVICE = 2,
		OP_REFRESH = 3,
		OP_USERINFO = 4,
		OP_REVOKE = 5,
	};

	enum HttpPurpose {
		HTTP_NONE = 0,
		HTTP_DISCOVERY = 1,
		HTTP_TOKEN_EXCHANGE = 2,
		HTTP_DEVICE_AUTHORIZE = 3,
		HTTP_DEVICE_POLL = 4,
		HTTP_TOKEN_REFRESH = 5,
		HTTP_USERINFO = 6,
		HTTP_REVOKE = 7,
	};

	struct LoopbackConn {
		Ref<StreamPeerTCP> peer;
		String buffer;
		uint64_t accepted_msec = 0;
	};

	String issuer_url;
	String client_id;
	String scopes = "openid profile email offline_access";
	int redirect_port = 0;
	String redirect_uri;
	bool persist_session = true;
	bool auto_refresh = true;
	String storage_path;
	bool tls_verify = true;
	int login_timeout_sec = 300;
	int refresh_leeway_sec = 60;

	Status status = STATUS_IDLE;
	AuthError last_error = AUTH_ERROR_NONE;
	String last_error_message;

	AuthentikEndpoints endpoints;
	String discovered_for_issuer;
	bool discovery_loaded = false;

	Ref<AuthentikSession> session;
	Ref<AuthentikSession> pending_session;

	// PKCE & callback state
	PendingOperation pending_op = OP_NONE;
	bool pending_open_browser = true;
	String pkce_state;
	String pkce_nonce;
	String pkce_verifier;
	String active_redirect_uri;
	String active_callback_path = "/callback";
	String last_authorization_url;
	uint64_t operation_started_msec = 0;

	Ref<TCPServer> tcp_server;
	Vector<LoopbackConn> loopback_connections;

	// Device flow state
	String device_code;
	String user_code;
	String verification_uri;
	String verification_uri_complete;
	int device_poll_interval_sec = 5;
	uint64_t next_device_poll_msec = 0;
	uint64_t device_expires_msec = 0;

	// Active HTTP request state
	HttpPurpose http_purpose = HTTP_NONE;
	Ref<HTTPClient> http_client;
	HTTPClient::Method http_method = HTTPClient::METHOD_GET;
	String http_request_path;
	Vector<String> http_headers;
	CharString http_body;
	PackedByteArray http_response_body;
	int http_response_code = 0;
	List<String> http_response_headers;
	bool http_request_dispatched = false;
	int http_redirects = 0;
	uint64_t http_started_msec = 0;

	bool scene_tree_connected = false;

	void _set_status(Status p_status);
	void _fail(AuthError p_error, const String &p_message);
	void _update_auto_poll_connection();
	void _on_process_frame();

	Error _start_http_request(HttpPurpose p_purpose, HTTPClient::Method p_method, const String &p_url, const Vector<String> &p_headers, const String &p_body = String(), int p_redirect_count = 0);
	void _close_http();
	void _poll_http();
	void _handle_http_complete(HttpPurpose p_purpose, int p_code, const String &p_body_text);

	Error _ensure_endpoints_initialized();
	void _continue_pending_after_discovery();
	Error _begin_pkce_authorization();
	Error _begin_device_authorization();
	void _poll_loopback_server();
	void _stop_loopback_server();
	void _process_callback_query(const Dictionary &p_query);
	void _poll_device_flow();
	void _handle_token_response(const Dictionary &p_data, const String &p_expected_nonce, const String &p_prev_refresh, bool p_is_refresh);
	void _finalize_authenticated_session(const Ref<AuthentikSession> &p_session, bool p_is_refresh);

	String _resolve_effective_storage_path() const;

protected:
	static void _bind_methods();

public:
	void set_issuer_url(const String &p_issuer_url);
	String get_issuer_url() const { return issuer_url; }

	void set_client_id(const String &p_client_id);
	String get_client_id() const { return client_id; }

	void set_scopes(const String &p_scopes);
	String get_scopes() const { return scopes; }

	void set_redirect_port(int p_port);
	int get_redirect_port() const { return redirect_port; }

	void set_redirect_uri(const String &p_uri);
	String get_redirect_uri() const { return redirect_uri; }

	void set_persist_session(bool p_enable);
	bool is_persist_session_enabled() const { return persist_session; }

	void set_auto_refresh(bool p_enable);
	bool is_auto_refresh_enabled() const { return auto_refresh; }

	void set_storage_path(const String &p_path);
	String get_storage_path() const { return storage_path; }

	void set_tls_verify(bool p_verify);
	bool is_tls_verify_enabled() const { return tls_verify; }

	void set_login_timeout_sec(int p_seconds);
	int get_login_timeout_sec() const { return login_timeout_sec; }

	void set_refresh_leeway_sec(int p_seconds);
	int get_refresh_leeway_sec() const { return refresh_leeway_sec; }

	bool is_configured() const;
	Status get_status() const { return status; }
	AuthError get_last_error() const { return last_error; }
	String get_last_error_message() const { return last_error_message; }
	String get_last_authorization_url() const { return last_authorization_url; }
	String get_user_code() const { return user_code; }
	String get_verification_uri() const { return verification_uri; }
	String get_verification_uri_complete() const { return verification_uri_complete; }
	Dictionary get_endpoints() const { return endpoints.to_dictionary(); }

	bool is_authenticated() const;
	bool is_busy() const;
	Ref<AuthentikSession> get_session() const { return session; }
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

	Error save_session() const;
	Error load_session();
	void clear_stored_session() const;

	void poll();

	AuthentikClient() {}
	~AuthentikClient() override;
};

VARIANT_ENUM_CAST(AuthentikClient::Status);
VARIANT_ENUM_CAST(AuthentikClient::AuthError);

