/**************************************************************************/
/*  test_authentik.h                                                      */
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

#include "../authentik.h"
#include "../authentik_client.h"
#include "../authentik_oidc.h"
#include "../authentik_session.h"
#include "../studio_api_client.h"

#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace TestAuthentik {

static String _make_test_jwt(const Dictionary &p_payload) {
	Dictionary header;
	header["alg"] = "RS256";
	header["typ"] = "JWT";

	CharString h_utf8 = JSON::stringify(header).utf8();
	PackedByteArray h_bytes;
	h_bytes.resize(h_utf8.length());
	memcpy(h_bytes.ptrw(), h_utf8.get_data(), h_utf8.length());

	CharString p_utf8 = JSON::stringify(p_payload).utf8();
	PackedByteArray p_bytes;
	p_bytes.resize(p_utf8.length());
	memcpy(p_bytes.ptrw(), p_utf8.get_data(), p_utf8.length());

	PackedByteArray sig_bytes;
	sig_bytes.push_back(0x01);
	sig_bytes.push_back(0x02);
	sig_bytes.push_back(0x03);

	return AuthentikOIDC::base64url_encode(h_bytes) + "." +
			AuthentikOIDC::base64url_encode(p_bytes) + "." +
			AuthentikOIDC::base64url_encode(sig_bytes);
}

TEST_CASE("[Authentik] RFC 7636 PKCE S256 and Base64URL") {
	// RFC 7636 Appendix B test vector.
	const String verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
	const String expected_challenge = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";
	CHECK(AuthentikOIDC::compute_pkce_s256_challenge(verifier) == expected_challenge);

	PackedByteArray decoded;
	CHECK(AuthentikOIDC::base64url_decode(expected_challenge, decoded) == OK);
	CHECK(decoded.size() == 32);
	CHECK(AuthentikOIDC::base64url_encode(decoded) == expected_challenge);

	String random_tok;
	CHECK(AuthentikOIDC::generate_random_token(32, random_tok) == OK);
	CHECK(random_tok.length() >= 43);

	CHECK(AuthentikOIDC::constant_time_equals("abc123XYZ", "abc123XYZ"));
	CHECK_FALSE(AuthentikOIDC::constant_time_equals("abc123XYZ", "abc123XY_"));
	CHECK_FALSE(AuthentikOIDC::constant_time_equals("short", "longer_string"));
}

TEST_CASE("[Authentik] Authentik Endpoint Derivation and OIDC Discovery Merge") {
	AuthentikEndpoints ep;
	REQUIRE(AuthentikOIDC::derive_authentik_endpoints("https://auth.example.com/application/o/sample-app", ep) == OK);
	CHECK(ep.issuer == "https://auth.example.com/application/o/sample-app/");
	CHECK(ep.discovery_url == "https://auth.example.com/application/o/sample-app/.well-known/openid-configuration");
	CHECK(ep.authorization_endpoint == "https://auth.example.com/application/o/authorize/");
	CHECK(ep.token_endpoint == "https://auth.example.com/application/o/token/");
	CHECK(ep.userinfo_endpoint == "https://auth.example.com/application/o/userinfo/");
	CHECK(ep.device_authorization_endpoint == "https://auth.example.com/application/o/device/");
	CHECK(ep.revocation_endpoint == "https://auth.example.com/application/o/revoke/");
	CHECK(ep.end_session_endpoint == "https://auth.example.com/application/o/sample-app/end-session/");
	CHECK(ep.is_valid());

	Dictionary doc;
	doc["issuer"] = "https://auth.example.com/application/o/sample-app/";
	doc["token_endpoint"] = "https://auth.example.com/custom/token";
	AuthentikOIDC::merge_discovery_document(doc, ep);
	CHECK(ep.token_endpoint == "https://auth.example.com/custom/token");
	CHECK(ep.authorization_endpoint == "https://auth.example.com/application/o/authorize/");
}

