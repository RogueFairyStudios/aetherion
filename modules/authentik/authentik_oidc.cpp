/**************************************************************************/
/*  authentik_oidc.cpp                                                    */
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

#include "authentik_oidc.h"

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_encrypted.h"
#include "core/io/json.h"
#include "core/os/os.h"

Dictionary AuthentikEndpoints::to_dictionary() const {
	Dictionary d;
	d["issuer"] = issuer;
	d["discovery_url"] = discovery_url;
	d["authorization_endpoint"] = authorization_endpoint;
	d["token_endpoint"] = token_endpoint;
	d["userinfo_endpoint"] = userinfo_endpoint;
	d["device_authorization_endpoint"] = device_authorization_endpoint;
	d["revocation_endpoint"] = revocation_endpoint;
	d["end_session_endpoint"] = end_session_endpoint;
	return d;
}

AuthentikEndpoints AuthentikEndpoints::from_dictionary(const Dictionary &p_dict) {
	AuthentikEndpoints ep;
	ep.issuer = p_dict.get("issuer", String());
	ep.discovery_url = p_dict.get("discovery_url", String());
	ep.authorization_endpoint = p_dict.get("authorization_endpoint", String());
	ep.token_endpoint = p_dict.get("token_endpoint", String());
	ep.userinfo_endpoint = p_dict.get("userinfo_endpoint", String());
	ep.device_authorization_endpoint = p_dict.get("device_authorization_endpoint", String());
	ep.revocation_endpoint = p_dict.get("revocation_endpoint", String());
	ep.end_session_endpoint = p_dict.get("end_session_endpoint", String());
	return ep;
}

String AuthentikOIDC::base64url_encode(const PackedByteArray &p_bytes) {
	if (p_bytes.is_empty()) {
		return String();
	}
	String b64 = CryptoCore::b64_encode_str(p_bytes.ptr(), p_bytes.size());
	b64 = b64.replace_char('+', '-').replace_char('/', '_');
	while (b64.ends_with("=")) {
		b64 = b64.substr(0, b64.length() - 1);
	}
	return b64;
}

Error AuthentikOIDC::base64url_decode(const String &p_input, PackedByteArray &r_bytes) {
	r_bytes.clear();
	if (p_input.is_empty()) {
		return OK;
	}
	String b64 = p_input.strip_edges().replace_char('-', '+').replace_char('_', '/');
	int mod = b64.length() % 4;
	if (mod == 1) {
		return ERR_INVALID_DATA;
	}
	if (mod > 0) {
		b64 += String("=").repeat(4 - mod);
	}

	CharString ascii = b64.ascii();
	size_t max_len = (ascii.length() / 4) * 3 + 4;
	r_bytes.resize(max_len);
	size_t actual_len = 0;
	Error err = CryptoCore::b64_decode(r_bytes.ptrw(), r_bytes.size(), &actual_len, (const uint8_t *)ascii.get_data(), ascii.length());
	if (err != OK) {
		r_bytes.clear();
		return err;
	}
	r_bytes.resize(actual_len);
	return OK;
}

Error AuthentikOIDC::generate_random_token(int p_byte_length, String &r_token) {
	ERR_FAIL_COND_V(p_byte_length <= 0 || p_byte_length > 512, ERR_INVALID_PARAMETER);

	PackedByteArray bytes;
	bytes.resize(p_byte_length);
	Error err = CryptoCore::generate_random(bytes.ptrw(), size_t(p_byte_length));
	ERR_FAIL_COND_V(err != OK, err);

	r_token = base64url_encode(bytes);
	return OK;
}

String AuthentikOIDC::compute_pkce_s256_challenge(const String &p_code_verifier) {
	CharString utf8 = p_code_verifier.utf8();
	unsigned char digest[32];
	Error err = CryptoCore::sha256((const uint8_t *)utf8.get_data(), utf8.length(), digest);
	ERR_FAIL_COND_V(err != OK, String());

	PackedByteArray hash_bytes;
	hash_bytes.resize(32);
	memcpy(hash_bytes.ptrw(), digest, 32);
	return base64url_encode(hash_bytes);
}

