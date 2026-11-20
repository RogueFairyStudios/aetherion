/**************************************************************************/
/*  goethe_dialogue_runner.cpp                                            */
/**************************************************************************/

#include "goethe_dialogue_runner.h"

#include "core/io/json.h"
#include "core/object/class_db.h"

#include <cstring>

static int _write_utf8_buf(const String &p_str, char *p_buf, int p_buf_sz) {
	CharString cs = p_str.utf8();
	int len = cs.length();
	if (p_buf != nullptr && p_buf_sz > 0) {
		int copy_len = len < (p_buf_sz - 1) ? len : (p_buf_sz - 1);
		if (copy_len > 0) {
			memcpy(p_buf, cs.get_data(), static_cast<size_t>(copy_len));
		}
		p_buf[copy_len] = '\0';
	}
	return len;
}

GoetheDialogueRunner::CallScope::CallScope(GoetheDialogueRunner *p_self) :
		self(p_self) {
	++self->call_depth_;
}

GoetheDialogueRunner::CallScope::~CallScope() {
	if (--self->call_depth_ == 0) {
		self->_flush_signals();
	}
}

bool GoetheDialogueRunner::_begin_core_call(const char *p_what) const {
	ERR_FAIL_COND_V_MSG(in_core_call_, false,
			String("GoetheDialogueRunner.") + p_what + "() was called from a context hook. Hooks must not re-enter the runner.");
	return true;
}

void GoetheDialogueRunner::_queue_signal(const StringName &p_name) {
	PendingSignal sig;
	sig.name = p_name;
	pending_.push_back(sig);
}

void GoetheDialogueRunner::_queue_signal(const StringName &p_name, const Variant &p_arg0) {
	PendingSignal sig;
	sig.name = p_name;
	sig.arg0 = p_arg0;
	sig.argc = 1;
	pending_.push_back(sig);
}

void GoetheDialogueRunner::_queue_signal(const StringName &p_name, const Variant &p_arg0, const Variant &p_arg1) {
	PendingSignal sig;
	sig.name = p_name;
	sig.arg0 = p_arg0;
	sig.arg1 = p_arg1;
	sig.argc = 2;
	pending_.push_back(sig);
}

void GoetheDialogueRunner::_flush_signals() {
	if (flushing_ || pending_.is_empty()) {
		return;
	}
	flushing_ = true;
	Ref<GoetheDialogueRunner> keep_alive(this);
	for (int i = 0; i < pending_.size(); ++i) {
		const PendingSignal sig = pending_[i];
		switch (sig.argc) {
			case 0:
				emit_signal(sig.name);
				break;
			case 1:
				emit_signal(sig.name, sig.arg0);
				break;
			default:
				emit_signal(sig.name, sig.arg0, sig.arg1);
				break;
		}
	}
	pending_.clear();
	flushing_ = false;
}

Object *GoetheDialogueRunner::_context_object() const {
	return context_id_.is_valid() ? ObjectDB::get_instance(context_id_) : nullptr;
}

Variant GoetheDialogueRunner::_call_hook_1(const char *p_method, const char *p_arg0) const {
	Object *target = _context_object();
	if (target == nullptr) {
		return Variant();
	}
	Variant a0 = String::utf8(p_arg0 ? p_arg0 : "");
	const Variant *args[1] = { &a0 };
	Callable::CallError ce;
	return target->callp(StringName(p_method), args, 1, ce);
}

Variant GoetheDialogueRunner::_call_hook_2(const char *p_method, const char *p_arg0, const char *p_arg1) const {
	Object *target = _context_object();
	if (target == nullptr) {
		return Variant();
	}
	Variant a0 = String::utf8(p_arg0 ? p_arg0 : "");
	Variant a1 = String::utf8(p_arg1 ? p_arg1 : "");
	const Variant *args[2] = { &a0, &a1 };
	Callable::CallError ce;
	return target->callp(StringName(p_method), args, 2, ce);
}

