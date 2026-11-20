/**************************************************************************/
/*  goethe_dialogue.cpp                                                   */
/**************************************************************************/

#include "goethe_dialogue.h"

#include "core/io/json.h"
#include "core/object/class_db.h"

static PackedStringArray _json_to_string_array(const String &p_json) {
	PackedStringArray out;
	if (p_json.is_empty()) {
		return out;
	}
	Variant parsed = JSON::parse_string(p_json);
	if (parsed.get_type() == Variant::ARRAY) {
		Array arr = parsed;
		for (int i = 0; i < arr.size(); ++i) {
			out.push_back(String(arr[i]));
		}
	}
	return out;
}

static Dictionary _json_to_dict(const String &p_json) {
	if (p_json.is_empty()) {
		return Dictionary();
	}
	Variant parsed = JSON::parse_string(p_json);
	return parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
}

void GoetheDialogue::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_from_file", "path"), &GoetheDialogue::load_from_file);
	ClassDB::bind_method(D_METHOD("load_from_yaml", "yaml_content"), &GoetheDialogue::load_from_yaml);
	ClassDB::bind_method(D_METHOD("is_loaded"), &GoetheDialogue::is_loaded);
	ClassDB::bind_method(D_METHOD("get_last_error"), &GoetheDialogue::get_last_error);
	ClassDB::bind_method(D_METHOD("get_source_path"), &GoetheDialogue::get_source_path);

	ClassDB::bind_method(D_METHOD("get_id"), &GoetheDialogue::get_id);
	ClassDB::bind_method(D_METHOD("get_start_node"), &GoetheDialogue::get_start_node);
	ClassDB::bind_method(D_METHOD("get_node_count"), &GoetheDialogue::get_node_count);
	ClassDB::bind_method(D_METHOD("get_node_ids"), &GoetheDialogue::get_node_ids);
	ClassDB::bind_method(D_METHOD("has_node", "node_id"), &GoetheDialogue::has_node);
	ClassDB::bind_method(D_METHOD("get_speakers"), &GoetheDialogue::get_speakers);
	ClassDB::bind_method(D_METHOD("get_metadata"), &GoetheDialogue::get_metadata);
	ClassDB::bind_method(D_METHOD("get_node_info", "node_id"), &GoetheDialogue::get_node_info);
	ClassDB::bind_method(D_METHOD("validate"), &GoetheDialogue::validate);
	ClassDB::bind_method(D_METHOD("to_yaml"), &GoetheDialogue::to_yaml);
}

GoetheDialogue::GoetheDialogue() {
	const GoetheApiTable &api = GoetheApi::get();
	if (api.available && api.dialogue_create) {
		handle_ = api.dialogue_create();
	}
}

GoetheDialogue::~GoetheDialogue() {
	const GoetheApiTable &api = GoetheApi::get();
	if (handle_ != nullptr && api.dialogue_destroy) {
		api.dialogue_destroy(handle_);
		handle_ = nullptr;
	}
}

Error GoetheDialogue::load_from_file(const String &p_path) {
	CharString utf8;
	Error err = GoetheApi::read_godot_text_file(p_path, utf8);
	if (err != OK) {
		last_error_ = String("Cannot open dialogue file: ") + p_path;
		return err;
	}
	err = load_from_yaml(String::utf8(utf8.get_data(), utf8.length()));
	if (err == OK) {
		source_path_ = p_path;
	}
	return err;
}

Error GoetheDialogue::load_from_yaml(const String &p_yaml_content) {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr) {
		last_error_ = "Goethe native runtime library (goethe.dll / libgoethe) is not loaded.";
		return ERR_UNAVAILABLE;
	}
	CharString utf8 = p_yaml_content.utf8();
	int rc = api.dialogue_load_yaml(handle_, utf8.get_data());
	if (rc != 0) {
		last_error_ = String::utf8(api.dialogue_get_last_error(handle_));
		return ERR_PARSE_ERROR;
	}
	source_path_ = String();
	last_error_ = String();
	return OK;
}

bool GoetheDialogue::is_loaded() const {
	const GoetheApiTable &api = GoetheApi::get();
	return api.available && handle_ != nullptr && api.dialogue_is_loaded(handle_) != 0;
}

String GoetheDialogue::get_last_error() const {
	return last_error_;
}

String GoetheDialogue::get_source_path() const {
	return source_path_;
}

String GoetheDialogue::get_id() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return String();
	}
	return String::utf8(api.dialogue_get_id(handle_));
}

String GoetheDialogue::get_start_node() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return String();
	}
	return String::utf8(api.dialogue_get_start_node(handle_));
}

int64_t GoetheDialogue::get_node_count() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return 0;
	}
	return api.dialogue_get_node_count(handle_);
}

PackedStringArray GoetheDialogue::get_node_ids() const {
	PackedStringArray ids;
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return ids;
	}
	int count = api.dialogue_get_node_count(handle_);
	for (int i = 0; i < count; ++i) {
		ids.push_back(String::utf8(api.dialogue_get_node_id(handle_, i)));
	}
	return ids;
}

bool GoetheDialogue::has_node(const String &p_node_id) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return false;
	}
	CharString id = p_node_id.utf8();
	return api.dialogue_has_node(handle_, id.get_data()) != 0;
}

PackedStringArray GoetheDialogue::get_speakers() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return PackedStringArray();
	}
	return _json_to_string_array(GoetheApi::take_c_string(api.dialogue_get_speakers_json(handle_)));
}

Dictionary GoetheDialogue::get_metadata() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return Dictionary();
	}
	return _json_to_dict(GoetheApi::take_c_string(api.dialogue_get_metadata_json(handle_)));
}

Dictionary GoetheDialogue::get_node_info(const String &p_node_id) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return Dictionary();
	}
	CharString id = p_node_id.utf8();
	return _json_to_dict(GoetheApi::take_c_string(api.dialogue_get_node_info_json(handle_, id.get_data())));
}

PackedStringArray GoetheDialogue::validate() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr) {
		PackedStringArray errs;
		errs.push_back("Goethe native library is not loaded");
		return errs;
	}
	return _json_to_string_array(GoetheApi::take_c_string(api.dialogue_validate_json(handle_)));
}

String GoetheDialogue::to_yaml() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!is_loaded()) {
		return String();
	}
	return GoetheApi::take_c_string(api.dialogue_to_yaml(handle_));
}