bool AuthentikOIDC::constant_time_equals(const String &p_a, const String &p_b) {
	CharString a = p_a.utf8();
	CharString b = p_b.utf8();
	const int len_a = a.length();
	const int len_b = b.length();
	uint8_t diff = (len_a == len_b) ? 0 : 1;
	const int max_len = MAX(len_a, len_b);
	for (int i = 0; i < max_len; i++) {
		uint8_t ca = (i < len_a) ? (uint8_t)a[i] : 0;
		uint8_t cb = (i < len_b) ? (uint8_t)b[i] : 0;
		diff |= (ca ^ cb);
	}
	return diff == 0;
}

bool AuthentikOIDC::is_jwt_format(const String &p_token) {
	int first_dot = p_token.find_char('.');
	if (first_dot <= 0) {
		return false;
	}
	int second_dot = p_token.find_char('.', first_dot + 1);
	if (second_dot <= first_dot + 1) {
		return false;
	}
	return p_token.find_char('.', second_dot + 1) == -1;
}

Error AuthentikOIDC::decode_jwt_payload(const String &p_jwt, Dictionary &r_claims) {
	r_claims.clear();
	Vector<String> parts = p_jwt.split(".");
	if (parts.size() != 3 || parts[1].is_empty()) {
		return ERR_INVALID_DATA;
	}

	PackedByteArray payload_bytes;
	Error err = base64url_decode(parts[1], payload_bytes);
	if (err != OK) {
		return err;
	}

	String json_str;
	if (json_str.append_utf8((const char *)payload_bytes.ptr(), payload_bytes.size()) != OK) {
		return ERR_PARSE_ERROR;
	}

	JSON json;
	err = json.parse(json_str);
	if (err != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		return ERR_PARSE_ERROR;
	}

	r_claims = json.get_data();
	return OK;
}

String AuthentikOIDC::normalize_issuer_url(const String &p_issuer_url) {
	String trimmed = p_issuer_url.strip_edges();
	while (trimmed.ends_with("/")) {
		trimmed = trimmed.substr(0, trimmed.length() - 1);
	}
	return trimmed;
}

bool AuthentikOIDC::issuers_match(const String &p_expected, const String &p_actual) {
	String a = normalize_issuer_url(p_expected);
	String b = normalize_issuer_url(p_actual);
	if (a.is_empty() || b.is_empty()) {
		return false;
	}
	return a == b;
}

bool AuthentikOIDC::audience_matches(const Variant &p_aud_claim, const String &p_client_id) {
	if (p_client_id.is_empty()) {
		return false;
	}
	if (p_aud_claim.get_type() == Variant::STRING) {
		return String(p_aud_claim) == p_client_id;
	}
	if (p_aud_claim.get_type() == Variant::ARRAY) {
		Array arr = p_aud_claim;
		for (int i = 0; i < arr.size(); i++) {
			if (arr[i].get_type() == Variant::STRING && String(arr[i]) == p_client_id) {
				return true;
			}
		}
	}
	return false;
}

Error AuthentikOIDC::derive_authentik_endpoints(const String &p_issuer_url, AuthentikEndpoints &r_endpoints) {
	String normalized = normalize_issuer_url(p_issuer_url);
	if (normalized.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}

	String scheme;
	String host;
	int port = 0;
	String path;
	String fragment;
	Error err = normalized.parse_url(scheme, host, port, path, fragment);
	if (err != OK || (scheme != "https://" && scheme != "http://") || host.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}

	String origin = scheme + host;
	if (port > 0) {
		bool is_default_port = (scheme == "https://" && port == 443) || (scheme == "http://" && port == 80);
		if (!is_default_port) {
			origin += ":" + itos(port);
		}
	}

	String base_with_path = origin + path;
	r_endpoints.issuer = base_with_path + "/";
	r_endpoints.discovery_url = base_with_path + "/.well-known/openid-configuration";

	// Authentik canonical provider paths live under /application/o/...
	int app_idx = path.find("/application/o/");
	if (app_idx != -1) {
		String prefix = origin + path.substr(0, app_idx);
		r_endpoints.authorization_endpoint = prefix + "/application/o/authorize/";
		r_endpoints.token_endpoint = prefix + "/application/o/token/";
		r_endpoints.userinfo_endpoint = prefix + "/application/o/userinfo/";
		r_endpoints.device_authorization_endpoint = prefix + "/application/o/device/";
		r_endpoints.revocation_endpoint = prefix + "/application/o/revoke/";
		r_endpoints.end_session_endpoint = base_with_path + "/end-session/";
	} else {
		// Generic OIDC provider: endpoints will be populated from discovery.
		r_endpoints.authorization_endpoint = String();
		r_endpoints.token_endpoint = String();
		r_endpoints.userinfo_endpoint = String();
		r_endpoints.device_authorization_endpoint = String();
		r_endpoints.revocation_endpoint = String();
		r_endpoints.end_session_endpoint = String();
	}

	return OK;
}

