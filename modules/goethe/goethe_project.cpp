/**************************************************************************/
/*  goethe_project.cpp                                                    */
/**************************************************************************/

#include "goethe_project.h"

#include "core/io/json.h"
#include "core/object/class_db.h"

void GoetheProject::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_from_file", "path"), &GoetheProject::load_from_file);
	ClassDB::bind_method(D_METHOD("load_from_yaml", "yaml_content"), &GoetheProject::load_from_yaml);
	ClassDB::bind_method(D_METHOD("is_loaded"), &GoetheProject::is_loaded);
	ClassDB::bind_method(D_METHOD("get_last_error"), &GoetheProject::get_last_error);
	ClassDB::bind_method(D_METHOD("get_source_path"), &GoetheProject::get_source_path);

	ClassDB::bind_method(D_METHOD("get_project_name"), &GoetheProject::get_project_name);
	ClassDB::bind_method(D_METHOD("get_version"), &GoetheProject::get_version);
	ClassDB::bind_method(D_METHOD("get_author"), &GoetheProject::get_author);
	ClassDB::bind_method(D_METHOD("get_default_locale"), &GoetheProject::get_default_locale);
	ClassDB::bind_method(D_METHOD("get_dialogues_dir"), &GoetheProject::get_dialogues_dir);
	ClassDB::bind_method(D_METHOD("get_portraits_dir"), &GoetheProject::get_portraits_dir);
	ClassDB::bind_method(D_METHOD("get_locales_dir"), &GoetheProject::get_locales_dir);

	ClassDB::bind_method(D_METHOD("get_character_ids"), &GoetheProject::get_character_ids);
	ClassDB::bind_method(D_METHOD("has_character", "id_or_name"), &GoetheProject::has_character);
	ClassDB::bind_method(D_METHOD("get_character", "id_or_name"), &GoetheProject::get_character);
	ClassDB::bind_method(D_METHOD("get_portrait_path", "character_id", "mood"), &GoetheProject::get_portrait_path, DEFVAL(String()));

	ClassDB::bind_method(D_METHOD("get_scenes"), &GoetheProject::get_scenes);
	ClassDB::bind_method(D_METHOD("get_scene", "id_or_path"), &GoetheProject::get_scene);
	ClassDB::bind_method(D_METHOD("get_scene_path", "scene_id"), &GoetheProject::get_scene_path);

	ClassDB::bind_method(D_METHOD("get_global_flags"), &GoetheProject::get_global_flags);
	ClassDB::bind_method(D_METHOD("get_global_vars"), &GoetheProject::get_global_vars);
}

GoetheProject::GoetheProject() {
	const GoetheApiTable &api = GoetheApi::get();
	if (api.available && api.project_create) {
		handle_ = api.project_create();
	}
}

GoetheProject::~GoetheProject() {
	const GoetheApiTable &api = GoetheApi::get();
	if (handle_ != nullptr && api.project_destroy) {
		api.project_destroy(handle_);
		handle_ = nullptr;
	}
}

Error GoetheProject::load_from_file(const String &p_path) {
	CharString utf8;
	Error err = GoetheApi::read_godot_text_file(p_path, utf8);
	if (err != OK) {
		last_error_ = String("Cannot open project file: ") + p_path;
		return err;
	}
	err = load_from_yaml(String::utf8(utf8.get_data(), utf8.length()));
	if (err == OK) {
		source_path_ = p_path;
	}
	return err;
}

Error GoetheProject::load_from_yaml(const String &p_yaml_content) {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr) {
		last_error_ = "Goethe native runtime library is not loaded.";
		return ERR_UNAVAILABLE;
	}
	CharString utf8 = p_yaml_content.utf8();
	int rc = api.project_load_yaml(handle_, utf8.get_data());
	if (rc != 0) {
		last_error_ = String::utf8(api.project_get_last_error(handle_));
		return ERR_PARSE_ERROR;
	}
	Variant parsed = JSON::parse_string(GoetheApi::take_c_string(api.project_get_info_json(handle_)));
	info_cache_ = parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
	source_path_ = String();
	last_error_ = String();
	return OK;
}

bool GoetheProject::is_loaded() const {
	const GoetheApiTable &api = GoetheApi::get();
	return api.available && handle_ != nullptr && api.project_is_loaded(handle_) != 0;
}

String GoetheProject::get_last_error() const {
	return last_error_;
}

String GoetheProject::get_source_path() const {
	return source_path_;
}

