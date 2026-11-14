/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "authentik.h"
#include "authentik_client.h"
#include "authentik_session.h"
#include "studio_api_client.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/authentik_editor_plugin.h"
#include "editor/studio_projects_panel.h"
#endif

static Authentik *authentik_singleton = nullptr;

void initialize_authentik_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(AuthentikSession);
		GDREGISTER_CLASS(AuthentikClient);
		GDREGISTER_CLASS(StudioApiClient);
		GDREGISTER_ABSTRACT_CLASS(Authentik);

		GLOBAL_DEF_BASIC("network/authentik/issuer_url", "");
		GLOBAL_DEF_BASIC("network/authentik/client_id", "");
		GLOBAL_DEF_BASIC("network/authentik/scopes", "openid profile email offline_access");
		GLOBAL_DEF_BASIC("network/authentik/studio_api_url", "");
		GLOBAL_DEF(PropertyInfo(Variant::INT, "network/authentik/redirect_port", PROPERTY_HINT_RANGE, "0,65535,1"), 0);
		GLOBAL_DEF("network/authentik/redirect_uri", "");
		GLOBAL_DEF_BASIC("network/authentik/persist_session", true);
		GLOBAL_DEF("network/authentik/auto_refresh", true);

		authentik_singleton = memnew(Authentik);
		Engine::get_singleton()->add_singleton(Engine::Singleton("Authentik", authentik_singleton));
	}

#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(AuthentikAccountDialog);
		GDREGISTER_INTERNAL_CLASS(AuthentikAccountButton);
		GDREGISTER_INTERNAL_CLASS(StudioProjectsPanel);
		GDREGISTER_INTERNAL_CLASS(AuthentikEditorPlugin);
		EditorPlugins::add_by_type<AuthentikEditorPlugin>();
	}
#endif
}

void uninitialize_authentik_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	if (authentik_singleton) {
		Engine::get_singleton()->remove_singleton("Authentik");
		memdelete(authentik_singleton);
		authentik_singleton = nullptr;
	}
}