void AuthentikOIDC::merge_discovery_document(const Dictionary &p_doc, AuthentikEndpoints &r_endpoints) {
	if (p_doc.has("issuer") && p_doc["issuer"].get_type() == Variant::STRING) {
		String v = String(p_doc["issuer"]).strip_edges();
		if (!v.is_empty()) {
			r_endpoints.issuer = v;
		}
	}
	if (p_doc.has("authorization_endpoint") && p_doc["authorization_endpoint"].get_type() == Variant::STRING) {
		String v = String(p_doc["authorization_endpoint"]).strip_edges();
		if (!v.is_empty()) {
			r_endpoints.authorization_endpoint = v;
		}
	}
	if (p_doc.has("token_endpoint") && p_doc["token_endpoint"].get_type() == Variant::STRING) {
		String v = String(p_doc["token_endpoint"]).strip_edges();
		if (!v.is_empty()) {
			r_endpoints.token_endpoint = v;
		}
	}
	if (p_doc.has("userinfo_endpoint") && p_doc["userinfo_endpoint"].get_type() == Variant::STRING) {
		String v = String(p_doc["userinfo_endpoint"]).strip_edges();
		if (!v.is_empty()) {
			r_endpoints.userinfo_endpoint = v;
		}
	}
	if (p_doc.has("device_authorization_endpoint") && p_doc["device_authorization_endpoint"].get_type() == Variant::STRING) {
		String v = String(p_doc["device_authorization_endpoint"]).strip_edges();
		if (!v.is_empty()) {
			r_endpoints.device_authorization_endpoint = v;
		}
	}
	if (p_doc.has("revocation_endpoint") && p_doc["revocation_endpoint"].get_type() == Variant::STRING) {
		String v = String(p_doc["revocation_endpoint"]).strip_edges();
		if (!v.is_empty()) {
			r_endpoints.revocation_endpoint = v;
		}
	}
	if (p_doc.has("end_session_endpoint") && p_doc["end_session_endpoint"].get_type() == Variant::STRING) {
		String v = String(p_doc["end_session_endpoint"]).strip_edges();
		if (!v.is_empty()) {
			r_endpoints.end_session_endpoint = v;
		}
	}
}

String AuthentikOIDC::percent_encode_component(const String &p_value) {
	CharString utf8 = p_value.utf8();
	String out;
	static const char hex_chars[] = "0123456789ABCDEF";
	for (int i = 0; i < utf8.length(); i++) {
		uint8_t c = (uint8_t)utf8[i];
		bool unreserved = (c >= 'A' && c <= 'Z') ||
				(c >= 'a' && c <= 'z') ||
				(c >= '0' && c <= '9') ||
				c == '-' || c == '.' || c == '_' || c == '~';
		if (unreserved) {
			out += char32_t(c);
		} else {
			out += '%';
			out += char32_t(hex_chars[(c >> 4) & 0x0F]);
			out += char32_t(hex_chars[c & 0x0F]);
		}
	}
	return out;
}

