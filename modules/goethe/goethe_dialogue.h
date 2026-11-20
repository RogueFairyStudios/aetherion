/**************************************************************************/
/*  goethe_dialogue.h                                                     */
/**************************************************************************/

#pragma once

#include "goethe_api.h"

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class GoetheDialogue : public RefCounted {
	GDCLASS(GoetheDialogue, RefCounted);

	GoetheDialogueHandle *handle_ = nullptr;
	String source_path_;
	String last_error_;

protected:
	static void _bind_methods();

public:
	GoetheDialogue();
	~GoetheDialogue() override;

	Error load_from_file(const String &p_path);
	Error load_from_yaml(const String &p_yaml_content);

	bool is_loaded() const;
	String get_last_error() const;
	String get_source_path() const;

	String get_id() const;
	String get_start_node() const;
	int64_t get_node_count() const;
	PackedStringArray get_node_ids() const;
	bool has_node(const String &p_node_id) const;
	PackedStringArray get_speakers() const;
	Dictionary get_metadata() const;
	Dictionary get_node_info(const String &p_node_id) const;
	PackedStringArray validate() const;
	String to_yaml() const;

	const GoetheDialogueHandle *get_native_handle() const { return handle_; }
};