TEST_CASE("[Authentik] Query String and Loopback Callback Parsing") {
	Dictionary params;
	params["client_id"] = "app-client";
	params["scope"] = "openid profile email";
	params["redirect_uri"] = "http://127.0.0.1:8400/callback";

	String encoded = AuthentikOIDC::form_urlencode(params);
	Dictionary parsed = AuthentikOIDC::parse_query_string(encoded);
	CHECK(String(parsed["client_id"]) == "app-client");
	CHECK(String(parsed["scope"]) == "openid profile email");
	CHECK(String(parsed["redirect_uri"]) == "http://127.0.0.1:8400/callback");

	String raw_req = "GET /callback?code=auth_code_123&state=xyz_987 HTTP/1.1\r\nHost: 127.0.0.1:8400\r\n\r\n";
	String path_only;
	Dictionary cb_query;
	REQUIRE(AuthentikOIDC::parse_http_get_request(raw_req, path_only, cb_query) == OK);
	CHECK(path_only == "/callback");
	CHECK(String(cb_query["code"]) == "auth_code_123");
	CHECK(String(cb_query["state"]) == "xyz_987");
}

TEST_CASE("[Authentik] Session Token Validation, Groups, Entitlements, and Encrypted Storage") {
	const String issuer = "https://auth.example.com/application/o/sample-app/";
	const String client_id = "sample-public-client";
	const String nonce = "nonce_value_42";

	Dictionary id_claims;
	id_claims["iss"] = issuer;
	id_claims["sub"] = "user-uuid-001";
	id_claims["aud"] = client_id;
	id_claims["nonce"] = nonce;
	id_claims["exp"] = 4102444800LL;
	id_claims["name"] = "Ada Lovelace";
	id_claims["preferred_username"] = "ada";
	id_claims["email"] = "ada@example.com";
	id_claims["email_verified"] = true;

	Array groups_arr;
	groups_arr.push_back("Engine Developers");
	groups_arr.push_back("Tech Art");
	id_claims["groups"] = groups_arr;

	Array ent_arr;
	ent_arr.push_back("editor:admin");
	id_claims["entitlements"] = ent_arr;

	Dictionary token_resp;
	token_resp["access_token"] = _make_test_jwt(id_claims);
	token_resp["id_token"] = _make_test_jwt(id_claims);
	token_resp["refresh_token"] = "refresh_opaque_secret";
	token_resp["token_type"] = "Bearer";
	token_resp["expires_in"] = 3600;
	token_resp["scope"] = "openid profile email offline_access";

	String err;
	Ref<AuthentikSession> session = AuthentikSession::create_from_token_response(
			token_resp, issuer, client_id, nonce, String(), 1700000000LL, err);
	REQUIRE_MESSAGE(session.is_valid(), err.utf8().get_data());
	CHECK(session->get_subject() == "user-uuid-001");
	CHECK(session->get_display_name() == "Ada Lovelace");
	CHECK(session->get_username() == "ada");
	CHECK(session->get_email() == "ada@example.com");
	CHECK(session->is_email_verified());
	CHECK(session->has_group("Engine Developers"));
	CHECK_FALSE(session->has_group("External Guests"));
	CHECK(session->has_entitlement("editor:admin"));
	CHECK(session->get_authorization_header().begins_with("Bearer "));

	// Wrong nonce must be rejected.
	String bad_nonce_err;
	Ref<AuthentikSession> rejected = AuthentikSession::create_from_token_response(
			token_resp, issuer, client_id, "different_nonce", String(), 1700000000LL, bad_nonce_err);
	CHECK(rejected.is_null());

	// Encrypted disk persistence round-trip.
	String tmp_file = OS::get_singleton()->get_cache_path().path_join("test_authentik_session.dat");
	Vector<uint8_t> key = AuthentikOIDC::derive_storage_encryption_key("unit-test-context");
	REQUIRE(AuthentikOIDC::save_encrypted_dictionary(tmp_file, session->to_storage_dictionary(), key) == OK);

	Dictionary loaded_dict;
	REQUIRE(AuthentikOIDC::load_encrypted_dictionary(tmp_file, loaded_dict, key) == OK);
	Ref<AuthentikSession> restored = AuthentikSession::from_storage_dictionary(loaded_dict);
	REQUIRE(restored.is_valid());
	CHECK(restored->get_subject() == "user-uuid-001");
	CHECK(restored->get_refresh_token() == "refresh_opaque_secret");
	CHECK(restored->has_group("Tech Art"));

	Ref<DirAccess> da = DirAccess::create_for_path(tmp_file.get_base_dir());
	if (da.is_valid() && da->file_exists(tmp_file)) {
		da->remove(tmp_file);
	}
}