String GoetheProject::_resolve_path(const String &p_path) const {
	if (p_path.is_empty() || source_path_.is_empty() || p_path.is_absolute_path() || p_path.contains("://")) {
		return p_path;
	}
	return source_path_.get_base_dir().path_join(p_path).simplify_path();
}

String GoetheProject::get_project_name() const {
	return info_cache_.get("name", String());
}

String GoetheProject::get_version() const {
	return info_cache_.get("version", String());
}

String GoetheProject::get_author() const {
	return info_cache_.get("author", String());
}

String GoetheProject::get_default_locale() const {
	return info_cache_.get("default_locale", String());
}

String GoetheProject::get_dialogues_dir() const {
	return is_loaded() ? _resolve_path(info_cache_.get("dialogues_dir", String())) : String();
}

String GoetheProject::get_portraits_dir() const {
	return is_loaded() ? _resolve_path(info_cache_.get("portraits_dir", String())) : String();
}

String GoetheProject::get_locales_dir() const {
	return is_loaded() ? _resolve_path(info_cache_.get("locales_dir", String())) : String();
}

PackedStringArray GoetheProject::get_character_ids() const {
	PackedStringArray out;
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return out;
	}
	Variant parsed = JSON::parse_string(GoetheApi::take_c_string(api.project_get_character_ids_json(handle_)));
	if (parsed.get_type() == Variant::ARRAY) {
		Array arr = parsed;
		for (int i = 0; i < arr.size(); ++i) {
			out.push_back(String(arr[i]));
		}
	}
	return out;
}

bool GoetheProject::has_character(const String &p_id_or_name) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return false;
	}
	CharString cs = p_id_or_name.utf8();
	return api.project_has_character(handle_, cs.get_data()) != 0;
}

Dictionary GoetheProject::get_character(const String &p_id_or_name) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return Dictionary();
	}
	CharString cs = p_id_or_name.utf8();
	Variant parsed = JSON::parse_string(GoetheApi::take_c_string(api.project_get_character_json(handle_, cs.get_data())));
	if (parsed.get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	Dictionary d = parsed;
	if (d.has("default_portrait_path")) {
		d["default_portrait_path"] = _resolve_path(String(d["default_portrait_path"]));
	}
	if (d.has("portraits") && d["portraits"].get_type() == Variant::DICTIONARY) {
		Dictionary raw_p = d["portraits"];
		Dictionary resolved_p;
		Array keys = raw_p.keys();
		for (int i = 0; i < keys.size(); ++i) {
			resolved_p[keys[i]] = _resolve_path(String(raw_p[keys[i]]));
		}
		d["portraits"] = resolved_p;
	}
	return d;
}

String GoetheProject::get_portrait_path(const String &p_character_id, const String &p_mood) const {
	Dictionary c = get_character(p_character_id);
	if (c.is_empty()) {
		return String();
	}
	Dictionary portraits = c.get("portraits", Dictionary());
	if (!p_mood.is_empty() && portraits.has(p_mood)) {
		return String(portraits[p_mood]);
	}
	String def_path = c.get("default_portrait_path", String());
	if (!def_path.is_empty()) {
		return def_path;
	}
	if (portraits.has("default")) {
		return String(portraits["default"]);
	}
	return String();
}

Array GoetheProject::get_scenes() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return Array();
	}
	Variant parsed = JSON::parse_string(GoetheApi::take_c_string(api.project_get_scenes_json(handle_)));
	return parsed.get_type() == Variant::ARRAY ? Array(parsed) : Array();
}

Dictionary GoetheProject::get_scene(const String &p_id_or_path) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return Dictionary();
	}
	CharString cs = p_id_or_path.utf8();
	Variant parsed = JSON::parse_string(GoetheApi::take_c_string(api.project_get_scene_json(handle_, cs.get_data())));
	return parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
}

String GoetheProject::get_scene_path(const String &p_scene_id) const {
	Dictionary sc = get_scene(p_scene_id);
	String fp = sc.get("file_path", String());
	if (fp.is_empty()) {
		return String();
	}
	if (fp.is_absolute_path() || fp.contains("://")) {
		return fp;
	}
	return get_dialogues_dir().path_join(fp).simplify_path();
}

PackedStringArray GoetheProject::get_global_flags() const {
	PackedStringArray out;
	Array arr = info_cache_.get("global_flags", Array());
	for (int i = 0; i < arr.size(); ++i) {
		out.push_back(String(arr[i]));
	}
	return out;
}

Dictionary GoetheProject::get_global_vars() const {
	return info_cache_.get("global_vars", Dictionary());
}
