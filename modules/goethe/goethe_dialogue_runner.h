/**************************************************************************/
/*  goethe_dialogue_runner.h                                              */
/**************************************************************************/

#pragma once

#include "goethe_api.h"
#include "goethe_dialogue.h"

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/binder_common.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class GoetheDialogueRunner : public RefCounted {
	GDCLASS(GoetheDialogueRunner, RefCounted);

public:
	enum State {
		STATE_IDLE = 0,
		STATE_STARTING = 1,
		STATE_RUNNING = 2,
		STATE_WAITING_CHOICE = 3,
		STATE_SUSPENDED = 4,
		STATE_COMPLETED = 5,
		STATE_ABORTED = 6,
	};

private:
	struct PendingSignal {
		StringName name;
		Variant arg0;
		Variant arg1;
		int argc = 0;
	};

	struct CallScope {
		GoetheDialogueRunner *self = nullptr;
		explicit CallScope(GoetheDialogueRunner *p_self);
		~CallScope();
	};

	GoetheRunnerHandle *handle_ = nullptr;
	Ref<GoetheDialogue> dialogue_ref_;
	ObjectID context_id_;
	bool raw_keys_ = false;
	Dictionary current_node_;

	Vector<PendingSignal> pending_;
	int call_depth_ = 0;
	mutable bool in_core_call_ = false;
	bool flushing_ = false;

	Object *_context_object() const;
	Variant _call_hook_1(const char *p_method, const char *p_arg0) const;
	Variant _call_hook_2(const char *p_method, const char *p_arg0, const char *p_arg1) const;
	GoetheRunnerCallbacks _build_callbacks();

	void _queue_signal(const StringName &p_name);
	void _queue_signal(const StringName &p_name, const Variant &p_arg0);
	void _queue_signal(const StringName &p_name, const Variant &p_arg0, const Variant &p_arg1);
	void _flush_signals();
	bool _begin_core_call(const char *p_what) const;

	// Static C-ABI bridges
	static int _cb_has_flag(void *p_ud, const char *p_key);
	static int _cb_get_var(void *p_ud, const char *p_key, char *p_buf, int p_buf_sz);
	static int _cb_get_quest_state(void *p_ud, const char *p_qid, char *p_buf, int p_buf_sz);
	static int _cb_get_objective_state(void *p_ud, const char *p_qid, const char *p_oid, char *p_buf, int p_buf_sz);
	static int _cb_is_chapter_active(void *p_ud, const char *p_id);
	static int _cb_has_entered_area(void *p_ud, const char *p_id);
	static int _cb_has_visited_dialogue(void *p_ud, const char *p_id);
	static int _cb_has_selected_choice(void *p_ud, const char *p_id);
	static int _cb_has_event_fired(void *p_ud, const char *p_id);
	static int _cb_ms_since_event(void *p_ud, const char *p_id);
	static int _cb_has_inventory_item(void *p_ud, const char *p_id);
	static int _cb_is_door_locked(void *p_ud, const char *p_id);
	static int _cb_has_access(void *p_ud, const char *p_id);
	static void _cb_set_flag(void *p_ud, const char *p_key, int p_val);
	static void _cb_set_var(void *p_ud, const char *p_key, const char *p_val);
	static void _cb_on_effect_json(void *p_ud, const char *p_json);
	static void _cb_on_node_presented_json(void *p_ud, const char *p_json);
	static void _cb_on_event(void *p_ud, int p_type, const char *p_dlg_id, const char *p_node_id, const char *p_choice_id, const char *p_reason);

protected:
	static void _bind_methods();

public:
	GoetheDialogueRunner();
	~GoetheDialogueRunner() override;

	Error start(const Ref<GoetheDialogue> &p_dialogue);
	void advance();
	bool select_choice(const String &p_choice_id);
	void suspend();
	void resume();
	void abort(const String &p_reason = String());

	State get_state() const;
	bool is_active() const;
	bool is_waiting_choice() const;

	Ref<GoetheDialogue> get_dialogue() const;
	String get_dialogue_id() const;
	String get_current_node_id() const;
	Dictionary get_current_node() const;
	Array get_choices() const;

	void set_context(Object *p_context);
	Object *get_context() const;

	void set_raw_keys(bool p_enable);
	bool is_raw_keys() const;
};

VARIANT_ENUM_CAST(GoetheDialogueRunner::State);