String AuthentikOIDC::form_urlencode(const Dictionary &p_params) {
	String result;
	Array keys = p_params.keys();
	bool first = true;
	for (int i = 0; i < keys.size(); i++) {
		String key = String(keys[i]);
		Variant val = p_params[keys[i]];
		if (val.get_type() == Variant::NIL) {
			continue;
		}
		if (!first) {
			result += "&";
		}
		first = false;
		result += percent_encode_component(key) + "=" + percent_encode_component(String(val));
	}
	return result;
}

Dictionary AuthentikOIDC::parse_query_string(const String &p_query) {
	Dictionary out;
	String query = p_query;
	if (query.begins_with("?")) {
		query = query.substr(1);
	}
	int frag = query.find_char('#');
	if (frag != -1) {
		query = query.substr(0, frag);
	}
	if (query.is_empty()) {
		return out;
	}

	Vector<String> pairs = query.split("&", false);
	for (int i = 0; i < pairs.size(); i++) {
		const String &pair = pairs[i];
		int eq = pair.find_char('=');
		String raw_key = (eq == -1) ? pair : pair.substr(0, eq);
		String raw_val = (eq == -1) ? String() : pair.substr(eq + 1);
		String decoded_key = raw_key.replace_char('+', ' ').uri_decode();
		String decoded_val = raw_val.replace_char('+', ' ').uri_decode();
		if (!decoded_key.is_empty()) {
			out[decoded_key] = decoded_val;
		}
	}
	return out;
}

String AuthentikOIDC::build_url_with_query(const String &p_base_url, const Dictionary &p_params) {
	String encoded = form_urlencode(p_params);
	if (encoded.is_empty()) {
		return p_base_url;
	}
	if (p_base_url.contains_char('?')) {
		return p_base_url + "&" + encoded;
	}
	return p_base_url + "?" + encoded;
}

Error AuthentikOIDC::parse_http_get_request(const String &p_raw_request, String &r_path_only, Dictionary &r_query) {
	r_path_only = String();
	r_query.clear();

	int line_end = p_raw_request.find("\r\n");
	if (line_end == -1) {
		line_end = p_raw_request.find_char('\n');
	}
	if (line_end <= 0 || line_end > 8192) {
		return ERR_INVALID_DATA;
	}

	String request_line = p_raw_request.substr(0, line_end).strip_edges();
	Vector<String> tokens = request_line.split(" ", false);
	if (tokens.size() < 2) {
		return ERR_INVALID_DATA;
	}
	if (tokens[0].to_upper() != "GET") {
		return ERR_METHOD_NOT_FOUND;
	}

	String target = tokens[1];
	if (!target.begins_with("/")) {
		return ERR_INVALID_DATA;
	}

	int qpos = target.find_char('?');
	if (qpos == -1) {
		r_path_only = target;
	} else {
		r_path_only = target.substr(0, qpos);
		r_query = parse_query_string(target.substr(qpos + 1));
	}
	return OK;
}

static String _html_escape(const String &p_text) {
	return p_text
			.replace("&", "&amp;")
			.replace("<", "&lt;")
			.replace(">", "&gt;")
			.replace("\"", "&quot;")
			.replace("'", "&#39;");
}

