/**************************************************************************/
/*  goethe_api.h                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             AETHERION ENGINE                           */
/*                     https://github.com/RogueFairyStudios               */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"

#ifdef GOETHE_EXTERNAL_SDK
#include <goethe/goethe_dialog.h>
#else
#include "goethe_c_abi_fallback.h"
#endif

// Runtime C-ABI dispatch table loaded from pre-built goethe.dll / libgoethe.so / libgoethe.dylib.
struct GoetheApiTable {
	void *library_handle = nullptr;
	bool initialized = false;
	bool available = false;

	const char *(*version)() = nullptr;
	void (*free_string)(char *) = nullptr;

	// Locale API
	void (*set_locale)(const char *) = nullptr;
	char *(*get_locale)() = nullptr;
	int (*locale_load_file)(const char *) = nullptr;
	int (*locale_load_string)(const char *) = nullptr;
	int (*locale_load_from_package)(const char *, const char *) = nullptr;
	void (*locale_unload)(const char *) = nullptr;
	void (*locale_unload_all)() = nullptr;
	int (*locale_has_key)(const char *) = nullptr;
	int (*locale_has_key_in_locale)(const char *, const char *) = nullptr;
	char *(*translate)(const char *) = nullptr;
	char *(*translate_with_params)(const char *, const char *const *, const char *const *, int) = nullptr;
	char *(*translate_plural)(const char *, int64_t) = nullptr;
	char *(*translate_plural_with_params)(const char *, int64_t, const char *const *, const char *const *, int) = nullptr;

	// Dialogue Handle API
	GoetheDialogueHandle *(*dialogue_create)() = nullptr;
	void (*dialogue_destroy)(GoetheDialogueHandle *) = nullptr;
	int (*dialogue_load_file)(GoetheDialogueHandle *, const char *) = nullptr;
	int (*dialogue_load_yaml)(GoetheDialogueHandle *, const char *) = nullptr;
	int (*dialogue_is_loaded)(const GoetheDialogueHandle *) = nullptr;
	const char *(*dialogue_get_last_error)(const GoetheDialogueHandle *) = nullptr;
	const char *(*dialogue_get_id)(const GoetheDialogueHandle *) = nullptr;
	const char *(*dialogue_get_start_node)(const GoetheDialogueHandle *) = nullptr;
	int (*dialogue_get_node_count)(const GoetheDialogueHandle *) = nullptr;
	const char *(*dialogue_get_node_id)(const GoetheDialogueHandle *, int) = nullptr;
	int (*dialogue_has_node)(const GoetheDialogueHandle *, const char *) = nullptr;
	char *(*dialogue_get_speakers_json)(const GoetheDialogueHandle *) = nullptr;
	char *(*dialogue_get_metadata_json)(const GoetheDialogueHandle *) = nullptr;
	char *(*dialogue_get_node_info_json)(const GoetheDialogueHandle *, const char *) = nullptr;
	char *(*dialogue_validate_json)(const GoetheDialogueHandle *) = nullptr;
	char *(*dialogue_to_yaml)(const GoetheDialogueHandle *) = nullptr;

	// Runner Handle API
	GoetheRunnerHandle *(*runner_create)() = nullptr;
	void (*runner_destroy)(GoetheRunnerHandle *) = nullptr;
	void (*runner_set_callbacks)(GoetheRunnerHandle *, const GoetheRunnerCallbacks *) = nullptr;
	void (*runner_set_raw_keys)(GoetheRunnerHandle *, int) = nullptr;
	int (*runner_get_raw_keys)(const GoetheRunnerHandle *) = nullptr;
	int (*runner_start)(GoetheRunnerHandle *, const GoetheDialogueHandle *) = nullptr;
	void (*runner_advance)(GoetheRunnerHandle *) = nullptr;
	int (*runner_select_choice)(GoetheRunnerHandle *, const char *) = nullptr;
	void (*runner_suspend)(GoetheRunnerHandle *) = nullptr;
	void (*runner_resume)(GoetheRunnerHandle *) = nullptr;
	void (*runner_abort)(GoetheRunnerHandle *, const char *) = nullptr;
	int (*runner_get_state)(const GoetheRunnerHandle *) = nullptr;
	int (*runner_is_active)(const GoetheRunnerHandle *) = nullptr;
	int (*runner_is_waiting_choice)(const GoetheRunnerHandle *) = nullptr;
	const char *(*runner_get_dialogue_id)(const GoetheRunnerHandle *) = nullptr;
	const char *(*runner_get_current_node_id)(const GoetheRunnerHandle *) = nullptr;
	char *(*runner_get_current_node_json)(const GoetheRunnerHandle *) = nullptr;
	char *(*runner_get_choices_json)(const GoetheRunnerHandle *) = nullptr;

	// Project Handle API
	GoetheProjectHandle *(*project_create)() = nullptr;
	void (*project_destroy)(GoetheProjectHandle *) = nullptr;
	int (*project_load_file)(GoetheProjectHandle *, const char *) = nullptr;
	int (*project_load_yaml)(GoetheProjectHandle *, const char *) = nullptr;
	int (*project_is_loaded)(const GoetheProjectHandle *) = nullptr;
	const char *(*project_get_last_error)(const GoetheProjectHandle *) = nullptr;
	char *(*project_get_info_json)(const GoetheProjectHandle *) = nullptr;
	char *(*project_get_character_ids_json)(const GoetheProjectHandle *) = nullptr;
	int (*project_has_character)(const GoetheProjectHandle *, const char *) = nullptr;
	char *(*project_get_character_json)(const GoetheProjectHandle *, const char *) = nullptr;
	char *(*project_get_portrait_path)(const GoetheProjectHandle *, const char *, const char *, const char *) = nullptr;
	char *(*project_get_scenes_json)(const GoetheProjectHandle *) = nullptr;
	char *(*project_get_scene_json)(const GoetheProjectHandle *, const char *) = nullptr;
};

namespace GoetheApi {

const GoetheApiTable &get();
bool is_available();
void shutdown();

String take_c_string(char *p_cstr);
Error read_godot_text_file(const String &p_path, CharString &r_utf8);

} // namespace GoetheApi