int GoetheDialogueRunner::_cb_has_flag(void *p_ud, const char *p_key) {
	return bool(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_has_flag", p_key)) ? 1 : 0;
}

int GoetheDialogueRunner::_cb_get_var(void *p_ud, const char *p_key, char *p_buf, int p_buf_sz) {
	Variant v = static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_get_var", p_key);
	return _write_utf8_buf(v.stringify(), p_buf, p_buf_sz);
}

int GoetheDialogueRunner::_cb_get_quest_state(void *p_ud, const char *p_qid, char *p_buf, int p_buf_sz) {
	Variant v = static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_get_quest_state", p_qid);
	return _write_utf8_buf(v.stringify(), p_buf, p_buf_sz);
}

int GoetheDialogueRunner::_cb_get_objective_state(void *p_ud, const char *p_qid, const char *p_oid, char *p_buf, int p_buf_sz) {
	Variant v = static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_2("goethe_get_objective_state", p_qid, p_oid);
	return _write_utf8_buf(v.stringify(), p_buf, p_buf_sz);
}

int GoetheDialogueRunner::_cb_is_chapter_active(void *p_ud, const char *p_id) {
	return bool(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_is_chapter_active", p_id)) ? 1 : 0;
}

int GoetheDialogueRunner::_cb_has_entered_area(void *p_ud, const char *p_id) {
	return bool(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_has_entered_area", p_id)) ? 1 : 0;
}

int GoetheDialogueRunner::_cb_has_visited_dialogue(void *p_ud, const char *p_id) {
	return bool(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_has_visited_dialogue", p_id)) ? 1 : 0;
}

int GoetheDialogueRunner::_cb_has_selected_choice(void *p_ud, const char *p_id) {
	return bool(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_has_selected_choice", p_id)) ? 1 : 0;
}

int GoetheDialogueRunner::_cb_has_event_fired(void *p_ud, const char *p_id) {
	return bool(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_has_event_fired", p_id)) ? 1 : 0;
}

int GoetheDialogueRunner::_cb_ms_since_event(void *p_ud, const char *p_id) {
	return static_cast<int>(int64_t(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_ms_since_event", p_id)));
}

int GoetheDialogueRunner::_cb_has_inventory_item(void *p_ud, const char *p_id) {
	return bool(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_has_inventory_item", p_id)) ? 1 : 0;
}

int GoetheDialogueRunner::_cb_is_door_locked(void *p_ud, const char *p_id) {
	return bool(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_is_door_locked", p_id)) ? 1 : 0;
}

int GoetheDialogueRunner::_cb_has_access(void *p_ud, const char *p_id) {
	return bool(static_cast<GoetheDialogueRunner *>(p_ud)->_call_hook_1("goethe_has_access", p_id)) ? 1 : 0;
}

void GoetheDialogueRunner::_cb_set_flag(void *p_ud, const char *p_key, int p_val) {
	auto *self = static_cast<GoetheDialogueRunner *>(p_ud);
	if (Object *obj = self->_context_object()) {
		Variant a0 = String::utf8(p_key ? p_key : "");
		Variant a1 = (p_val != 0);
		const Variant *args[2] = { &a0, &a1 };
		Callable::CallError ce;
		obj->callp(StringName("goethe_set_flag"), args, 2, ce);
	}
}

void GoetheDialogueRunner::_cb_set_var(void *p_ud, const char *p_key, const char *p_val) {
	auto *self = static_cast<GoetheDialogueRunner *>(p_ud);
	if (Object *obj = self->_context_object()) {
		Variant a0 = String::utf8(p_key ? p_key : "");
		Variant a1 = String::utf8(p_val ? p_val : "");
		const Variant *args[2] = { &a0, &a1 };
		Callable::CallError ce;
		obj->callp(StringName("goethe_set_var"), args, 2, ce);
	}
}

void GoetheDialogueRunner::_cb_on_effect_json(void *p_ud, const char *p_json) {
	auto *self = static_cast<GoetheDialogueRunner *>(p_ud);
	Variant parsed = JSON::parse_string(String::utf8(p_json ? p_json : "{}"));
	Dictionary data = parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();

	Object *target = self->_context_object();
	if (target != nullptr && target->has_method(StringName("goethe_apply_effect"))) {
		Variant a0 = data;
		const Variant *args[1] = { &a0 };
		Callable::CallError ce;
		target->callp(StringName("goethe_apply_effect"), args, 1, ce);
	}
	self->_queue_signal("effect_triggered", data.duplicate(true));
}

void GoetheDialogueRunner::_cb_on_node_presented_json(void *p_ud, const char *p_json) {
	auto *self = static_cast<GoetheDialogueRunner *>(p_ud);
	Variant parsed = JSON::parse_string(String::utf8(p_json ? p_json : "{}"));
	Dictionary node = parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
	self->current_node_ = node;
	self->_queue_signal("node_presented", node.duplicate(true));
}

void GoetheDialogueRunner::_cb_on_event(void *p_ud, int p_type, const char *p_dlg_id, const char *p_node_id, const char *p_choice_id, const char *p_reason) {
	auto *self = static_cast<GoetheDialogueRunner *>(p_ud);
	String dlg_id = String::utf8(p_dlg_id ? p_dlg_id : "");
	String node_id = String::utf8(p_node_id ? p_node_id : "");

	switch (p_type) {
		case GOETHE_EVENT_STARTED:
			self->_queue_signal("dialogue_started", dlg_id);
			break;
		case GOETHE_EVENT_CHOICE_OFFERED: {
			Array choices = self->current_node_.get("choices", Array());
			self->_queue_signal("choices_offered", node_id, choices.duplicate(true));
		} break;
		case GOETHE_EVENT_CHOICE_SELECTED:
			self->_queue_signal("choice_selected", node_id, p_choice_id ? String::utf8(p_choice_id) : String());
			break;
		case GOETHE_EVENT_SUSPENDED:
			self->_queue_signal("dialogue_suspended", node_id);
			break;
		case GOETHE_EVENT_RESUMED:
			self->_queue_signal("dialogue_resumed", node_id);
			break;
		case GOETHE_EVENT_COMPLETED:
			self->_queue_signal("dialogue_completed", dlg_id);
			break;
		case GOETHE_EVENT_ABORTED:
			self->_queue_signal("dialogue_aborted", dlg_id, p_reason ? String::utf8(p_reason) : String());
			break;
		default:
			break;
	}
}

GoetheRunnerCallbacks GoetheDialogueRunner::_build_callbacks() {
	GoetheRunnerCallbacks cbs{};
	cbs.user_data = this;

	Object *target = _context_object();
	auto has = [target](const char *p_method) {
		return target != nullptr && target->has_method(StringName(p_method));
	};

	if (has("goethe_has_flag")) {
		cbs.has_flag = &_cb_has_flag;
	}
	if (has("goethe_get_var")) {
		cbs.get_var = &_cb_get_var;
	}
	if (has("goethe_get_quest_state")) {
		cbs.get_quest_state = &_cb_get_quest_state;
	}
	if (has("goethe_get_objective_state")) {
		cbs.get_objective_state = &_cb_get_objective_state;
	}
	if (has("goethe_is_chapter_active")) {
		cbs.is_chapter_active = &_cb_is_chapter_active;
	}
	if (has("goethe_has_entered_area")) {
		cbs.has_entered_area = &_cb_has_entered_area;
	}
	if (has("goethe_has_visited_dialogue")) {
		cbs.has_visited_dialogue = &_cb_has_visited_dialogue;
	}
	if (has("goethe_has_selected_choice")) {
		cbs.has_selected_choice = &_cb_has_selected_choice;
	}
	if (has("goethe_has_event_fired")) {
		cbs.has_event_fired = &_cb_has_event_fired;
	}
	if (has("goethe_ms_since_event")) {
		cbs.ms_since_event = &_cb_ms_since_event;
	}
	if (has("goethe_has_inventory_item")) {
		cbs.has_inventory_item = &_cb_has_inventory_item;
	}
	if (has("goethe_is_door_locked")) {
		cbs.is_door_locked = &_cb_is_door_locked;
	}
	if (has("goethe_has_access")) {
		cbs.has_access = &_cb_has_access;
	}
	if (has("goethe_set_flag")) {
		cbs.set_flag = &_cb_set_flag;
	}
	if (has("goethe_set_var")) {
		cbs.set_var = &_cb_set_var;
	}

	cbs.on_effect_json = &_cb_on_effect_json;
	cbs.on_node_presented_json = &_cb_on_node_presented_json;
	cbs.on_event = &_cb_on_event;
	return cbs;
}

GoetheDialogueRunner::GoetheDialogueRunner() {
	const GoetheApiTable &api = GoetheApi::get();
	if (api.available && api.runner_create) {
		handle_ = api.runner_create();
	}
}

GoetheDialogueRunner::~GoetheDialogueRunner() {
	const GoetheApiTable &api = GoetheApi::get();
	if (handle_ != nullptr && api.runner_destroy) {
		api.runner_destroy(handle_);
		handle_ = nullptr;
	}
}

Error GoetheDialogueRunner::start(const Ref<GoetheDialogue> &p_dialogue) {
	ERR_FAIL_COND_V_MSG(p_dialogue.is_null() || !p_dialogue->is_loaded(), ERR_INVALID_PARAMETER,
			"GoetheDialogueRunner.start() requires a loaded GoetheDialogue.");
	const GoetheApiTable &api = GoetheApi::get();
	ERR_FAIL_COND_V_MSG(!api.available || handle_ == nullptr, ERR_UNAVAILABLE,
			"Goethe native runtime library is not available.");
	if (!_begin_core_call("start")) {
		return ERR_BUSY;
	}

	CallScope scope(this);
	dialogue_ref_ = p_dialogue;
	current_node_ = Dictionary();

	GoetheRunnerCallbacks cbs = _build_callbacks();
	api.runner_set_callbacks(handle_, &cbs);
	api.runner_set_raw_keys(handle_, raw_keys_ ? 1 : 0);

	in_core_call_ = true;
	int rc = api.runner_start(handle_, p_dialogue->get_native_handle());
	in_core_call_ = false;
	return rc == 0 ? OK : FAILED;
}

void GoetheDialogueRunner::advance() {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr || !_begin_core_call("advance")) {
		return;
	}
	CallScope scope(this);
	in_core_call_ = true;
	api.runner_advance(handle_);
	in_core_call_ = false;
}

bool GoetheDialogueRunner::select_choice(const String &p_choice_id) {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr || !is_waiting_choice() || !_begin_core_call("select_choice")) {
		return false;
	}
	CallScope scope(this);
	CharString cid = p_choice_id.utf8();
	in_core_call_ = true;
	int accepted = api.runner_select_choice(handle_, cid.get_data());
	in_core_call_ = false;
	return accepted != 0;
}

void GoetheDialogueRunner::suspend() {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr || !_begin_core_call("suspend")) {
		return;
	}
	CallScope scope(this);
	in_core_call_ = true;
	api.runner_suspend(handle_);
	in_core_call_ = false;
}

void GoetheDialogueRunner::resume() {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr || !_begin_core_call("resume")) {
		return;
	}
	CallScope scope(this);
	in_core_call_ = true;
	api.runner_resume(handle_);
	in_core_call_ = false;
}

void GoetheDialogueRunner::abort(const String &p_reason) {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr || !is_active() || !_begin_core_call("abort")) {
		return;
	}
	CallScope scope(this);
	CharString reason = p_reason.utf8();
	in_core_call_ = true;
	api.runner_abort(handle_, reason.get_data());
	in_core_call_ = false;
}

GoetheDialogueRunner::State GoetheDialogueRunner::get_state() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr) {
		return STATE_IDLE;
	}
	return static_cast<State>(api.runner_get_state(handle_));
}

bool GoetheDialogueRunner::is_active() const {
	const GoetheApiTable &api = GoetheApi::get();
	return api.available && handle_ != nullptr && api.runner_is_active(handle_) != 0;
}

bool GoetheDialogueRunner::is_waiting_choice() const {
	const GoetheApiTable &api = GoetheApi::get();
	return api.available && handle_ != nullptr && api.runner_is_waiting_choice(handle_) != 0;
}

Ref<GoetheDialogue> GoetheDialogueRunner::get_dialogue() const {
	return dialogue_ref_;
}

String GoetheDialogueRunner::get_dialogue_id() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr) {
		return String();
	}
	return String::utf8(api.runner_get_dialogue_id(handle_));
}

String GoetheDialogueRunner::get_current_node_id() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr) {
		return String();
	}
	return String::utf8(api.runner_get_current_node_id(handle_));
}

