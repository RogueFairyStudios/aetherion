/**************************************************************************/
/*  authentik_session.cpp                                                 */
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

#include "authentik_session.h"

#include "authentik_oidc.h"

#include "core/object/class_db.h"
#include "core/os/os.h"

static PackedStringArray _extract_string_list(const Variant &p_raw) {
	PackedStringArray result;
	if (p_raw.get_type() == Variant::PACKED_STRING_ARRAY) {
		return p_raw;
	}
	if (p_raw.get_type() == Variant::ARRAY) {
		Array arr = p_raw;
		for (int i = 0; i < arr.size(); i++) {
			if (arr[i].get_type() == Variant::STRING) {
				String s = String(arr[i]);
				if (!s.is_empty() && !result.has(s)) {
					result.push_back(s);
				}
			}
		}
	}
	return result;
}

static void _populate_identity_fields(
		const Dictionary &p_claims,
		String &r_subject,
		String &r_username,
		String &r_email,
		bool &r_email_verified,
		String &r_display_name,
		PackedStringArray &r_groups,
		PackedStringArray &r_entitlements) {
	if (p_claims.has("sub") && p_claims["sub"].get_type() == Variant::STRING) {
		String v = p_claims["sub"];
		if (!v.is_empty()) {
			r_subject = v;
		}
	}
	if (p_claims.has("preferred_username") && p_claims["preferred_username"].get_type() == Variant::STRING) {
		String v = p_claims["preferred_username"];
		if (!v.is_empty()) {
			r_username = v;
		}
	} else if (p_claims.has("nickname") && p_claims["nickname"].get_type() == Variant::STRING && r_username.is_empty()) {
		r_username = p_claims["nickname"];
	}
	if (p_claims.has("email") && p_claims["email"].get_type() == Variant::STRING) {
		String v = p_claims["email"];
		if (!v.is_empty()) {
			r_email = v;
		}
	}
	if (p_claims.has("email_verified")) {
		Variant ev = p_claims["email_verified"];
		if (ev.get_type() == Variant::BOOL) {
			r_email_verified = bool(ev);
		} else if (ev.get_type() == Variant::STRING) {
			r_email_verified = String(ev).to_lower() == "true";
		}
	}
	if (p_claims.has("groups")) {
		PackedStringArray g = _extract_string_list(p_claims["groups"]);
		if (!g.is_empty()) {
			r_groups = g;
		}
	}
	if (p_claims.has("entitlements")) {
		PackedStringArray e = _extract_string_list(p_claims["entitlements"]);
		if (!e.is_empty()) {
			r_entitlements = e;
		}
	}
	if (p_claims.has("name") && p_claims["name"].get_type() == Variant::STRING) {
		String v = String(p_claims["name"]).strip_edges();
		if (!v.is_empty()) {
			r_display_name = v;
		}
	}
	if (r_display_name.is_empty()) {
		if (!r_username.is_empty()) {
			r_display_name = r_username;
		} else if (!r_email.is_empty()) {
			r_display_name = r_email;
		} else if (!r_subject.is_empty()) {
			r_display_name = r_subject;
		}
	}
}

