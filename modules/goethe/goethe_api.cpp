/**************************************************************************/
/*  goethe_api.cpp                                                        */
/**************************************************************************/

#include "goethe_api.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/os/os.h"

namespace GoetheApi {

static Mutex g_api_mutex;
static GoetheApiTable g_api;

template <typename FnPtr>
static bool _bind_sym(void *p_lib, const char *p_name, FnPtr &r_fn, bool p_optional = false) {
	void *sym = nullptr;
	Error err = OS::get_singleton()->get_dynamic_library_symbol_handle(p_lib, String(p_name), sym, p_optional);
	if (err != OK || sym == nullptr) {
		r_fn = nullptr;
		return p_optional;
	}
	r_fn = reinterpret_cast<FnPtr>(sym);
	return true;
}

static Vector<String> _candidate_library_paths() {
	Vector<String> paths;
	OS *os = OS::get_singleton();
	if (!os) {
		return paths;
	}

	String env_override = os->get_environment("GOETHE_LIBRARY_PATH").strip_edges();
	if (!env_override.is_empty()) {
		paths.push_back(env_override);
	}

#if defined(WINDOWS_ENABLED)
	const String lib_filename = "goethe.dll";
#elif defined(MACOS_ENABLED)
	const String lib_filename = "libgoethe.dylib";
#else
	const String lib_filename = "libgoethe.so";
#endif

	String exe_dir = os->get_executable_path().get_base_dir();
	if (!exe_dir.is_empty()) {
		paths.push_back(exe_dir.path_join(lib_filename));
#if defined(MACOS_ENABLED)
		paths.push_back(exe_dir.path_join("../Frameworks").path_join(lib_filename).simplify_path());
		paths.push_back(exe_dir.path_join("../Resources").path_join(lib_filename).simplify_path());
#endif
	}

	if (ProjectSettings::get_singleton()) {
		String res_bin = ProjectSettings::get_singleton()->globalize_path("res://bin/" + lib_filename);
		if (!res_bin.is_empty()) {
			paths.push_back(res_bin);
		}
	}

	paths.push_back(lib_filename);
	return paths;
}

const GoetheApiTable &get() {
	MutexLock lock(g_api_mutex);
	if (g_api.initialized) {
		return g_api;
	}
	g_api.initialized = true;

	OS *os = OS::get_singleton();
	if (!os) {
		return g_api;
	}

	void *handle = nullptr;
	for (const String &candidate : _candidate_library_paths()) {
		if (os->open_dynamic_library(candidate, handle) == OK && handle != nullptr) {
			break;
		}
		handle = nullptr;
	}

	if (handle == nullptr) {
		return g_api;
	}

	g_api.library_handle = handle;
	bool ok = true;
	ok = _bind_sym(handle, "goethe_version", g_api.version, true) && ok;
	ok = _bind_sym(handle, "goethe_free_string", g_api.free_string) && ok;

	ok = _bind_sym(handle, "goethe_set_locale", g_api.set_locale) && ok;
	ok = _bind_sym(handle, "goethe_get_locale", g_api.get_locale) && ok;
	ok = _bind_sym(handle, "goethe_locale_load_file", g_api.locale_load_file) && ok;
	ok = _bind_sym(handle, "goethe_locale_load_string", g_api.locale_load_string) && ok;
	ok = _bind_sym(handle, "goethe_locale_load_from_package", g_api.locale_load_from_package) && ok;
	ok = _bind_sym(handle, "goethe_locale_unload", g_api.locale_unload) && ok;
	ok = _bind_sym(handle, "goethe_locale_unload_all", g_api.locale_unload_all) && ok;
	ok = _bind_sym(handle, "goethe_locale_has_key", g_api.locale_has_key) && ok;
	ok = _bind_sym(handle, "goethe_locale_has_key_in_locale", g_api.locale_has_key_in_locale) && ok;
	ok = _bind_sym(handle, "goethe_translate", g_api.translate) && ok;
	ok = _bind_sym(handle, "goethe_translate_with_params", g_api.translate_with_params) && ok;
	ok = _bind_sym(handle, "goethe_translate_plural", g_api.translate_plural) && ok;
	ok = _bind_sym(handle, "goethe_translate_plural_with_params", g_api.translate_plural_with_params) && ok;

	ok = _bind_sym(handle, "goethe_dialogue_create", g_api.dialogue_create) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_destroy", g_api.dialogue_destroy) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_load_file", g_api.dialogue_load_file) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_load_yaml", g_api.dialogue_load_yaml) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_is_loaded", g_api.dialogue_is_loaded) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_get_last_error", g_api.dialogue_get_last_error) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_get_id", g_api.dialogue_get_id) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_get_start_node", g_api.dialogue_get_start_node) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_get_node_count", g_api.dialogue_get_node_count) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_get_node_id", g_api.dialogue_get_node_id) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_has_node", g_api.dialogue_has_node) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_get_speakers_json", g_api.dialogue_get_speakers_json) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_get_metadata_json", g_api.dialogue_get_metadata_json) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_get_node_info_json", g_api.dialogue_get_node_info_json) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_validate_json", g_api.dialogue_validate_json) && ok;
	ok = _bind_sym(handle, "goethe_dialogue_to_yaml", g_api.dialogue_to_yaml) && ok;

	ok = _bind_sym(handle, "goethe_runner_create", g_api.runner_create) && ok;
	ok = _bind_sym(handle, "goethe_runner_destroy", g_api.runner_destroy) && ok;
	ok = _bind_sym(handle, "goethe_runner_set_callbacks", g_api.runner_set_callbacks) && ok;
	ok = _bind_sym(handle, "goethe_runner_set_raw_keys", g_api.runner_set_raw_keys) && ok;
	ok = _bind_sym(handle, "goethe_runner_get_raw_keys", g_api.runner_get_raw_keys) && ok;
	ok = _bind_sym(handle, "goethe_runner_start", g_api.runner_start) && ok;
	ok = _bind_sym(handle, "goethe_runner_advance", g_api.runner_advance) && ok;
	ok = _bind_sym(handle, "goethe_runner_select_choice", g_api.runner_select_choice) && ok;
	ok = _bind_sym(handle, "goethe_runner_suspend", g_api.runner_suspend) && ok;
	ok = _bind_sym(handle, "goethe_runner_resume", g_api.runner_resume) && ok;
	ok = _bind_sym(handle, "goethe_runner_abort", g_api.runner_abort) && ok;
	ok = _bind_sym(handle, "goethe_runner_get_state", g_api.runner_get_state) && ok;
	ok = _bind_sym(handle, "goethe_runner_is_active", g_api.runner_is_active) && ok;
	ok = _bind_sym(handle, "goethe_runner_is_waiting_choice", g_api.runner_is_waiting_choice) && ok;
	ok = _bind_sym(handle, "goethe_runner_get_dialogue_id", g_api.runner_get_dialogue_id) && ok;
	ok = _bind_sym(handle, "goethe_runner_get_current_node_id", g_api.runner_get_current_node_id) && ok;
	ok = _bind_sym(handle, "goethe_runner_get_current_node_json", g_api.runner_get_current_node_json) && ok;
	ok = _bind_sym(handle, "goethe_runner_get_choices_json", g_api.runner_get_choices_json) && ok;

	ok = _bind_sym(handle, "goethe_project_create", g_api.project_create) && ok;
	ok = _bind_sym(handle, "goethe_project_destroy", g_api.project_destroy) && ok;
	ok = _bind_sym(handle, "goethe_project_load_file", g_api.project_load_file) && ok;
	ok = _bind_sym(handle, "goethe_project_load_yaml", g_api.project_load_yaml) && ok;
	ok = _bind_sym(handle, "goethe_project_is_loaded", g_api.project_is_loaded) && ok;
	ok = _bind_sym(handle, "goethe_project_get_last_error", g_api.project_get_last_error) && ok;
	ok = _bind_sym(handle, "goethe_project_get_info_json", g_api.project_get_info_json) && ok;
	ok = _bind_sym(handle, "goethe_project_get_character_ids_json", g_api.project_get_character_ids_json) && ok;
	ok = _bind_sym(handle, "goethe_project_has_character", g_api.project_has_character) && ok;
	ok = _bind_sym(handle, "goethe_project_get_character_json", g_api.project_get_character_json) && ok;
	ok = _bind_sym(handle, "goethe_project_get_portrait_path", g_api.project_get_portrait_path) && ok;
	ok = _bind_sym(handle, "goethe_project_get_scenes_json", g_api.project_get_scenes_json) && ok;
	ok = _bind_sym(handle, "goethe_project_get_scene_json", g_api.project_get_scene_json) && ok;

	if (!ok) {
		os->close_dynamic_library(handle);
		g_api = GoetheApiTable();
		g_api.initialized = true;
		return g_api;
	}

	g_api.available = true;
	return g_api;
}

bool is_available() {
	return get().available;
}

void shutdown() {
	MutexLock lock(g_api_mutex);
	if (g_api.library_handle != nullptr && OS::get_singleton()) {
		OS::get_singleton()->close_dynamic_library(g_api.library_handle);
	}
	g_api = GoetheApiTable();
}

String take_c_string(char *p_cstr) {
	if (p_cstr == nullptr) {
		return String();
	}
	String out = String::utf8(p_cstr);
	const GoetheApiTable &api = get();
	if (api.free_string) {
		api.free_string(p_cstr);
	}
	return out;
}

Error read_godot_text_file(const String &p_path, CharString &r_utf8) {
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &err);
	if (file.is_null()) {
		return err == OK ? ERR_CANT_OPEN : err;
	}
	r_utf8 = file->get_as_text().utf8();
	return OK;
}

} // namespace GoetheApi
