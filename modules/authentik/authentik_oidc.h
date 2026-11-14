/**************************************************************************/
/*  authentik_oidc.h                                                      */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

struct AuthentikEndpoints {
	String issuer;
	String discovery_url;
	String authorization_endpoint;
	String token_endpoint;
	String userinfo_endpoint;
	String device_authorization_endpoint;
	String revocation_endpoint;
	String end_session_endpoint;

	bool is_valid() const {
		return !authorization_endpoint.is_empty() && !token_endpoint.is_empty();
	}

	Dictionary to_dictionary() const;
	static AuthentikEndpoints from_dictionary(const Dictionary &p_dict);
};

class AuthentikOIDC {
public:
	static String base64url_encode(const PackedByteArray &p_bytes);
	static Error base64url_decode(const String &p_input, PackedByteArray &r_bytes);

	static Error generate_random_token(int p_byte_length, String &r_token);
	static String compute_pkce_s256_challenge(const String &p_code_verifier);
	static bool constant_time_equals(const String &p_a, const String &p_b);

	static bool is_jwt_format(const String &p_token);
	static Error decode_jwt_payload(const String &p_jwt, Dictionary &r_claims);

	static String normalize_issuer_url(const String &p_issuer_url);
	static bool issuers_match(const String &p_expected, const String &p_actual);
	static bool audience_matches(const Variant &p_aud_claim, const String &p_client_id);

	static Error derive_authentik_endpoints(const String &p_issuer_url, AuthentikEndpoints &r_endpoints);
	static void merge_discovery_document(const Dictionary &p_doc, AuthentikEndpoints &r_endpoints);

	static String percent_encode_component(const String &p_value);
	static String form_urlencode(const Dictionary &p_params);
	static Dictionary parse_query_string(const String &p_query);
	static String build_url_with_query(const String &p_base_url, const Dictionary &p_params);

	static Error parse_http_get_request(const String &p_raw_request, String &r_path_only, Dictionary &r_query);
	static String build_loopback_http_response(bool p_success, const String &p_title, const String &p_message);

	static Vector<uint8_t> derive_storage_encryption_key(const String &p_context_salt = String());
	static Error save_encrypted_dictionary(const String &p_path, const Dictionary &p_data, const Vector<uint8_t> &p_key);
	static Error load_encrypted_dictionary(const String &p_path, Dictionary &r_data, const Vector<uint8_t> &p_key);
};