Ref<AuthentikSession> AuthentikSession::create_from_token_response(
		const Dictionary &p_tokens,
		const String &p_issuer,
		const String &p_client_id,
		const String &p_expected_nonce,
		const String &p_previous_refresh_token,
		int64_t p_now_unix,
		String &r_error) {
	r_error = String();

	String access = p_tokens.get("access_token", String());
	String id_tok = p_tokens.get("id_token", String());
	if (access.is_empty() && id_tok.is_empty()) {
		r_error = "Token endpoint response did not include an access_token or id_token.";
		return Ref<AuthentikSession>();
	}

	Dictionary id_claims;
	if (!id_tok.is_empty()) {
		if (AuthentikOIDC::decode_jwt_payload(id_tok, id_claims) != OK) {
			r_error = "Malformed id_token received from OIDC provider.";
			return Ref<AuthentikSession>();
		}
	}

	Dictionary access_claims;
	if (!access.is_empty() && AuthentikOIDC::is_jwt_format(access)) {
		AuthentikOIDC::decode_jwt_payload(access, access_claims);
	}

	// Validate nonce when expected and an id_token was returned.
	if (!p_expected_nonce.is_empty() && !id_claims.is_empty()) {
		String token_nonce = id_claims.get("nonce", String());
		if (token_nonce.is_empty() || !AuthentikOIDC::constant_time_equals(token_nonce, p_expected_nonce)) {
			r_error = "OIDC nonce validation failed.";
			return Ref<AuthentikSession>();
		}
	}

	// Validate issuer & audience on id_token when present.
	if (!id_claims.is_empty()) {
		if (!p_issuer.is_empty() && id_claims.has("iss")) {
			String token_iss = id_claims.get("iss", String());
			if (!AuthentikOIDC::issuers_match(p_issuer, token_iss)) {
				r_error = vformat("OIDC issuer mismatch: expected '%s', got '%s'.", p_issuer, token_iss);
				return Ref<AuthentikSession>();
			}
		}
		if (!p_client_id.is_empty() && id_claims.has("aud")) {
			if (!AuthentikOIDC::audience_matches(id_claims["aud"], p_client_id)) {
				r_error = vformat("OIDC audience mismatch for client_id '%s'.", p_client_id);
				return Ref<AuthentikSession>();
			}
		}
	}

	int64_t expires_in = 3600;
	if (p_tokens.has("expires_in")) {
		Variant raw_exp_in = p_tokens["expires_in"];
		if (raw_exp_in.get_type() == Variant::INT || raw_exp_in.get_type() == Variant::FLOAT) {
			expires_in = int64_t(raw_exp_in);
		}
	}
	int64_t computed_expires_at = p_now_unix + MAX((int64_t)1, expires_in);

	Variant jwt_exp = access_claims.has("exp") ? access_claims["exp"] : (id_claims.has("exp") ? id_claims["exp"] : Variant());
	if (jwt_exp.get_type() == Variant::INT || jwt_exp.get_type() == Variant::FLOAT) {
		int64_t claim_exp = int64_t(jwt_exp);
		if (claim_exp > 0) {
			if (claim_exp + 60 < p_now_unix) {
				r_error = "Received an already-expired OIDC token.";
				return Ref<AuthentikSession>();
			}
			computed_expires_at = claim_exp;
		}
	}

	// Merge claims: start with id_token claims, then overlay access_token claims (which in Authentik carry groups/entitlements).
	Dictionary merged_claims = id_claims.duplicate(true);
	Array acc_keys = access_claims.keys();
	for (int i = 0; i < acc_keys.size(); i++) {
		merged_claims[acc_keys[i]] = access_claims[acc_keys[i]];
	}
	// Preserve id_token display fields if access_token omitted them.
	if (id_claims.has("name") && !String(id_claims["name"]).is_empty()) {
		merged_claims["name"] = id_claims["name"];
	}
	if (id_claims.has("email") && !String(id_claims["email"]).is_empty()) {
		merged_claims["email"] = id_claims["email"];
	}
	if (id_claims.has("preferred_username") && !String(id_claims["preferred_username"]).is_empty()) {
		merged_claims["preferred_username"] = id_claims["preferred_username"];
	}

	Ref<AuthentikSession> s;
	s.instantiate();
	s->issuer = p_issuer;
	s->client_id = p_client_id;
	s->access_token = access;
	s->id_token = id_tok;
	String new_refresh = p_tokens.get("refresh_token", String());
	s->refresh_token = !new_refresh.is_empty() ? new_refresh : p_previous_refresh_token;
	String tok_type = p_tokens.get("token_type", String("Bearer"));
	s->token_type = tok_type.is_empty() ? String("Bearer") : tok_type;
	s->scope = p_tokens.get("scope", String());
	s->expires_at = computed_expires_at;
	s->claims = merged_claims;

	_populate_identity_fields(
			s->claims,
			s->subject,
			s->username,
			s->email,
			s->email_verified,
			s->display_name,
			s->groups,
			s->entitlements);

	if (s->subject.is_empty()) {
		s->subject = "authenticated-user";
	}
	if (s->display_name.is_empty()) {
		s->display_name = s->subject;
	}

	return s;
}

void AuthentikSession::apply_userinfo_claims(const Dictionary &p_userinfo) {
	if (p_userinfo.is_empty()) {
		return;
	}
	// Verify sub matches if both exist.
	if (!subject.is_empty() && subject != "authenticated-user" && p_userinfo.has("sub")) {
		String ui_sub = p_userinfo.get("sub", String());
		if (!ui_sub.is_empty() && ui_sub != subject) {
			return;
		}
	}
	Array keys = p_userinfo.keys();
	for (int i = 0; i < keys.size(); i++) {
		claims[keys[i]] = p_userinfo[keys[i]];
	}
	display_name = String();
	_populate_identity_fields(
			claims,
			subject,
			username,
			email,
			email_verified,
			display_name,
			groups,
			entitlements);
}

