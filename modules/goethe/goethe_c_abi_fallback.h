/**************************************************************************/
/*  goethe_c_abi_fallback.h                                               */
/**************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GoetheDialogueHandle GoetheDialogueHandle;
typedef struct GoetheRunnerHandle GoetheRunnerHandle;
typedef struct GoetheProjectHandle GoetheProjectHandle;

typedef enum GoetheRunnerState {
	GOETHE_RUNNER_IDLE = 0,
	GOETHE_RUNNER_STARTING = 1,
	GOETHE_RUNNER_RUNNING = 2,
	GOETHE_RUNNER_WAITING_CHOICE = 3,
	GOETHE_RUNNER_SUSPENDED = 4,
	GOETHE_RUNNER_COMPLETED = 5,
	GOETHE_RUNNER_ABORTED = 6
} GoetheRunnerState;

typedef enum GoetheEventType {
	GOETHE_EVENT_STARTED = 0,
	GOETHE_EVENT_SHOWN = 1,
	GOETHE_EVENT_CHOICE_OFFERED = 2,
	GOETHE_EVENT_CHOICE_SELECTED = 3,
	GOETHE_EVENT_SUSPENDED = 4,
	GOETHE_EVENT_RESUMED = 5,
	GOETHE_EVENT_COMPLETED = 6,
	GOETHE_EVENT_ABORTED = 7
} GoetheEventType;

typedef struct GoetheRunnerCallbacks {
	void *user_data;
	int (*has_flag)(void *user_data, const char *key);
	int (*get_var)(void *user_data, const char *key, char *out_buf, int out_buf_size);
	int (*get_quest_state)(void *user_data, const char *quest_id, char *out_buf, int out_buf_size);
	int (*get_objective_state)(void *user_data, const char *quest_id, const char *objective_id, char *out_buf, int out_buf_size);
	int (*is_chapter_active)(void *user_data, const char *id);
	int (*has_entered_area)(void *user_data, const char *id);
	int (*has_visited_dialogue)(void *user_data, const char *id);
	int (*has_selected_choice)(void *user_data, const char *id);
	int (*has_event_fired)(void *user_data, const char *id);
	int (*ms_since_event)(void *user_data, const char *id);
	int (*has_inventory_item)(void *user_data, const char *id);
	int (*is_door_locked)(void *user_data, const char *id);
	int (*has_access)(void *user_data, const char *id);
	void (*set_flag)(void *user_data, const char *key, int value);
	void (*set_var)(void *user_data, const char *key, const char *value);
	void (*on_effect_json)(void *user_data, const char *effect_json);
	void (*on_node_presented_json)(void *user_data, const char *node_json);
	void (*on_event)(void *user_data, int event_type, const char *dialogue_id, const char *node_id, const char *choice_id, const char *reason);
} GoetheRunnerCallbacks;

#ifdef __cplusplus
}
#endif
