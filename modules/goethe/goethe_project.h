/**************************************************************************/
/*  goethe_project.h                                                      */
/**************************************************************************/

#pragma once

#include "goethe_api.h"

#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class GoetheProject : public RefCounted {
	GDCLASS(GoetheProject, RefCounted);

	GoetheProjectHandle *handle_ = nullptr;
	Dictionary info_cache_;
	String source_path_;
	String last_error_;

	String _resolve_path(const String &p_path) const;

protected:
	static void _bind_methods();

public:
	GoetheProject();
	~GoetheProject() override;

	Error load_from_file(const String &p_path);
	Error load_from_yaml(const String &p_yaml_content);

	bool is_loaded() const;
	String get_last_error() const;
	String get_source_path() const;

	String get_project_name() const;
	String get_version() const;
	String get_author() const;
	String get_default_locale() const;
	String get_dialogues_dir() const;
	String get_portraits_dir() const;
	String get_locales_dir() const;

	PackedStringArray get_character_ids() const;
	bool has_character(const String &p_id_or_name) const;
	Dictionary get_character(const String &p_id_or_name) const;
	String get_portrait_path(const String &p_character_id, const String &p_mood = String()) const;

	Array get_scenes() const;
	Dictionary get_scene(const String &p_id_or_path) const;
	String get_scene_path(const String &p_scene_id) const;

	PackedStringArray get_global_flags() const;
	Dictionary get_global_vars() const;
};