Dictionary AuthentikSession::to_storage_dictionary() const {
	Dictionary d;
	d["version"] = 1;
	d["issuer"] = issuer;
	d["client_id"] = client_id;
	d["access_token"] = access_token;
	d["id_token"] = id_token;
	d["refresh_token"] = refresh_token;
	d["token_type"] = token_type;
	d["scope"] = scope;
	d["expires_at"] = expires_at;
	d["subject"] = subject;
	d["display_name"] = display_name;
	d["username"] = username;
	d["email"] = email;
	d["email_verified"] = email_verified;
	Array g_arr;
	for (int i = 0; i < groups.size(); i++) {
		g_arr.push_back(groups[i]);
	}
	d["groups"] = g_arr;
	Array e_arr;
	for (int i = 0; i < entitlements.size(); i++) {
		e_arr.push_back(entitlements[i]);
	}
	d["entitlements"] = e_arr;
	d["claims"] = claims;
	return d;
}

Ref<AuthentikSession> AuthentikSession::from_storage_dictionary(const Dictionary &p_dict) {
	String access = p_dict.get("access_token", String());
	String id_tok = p_dict.get("id_token", String());
	String refresh = p_dict.get("refresh_token", String());
	if (access.is_empty() && id_tok.is_empty() && refresh.is_empty()) {
		return Ref<AuthentikSession>();
	}

	Ref<AuthentikSession> s;
	s.instantiate();
	s->issuer = p_dict.get("issuer", String());
	s->client_id = p_dict.get("client_id", String());
	s->access_token = access;
	s->id_token = id_tok;
	s->refresh_token = refresh;
	s->token_type = p_dict.get("token_type", String("Bearer"));
	s->scope = p_dict.get("scope", String());
	s->expires_at = int64_t(p_dict.get("expires_at", 0));
	s->subject = p_dict.get("subject", String());
	s->display_name = p_dict.get("display_name", String());
	s->username = p_dict.get("username", String());
	s->email = p_dict.get("email", String());
	s->email_verified = bool(p_dict.get("email_verified", false));
	if (p_dict.has("groups")) {
		s->groups = _extract_string_list(p_dict["groups"]);
	}
	if (p_dict.has("entitlements")) {
		s->entitlements = _extract_string_list(p_dict["entitlements"]);
	}
	if (p_dict.has("claims") && p_dict["claims"].get_type() == Variant::DICTIONARY) {
		s->claims = p_dict["claims"];
	}
	if (s->display_name.is_empty()) {
		_populate_identity_fields(
				s->claims,
				s->subject,
				s->username,
				s->email,
				s->email_verified,
				s->display_name,
				s->groups,
				s->entitlements);
	}
	return s;
}

bool AuthentikSession::is_expired_at(int64_t p_unix_time, int64_t p_leeway_sec) const {
	if (expires_at <= 0) {
		return true;
	}
	return p_unix_time >= (expires_at - p_leeway_sec);
}

bool AuthentikSession::is_expired(int64_t p_leeway_sec) const {
	int64_t now = OS::get_singleton() ? int64_t(OS::get_singleton()->get_unix_time()) : 0;
	return is_expired_at(now, p_leeway_sec);
}

int64_t AuthentikSession::get_seconds_until_expiry() const {
	int64_t now = OS::get_singleton() ? int64_t(OS::get_singleton()->get_unix_time()) : 0;
	int64_t diff = expires_at - now;
	return diff > 0 ? diff : 0;
}

bool AuthentikSession::has_group(const String &p_group) const {
	return groups.has(p_group);
}

bool AuthentikSession::has_any_group(const PackedStringArray &p_groups) const {
	for (int i = 0; i < p_groups.size(); i++) {
		if (groups.has(p_groups[i])) {
			return true;
		}
	}
	return false;
}

bool AuthentikSession::has_all_groups(const PackedStringArray &p_groups) const {
	for (int i = 0; i < p_groups.size(); i++) {
		if (!groups.has(p_groups[i])) {
			return false;
		}
	}
	return true;
}

bool AuthentikSession::has_entitlement(const String &p_entitlement) const {
	return entitlements.has(p_entitlement);
}

Variant AuthentikSession::get_claim(const String &p_key, const Variant &p_default) const {
	return claims.get(p_key, p_default);
}

String AuthentikSession::get_bearer_token() const {
	if (!access_token.is_empty() && AuthentikOIDC::is_jwt_format(access_token)) {
		return access_token;
	}
	if (!id_token.is_empty() && AuthentikOIDC::is_jwt_format(id_token)) {
		return id_token;
	}
	return !access_token.is_empty() ? access_token : id_token;
}