TEST_CASE("[Authentik] StudioApiClient URL Validation and Cross-Tool JSON Parsing") {
	String norm;
	String err;

	// HTTPS remote URLs and trailing /v1 normalization.
	CHECK(StudioApiClient::validate_api_url("https://studio.example.com/v1/", norm, err) == OK);
	CHECK(norm == "https://studio.example.com");
	CHECK(err.is_empty());

	// Loopback HTTP is permitted for local development.
	CHECK(StudioApiClient::validate_api_url("http://127.0.0.1:8090/", norm, err) == OK);
	CHECK(norm == "http://127.0.0.1:8090");
	CHECK(StudioApiClient::validate_api_url("http://localhost:8090/v1", norm, err) == OK);
	CHECK(norm == "http://localhost:8090");

	// Non-loopback plain HTTP must be rejected.
	CHECK(StudioApiClient::validate_api_url("http://studio.example.com", norm, err) == ERR_INVALID_PARAMETER);
	CHECK_FALSE(err.is_empty());

	// Parse Aetherion /v1/me/projects payload.
	const String projects_json = R"({
		"user": {
			"sub": "uuid-user-x",
			"username": "alice",
			"name": "Alice Dev",
			"email": "alice@example.com",
			"groups": ["Studio Members"],
			"isAdmin": false
		},
		"clientApp": "aetherion",
		"projects": [
			{
				"id": "proj-1",
				"revision": 4,
				"name": "Klaus Syndicate",
				"kind": "digital",
				"engine": "aetherion",
				"phase": "production",
				"role": "editor",
				"canEdit": true,
				"assignedOpenTaskCount": 2,
				"openTaskCount": 5,
				"intranetUrl": "https://docs.example.com/p/studio?project=proj-1",
				"lore": {
					"projectSlug": "klaus-syndicate",
					"visibility": "internal",
					"actions": ["read", "write"],
					"canRead": true,
					"canWrite": true,
					"canCreateRepo": false
				},
				"repositories": [
					{
						"id": "repo-code",
						"name": "Engine & Gameplay",
						"slug": "code",
						"system": "git",
						"remoteUri": "https://git.example.com/studio/klaus-syndicate.git",
						"workspacePath": ".",
						"canWrite": true,
						"cloneCommand": ["git", "clone", "https://git.example.com/studio/klaus-syndicate.git"]
					}
				]
			}
		]
	})";

	Array projects;
	Dictionary user;
	REQUIRE(StudioApiClient::parse_projects_json(projects_json, projects, user, err) == OK);
	CHECK(projects.size() == 1);
	CHECK(String(user["username"]) == "alice");
	Dictionary p0 = projects[0];
	CHECK(String(p0["name"]) == "Klaus Syndicate");
	CHECK(String(p0["engine"]) == "aetherion");
	CHECK(int(p0["assignedOpenTaskCount"]) == 2);

	// Parse Lore /v1/me/workspaces payload.
	const String workspaces_json = R"({
		"user": {"sub": "uuid-user-y", "username": "bianca"},
		"clientApp": "geppetto",
		"vcsFilter": "lore",
		"workspaces": [
			{
				"projectId": "proj-1",
				"projectName": "Klaus Syndicate",
				"canWrite": true,
				"repositories": [
					{
						"id": "repo-art",
						"slug": "art-2d",
						"system": "lore",
						"loreFullName": "klaus-syndicate/art-2d",
						"remoteUri": "lores://lore.example.com/klaus-syndicate/art-2d"
					}
				]
			}
		]
	})";

	Array workspaces;
	Dictionary ws_user;
	REQUIRE(StudioApiClient::parse_workspaces_json(workspaces_json, workspaces, ws_user, err) == OK);
	CHECK(workspaces.size() == 1);
	CHECK(String(ws_user["username"]) == "bianca");
	CHECK(StudioApiClient::extract_error_message(R"({"error":"Token expired"})", 401) == "Token expired");
}