String AuthentikOIDC::build_loopback_http_response(bool p_success, const String &p_title, const String &p_message) {
	const String status_line = p_success ? "HTTP/1.1 200 OK" : "HTTP/1.1 400 Bad Request";
	const String accent = p_success ? "#4ade80" : "#f87171";
	const String body = String("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">") +
			"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" +
			"<title>" + _html_escape(p_title) + "</title>" +
			"<style>body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;" +
			"background:#14161b;color:#e5e7eb;font-family:system-ui,-apple-system,sans-serif;}" +
			".card{max-width:440px;padding:32px;border-radius:12px;background:#1e222b;" +
			"border:1px solid #2e3442;box-shadow:0 16px 40px rgba(0,0,0,.45);text-align:center;}" +
			".dot{width:12px;height:12px;border-radius:50%;display:inline-block;margin-bottom:14px;background:" + accent + ";}" +
			"h1{font-size:1.25rem;margin:0 0 10px;}p{margin:0;color:#9ca3af;line-height:1.5;font-size:.95rem;}</style>" +
			"</head><body><div class=\"card\"><span class=\"dot\"></span><h1>" + _html_escape(p_title) +
			"</h1><p>" + _html_escape(p_message) + "</p></div></body></html>";

	CharString body_utf8 = body.utf8();
	String headers = status_line + "\r\n" +
			"Content-Type: text/html; charset=utf-8\r\n" +
			"Content-Length: " + itos(body_utf8.length()) + "\r\n" +
			"Cache-Control: no-store, no-cache, must-revalidate\r\n" +
			"Pragma: no-cache\r\n" +
			"X-Content-Type-Options: nosniff\r\n" +
			"Connection: close\r\n\r\n";
	return headers + body;
}

Vector<uint8_t> AuthentikOIDC::derive_storage_encryption_key(const String &p_context_salt) {
	String material = "aetherion-authentik-session-v1:";
	if (OS::get_singleton()) {
		String env_key = OS::get_singleton()->get_environment("AETHERION_AUTHENTIK_STORAGE_KEY");
		if (!env_key.is_empty()) {
			material += env_key;
		} else {
			material += OS::get_singleton()->get_unique_id() + "|" + OS::get_singleton()->get_config_path();
		}
	}
	if (!p_context_salt.is_empty()) {
		material += "|" + p_context_salt;
	}

	CharString utf8 = material.utf8();
	unsigned char digest[32];
	CryptoCore::sha256((const uint8_t *)utf8.get_data(), utf8.length(), digest);

	Vector<uint8_t> key;
	key.resize(32);
	memcpy(key.ptrw(), digest, 32);
	return key;
}

Error AuthentikOIDC::save_encrypted_dictionary(const String &p_path, const Dictionary &p_data, const Vector<uint8_t> &p_key) {
	ERR_FAIL_COND_V(p_path.is_empty(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_key.size() != 32, ERR_INVALID_PARAMETER);

	String dir = p_path.get_base_dir();
	if (!dir.is_empty()) {
		Ref<DirAccess> da = DirAccess::create_for_path(dir);
		if (da.is_valid() && !da->dir_exists(dir)) {
			da->make_dir_recursive(dir);
		}
	}

	Ref<FileAccess> base = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_V(base.is_null(), ERR_CANT_OPEN);

	Ref<FileAccessEncrypted> enc;
	enc.instantiate();
	Error err = enc->open_and_parse(base, p_key, FileAccessEncrypted::MODE_WRITE_AES256);
	ERR_FAIL_COND_V(err != OK, err);

	String payload = JSON::stringify(p_data);
	enc->store_string(payload);
	enc->flush();
	enc = Ref<FileAccessEncrypted>();
	base = Ref<FileAccess>();

#ifdef UNIX_ENABLED
	FileAccess::set_unix_permissions(p_path, FileAccess::UNIX_READ_OWNER | FileAccess::UNIX_WRITE_OWNER);
#endif
	return OK;
}

Error AuthentikOIDC::load_encrypted_dictionary(const String &p_path, Dictionary &r_data, const Vector<uint8_t> &p_key) {
	r_data.clear();
	ERR_FAIL_COND_V(p_path.is_empty(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_key.size() != 32, ERR_INVALID_PARAMETER);

	if (!FileAccess::exists(p_path)) {
		return ERR_FILE_NOT_FOUND;
	}

	Ref<FileAccess> base = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V(base.is_null(), ERR_CANT_OPEN);

	Ref<FileAccessEncrypted> enc;
	enc.instantiate();
	Error err = enc->open_and_parse(base, p_key, FileAccessEncrypted::MODE_READ);
	if (err != OK) {
		return err;
	}

	uint64_t len = enc->get_length();
	PackedByteArray buf;
	buf.resize(len);
	if (len > 0) {
		enc->get_buffer(buf.ptrw(), len);
	}

	String json_str;
	if (json_str.append_utf8((const char *)buf.ptr(), buf.size()) != OK) {
		return ERR_PARSE_ERROR;
	}

	JSON json;
	err = json.parse(json_str);
	if (err != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		return ERR_PARSE_ERROR;
	}

	r_data = json.get_data();
	return OK;
}