String AuthentikSession::get_authorization_header() const {
	String tok = get_bearer_token();
	if (tok.is_empty()) {
		return String();
	}
	return "Bearer " + tok;
}

Dictionary AuthentikSession::to_summary_dictionary() const {
	Dictionary d;
	d["issuer"] = issuer;
	d["client_id"] = client_id;
	d["subject"] = subject;
	d["display_name"] = display_name;
	d["username"] = username;
	d["email"] = email;
	d["email_verified"] = email_verified;
	d["groups"] = groups;
	d["entitlements"] = entitlements;
	d["expires_at"] = expires_at;
	d["is_expired"] = is_expired();
	d["has_refresh_token"] = has_refresh_token();
	return d;
}

String AuthentikSession::_to_string() {
	return vformat("<AuthentikSession:%s user=\"%s\" expires_at=%d>", itos(get_instance_id()), display_name, expires_at);
}

void AuthentikSession::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_issuer"), &AuthentikSession::get_issuer);
	ClassDB::bind_method(D_METHOD("get_client_id"), &AuthentikSession::get_client_id);
	ClassDB::bind_method(D_METHOD("get_access_token"), &AuthentikSession::get_access_token);
	ClassDB::bind_method(D_METHOD("get_id_token"), &AuthentikSession::get_id_token);
	ClassDB::bind_method(D_METHOD("get_refresh_token"), &AuthentikSession::get_refresh_token);
	ClassDB::bind_method(D_METHOD("get_token_type"), &AuthentikSession::get_token_type);
	ClassDB::bind_method(D_METHOD("get_scope"), &AuthentikSession::get_scope);
	ClassDB::bind_method(D_METHOD("get_expires_at"), &AuthentikSession::get_expires_at);

	ClassDB::bind_method(D_METHOD("get_subject"), &AuthentikSession::get_subject);
	ClassDB::bind_method(D_METHOD("get_display_name"), &AuthentikSession::get_display_name);
	ClassDB::bind_method(D_METHOD("get_username"), &AuthentikSession::get_username);
	ClassDB::bind_method(D_METHOD("get_email"), &AuthentikSession::get_email);
	ClassDB::bind_method(D_METHOD("is_email_verified"), &AuthentikSession::is_email_verified);
	ClassDB::bind_method(D_METHOD("get_groups"), &AuthentikSession::get_groups);
	ClassDB::bind_method(D_METHOD("get_entitlements"), &AuthentikSession::get_entitlements);
	ClassDB::bind_method(D_METHOD("get_claims"), &AuthentikSession::get_claims);

	ClassDB::bind_method(D_METHOD("is_expired", "leeway_sec"), &AuthentikSession::is_expired, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_seconds_until_expiry"), &AuthentikSession::get_seconds_until_expiry);
	ClassDB::bind_method(D_METHOD("has_refresh_token"), &AuthentikSession::has_refresh_token);

	ClassDB::bind_method(D_METHOD("has_group", "group"), &AuthentikSession::has_group);
	ClassDB::bind_method(D_METHOD("has_any_group", "groups"), &AuthentikSession::has_any_group);
	ClassDB::bind_method(D_METHOD("has_all_groups", "groups"), &AuthentikSession::has_all_groups);
	ClassDB::bind_method(D_METHOD("has_entitlement", "entitlement"), &AuthentikSession::has_entitlement);
	ClassDB::bind_method(D_METHOD("get_claim", "key", "default"), &AuthentikSession::get_claim, DEFVAL(Variant()));

	ClassDB::bind_method(D_METHOD("get_bearer_token"), &AuthentikSession::get_bearer_token);
	ClassDB::bind_method(D_METHOD("get_authorization_header"), &AuthentikSession::get_authorization_header);
	ClassDB::bind_method(D_METHOD("to_summary_dictionary"), &AuthentikSession::to_summary_dictionary);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "issuer"), "", "get_issuer");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "client_id"), "", "get_client_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "access_token"), "", "get_access_token");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "id_token"), "", "get_id_token");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "refresh_token"), "", "get_refresh_token");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "token_type"), "", "get_token_type");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "scope"), "", "get_scope");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "expires_at"), "", "get_expires_at");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "subject"), "", "get_subject");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "", "get_display_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "username"), "", "get_username");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "email"), "", "get_email");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "email_verified"), "", "is_email_verified");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "groups"), "", "get_groups");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "entitlements"), "", "get_entitlements");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "claims"), "", "get_claims");
}