TEST_CASE("[Authentik] StudioApiClient Configuration Manifest Parsing") {
	const String project_config_json = R"({
		"schemaVersion": "rfs.studio.config/v1",
		"scope": "project",
		"tool": "aetherion",
		"project": {
			"id": "proj-klaus",
			"slug": "klaus-syndicate",
			"name": "Klaus Syndicate",
			"kind": "digital",
			"engine": "aetherion",
			"usesAetherion": true
		},
		"editorSettings": {
			"network/authentik/issuer_url": "https://auth.example.com/application/o/aetherion/",
			"network/authentik/client_id": "aetherion-editor",
			"network/authentik/scopes": "openid profile email offline_access",
			"network/authentik/studio_api_url": "https://studio.example.com/v1/",
			"network/authentik/redirect_port": 0,
			"network/authentik/persist_session": true,
			"network/authentik/auto_refresh": true
		},
		"workspace": {
			"primaryWorkspacePath": ".",
			"openTaskCount": 5,
			"assignedOpenTaskCount": 2
		}
	})";

	Dictionary parsed_cfg;
	Dictionary editor_settings;
	String err;
	REQUIRE(StudioApiClient::parse_studio_config_json(project_config_json, parsed_cfg, editor_settings, err) == OK);
	CHECK(String(parsed_cfg["scope"]) == "project");
	CHECK(String(Dictionary(parsed_cfg["project"])["name"]) == "Klaus Syndicate");
	CHECK(bool(Dictionary(parsed_cfg["project"])["usesAetherion"]) == true);
	CHECK(String(editor_settings["network/authentik/issuer_url"]) == "https://auth.example.com/application/o/aetherion/");
	CHECK(String(editor_settings["network/authentik/client_id"]) == "aetherion-editor");
	CHECK(String(editor_settings["network/authentik/studio_api_url"]) == "https://studio.example.com");
	CHECK(int(editor_settings["network/authentik/redirect_port"]) == 0);
	CHECK(bool(editor_settings["network/authentik/persist_session"]) == true);

	// Fallback extraction from a generic Authentik + Studio manifest without editorSettings.
	const String generic_auth_json = R"({
		"schemaVersion": "rfs.studio.config/v1",
		"scope": "studio",
		"tool": "authentik",
		"authentik": {
			"issuerUrl": "https://auth.example.com/application/o/studio/",
			"clientId": "studio-cli",
			"scopes": ["openid", "profile", "email"],
			"loopbackRedirectPort": 8400
		},
		"providers": {
			"aetherion": {
				"issuerUrl": "https://auth.example.com/application/o/aetherion/",
				"clientId": "aetherion-desktop",
				"scopes": ["openid", "profile", "email", "offline_access"]
			}
		},
		"studio": {
			"studioApiUrl": "https://docs.example.com/p/studio/"
		}
	})";

	Dictionary generic_cfg;
	Dictionary generic_es;
	REQUIRE(StudioApiClient::parse_studio_config_json(generic_auth_json, generic_cfg, generic_es, err) == OK);
	CHECK(String(generic_es["network/authentik/issuer_url"]) == "https://auth.example.com/application/o/aetherion/");
	CHECK(String(generic_es["network/authentik/client_id"]) == "aetherion-desktop");
	CHECK(String(generic_es["network/authentik/scopes"]) == "openid profile email offline_access");
	CHECK(String(generic_es["network/authentik/studio_api_url"]) == "https://docs.example.com/p/studio");
	CHECK(int(generic_es["network/authentik/redirect_port"]) == 8400);
}

} // namespace TestAuthentik

