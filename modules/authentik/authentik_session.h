/**************************************************************************/
/*  authentik_session.h                                                   */
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

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class AuthentikSession : public RefCounted {
	GDCLASS(AuthentikSession, RefCounted);

	String issuer;
	String client_id;
	String access_token;
	String id_token;
	String refresh_token;
	String token_type = "Bearer";
	String scope;
	int64_t expires_at = 0;

	String subject;
	String display_name;
	String username;
	String email;
	bool email_verified = false;
	PackedStringArray groups;
	PackedStringArray entitlements;
	Dictionary claims;

protected:
	static void _bind_methods();
	virtual String _to_string() override;

public:
	static Ref<AuthentikSession> create_from_token_response(
			const Dictionary &p_tokens,
			const String &p_issuer,
			const String &p_client_id,
			const String &p_expected_nonce,
			const String &p_previous_refresh_token,
			int64_t p_now_unix,
			String &r_error);

	void apply_userinfo_claims(const Dictionary &p_userinfo);

	Dictionary to_storage_dictionary() const;
	static Ref<AuthentikSession> from_storage_dictionary(const Dictionary &p_dict);

	String get_issuer() const { return issuer; }
	String get_client_id() const { return client_id; }
	String get_access_token() const { return access_token; }
	String get_id_token() const { return id_token; }
	String get_refresh_token() const { return refresh_token; }
	String get_token_type() const { return token_type; }
	String get_scope() const { return scope; }
	int64_t get_expires_at() const { return expires_at; }

	String get_subject() const { return subject; }
	String get_display_name() const { return display_name; }
	String get_username() const { return username; }
	String get_email() const { return email; }
	bool is_email_verified() const { return email_verified; }
	PackedStringArray get_groups() const { return groups; }
	PackedStringArray get_entitlements() const { return entitlements; }
	Dictionary get_claims() const { return claims; }

	bool is_expired_at(int64_t p_unix_time, int64_t p_leeway_sec = 0) const;
	bool is_expired(int64_t p_leeway_sec = 0) const;
	int64_t get_seconds_until_expiry() const;
	bool has_refresh_token() const { return !refresh_token.is_empty(); }

	bool has_group(const String &p_group) const;
	bool has_any_group(const PackedStringArray &p_groups) const;
	bool has_all_groups(const PackedStringArray &p_groups) const;
	bool has_entitlement(const String &p_entitlement) const;
	Variant get_claim(const String &p_key, const Variant &p_default = Variant()) const;

	String get_bearer_token() const;
	String get_authorization_header() const;
	Dictionary to_summary_dictionary() const;
};