Dictionary GoetheDialogueRunner::get_current_node() const {
	return current_node_.duplicate(true);
}

Array GoetheDialogueRunner::get_choices() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || handle_ == nullptr || !_begin_core_call("get_choices")) {
		return Array();
	}
	in_core_call_ = true;
	String json = GoetheApi::take_c_string(api.runner_get_choices_json(handle_));
	in_core_call_ = false;
	Variant parsed = JSON::parse_string(json);
	return parsed.get_type() == Variant::ARRAY ? Array(parsed) : Array();
}

void GoetheDialogueRunner::set_context(Object *p_context) {
	context_id_ = p_context != nullptr ? p_context->get_instance_id() : ObjectID();
}

Object *GoetheDialogueRunner::get_context() const {
	return _context_object();
}

void GoetheDialogueRunner::set_raw_keys(bool p_enable) {
	raw_keys_ = p_enable;
	const GoetheApiTable &api = GoetheApi::get();
	if (api.available && handle_ != nullptr) {
		api.runner_set_raw_keys(handle_, p_enable ? 1 : 0);
	}
}

bool GoetheDialogueRunner::is_raw_keys() const {
	return raw_keys_;
}

void GoetheDialogueRunner::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "dialogue"), &GoetheDialogueRunner::start);
	ClassDB::bind_method(D_METHOD("advance"), &GoetheDialogueRunner::advance);
	ClassDB::bind_method(D_METHOD("select_choice", "choice_id"), &GoetheDialogueRunner::select_choice);
	ClassDB::bind_method(D_METHOD("suspend"), &GoetheDialogueRunner::suspend);
	ClassDB::bind_method(D_METHOD("resume"), &GoetheDialogueRunner::resume);
	ClassDB::bind_method(D_METHOD("abort", "reason"), &GoetheDialogueRunner::abort, DEFVAL(String()));

	ClassDB::bind_method(D_METHOD("get_state"), &GoetheDialogueRunner::get_state);
	ClassDB::bind_method(D_METHOD("is_active"), &GoetheDialogueRunner::is_active);
	ClassDB::bind_method(D_METHOD("is_waiting_choice"), &GoetheDialogueRunner::is_waiting_choice);
	ClassDB::bind_method(D_METHOD("get_dialogue"), &GoetheDialogueRunner::get_dialogue);
	ClassDB::bind_method(D_METHOD("get_dialogue_id"), &GoetheDialogueRunner::get_dialogue_id);
	ClassDB::bind_method(D_METHOD("get_current_node_id"), &GoetheDialogueRunner::get_current_node_id);
	ClassDB::bind_method(D_METHOD("get_current_node"), &GoetheDialogueRunner::get_current_node);
	ClassDB::bind_method(D_METHOD("get_choices"), &GoetheDialogueRunner::get_choices);

	ClassDB::bind_method(D_METHOD("set_context", "context"), &GoetheDialogueRunner::set_context);
	ClassDB::bind_method(D_METHOD("get_context"), &GoetheDialogueRunner::get_context);
	ClassDB::bind_method(D_METHOD("set_raw_keys", "enable"), &GoetheDialogueRunner::set_raw_keys);
	ClassDB::bind_method(D_METHOD("is_raw_keys"), &GoetheDialogueRunner::is_raw_keys);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "context", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_context", "get_context");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "raw_keys"), "set_raw_keys", "is_raw_keys");

	ADD_SIGNAL(MethodInfo("dialogue_started", PropertyInfo(Variant::STRING, "dialogue_id")));
	ADD_SIGNAL(MethodInfo("node_presented", PropertyInfo(Variant::DICTIONARY, "node")));
	ADD_SIGNAL(MethodInfo("choices_offered", PropertyInfo(Variant::STRING, "node_id"), PropertyInfo(Variant::ARRAY, "choices")));
	ADD_SIGNAL(MethodInfo("choice_selected", PropertyInfo(Variant::STRING, "node_id"), PropertyInfo(Variant::STRING, "choice_id")));
	ADD_SIGNAL(MethodInfo("effect_triggered", PropertyInfo(Variant::DICTIONARY, "effect")));
	ADD_SIGNAL(MethodInfo("dialogue_suspended", PropertyInfo(Variant::STRING, "node_id")));
	ADD_SIGNAL(MethodInfo("dialogue_resumed", PropertyInfo(Variant::STRING, "node_id")));
	ADD_SIGNAL(MethodInfo("dialogue_completed", PropertyInfo(Variant::STRING, "dialogue_id")));
	ADD_SIGNAL(MethodInfo("dialogue_aborted", PropertyInfo(Variant::STRING, "dialogue_id"), PropertyInfo(Variant::STRING, "reason")));

	BIND_ENUM_CONSTANT(STATE_IDLE);
	BIND_ENUM_CONSTANT(STATE_STARTING);
	BIND_ENUM_CONSTANT(STATE_RUNNING);
	BIND_ENUM_CONSTANT(STATE_WAITING_CHOICE);
	BIND_ENUM_CONSTANT(STATE_SUSPENDED);
	BIND_ENUM_CONSTANT(STATE_COMPLETED);
	BIND_ENUM_CONSTANT(STATE_ABORTED);
}
