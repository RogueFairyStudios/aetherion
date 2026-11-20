/**************************************************************************/
/*  test_goethe.h                                                         */
/**************************************************************************/

#pragma once

#include "../goethe_api.h"
#include "../goethe_dialogue.h"
#include "../goethe_dialogue_runner.h"
#include "../goethe_locale_manager.h"
#include "../goethe_project.h"

#include "core/object/class_db.h"
#include "tests/test_macros.h"

namespace TestGoethe {

TEST_CASE("[Modules][Goethe] Classes are registered in ClassDB") {
	CHECK(ClassDB::class_exists("GoetheDialogue"));
	CHECK(ClassDB::class_exists("GoetheDialogueRunner"));
	CHECK(ClassDB::class_exists("GoetheProject"));
	CHECK(ClassDB::class_exists("GoetheLocaleManager"));
}

TEST_CASE("[Modules][Goethe] Pre-built Goethe SDK runtime round-trip when staged") {
	if (!GoetheApi::is_available()) {
		return;
	}

	Ref<GoetheDialogue> dlg;
	dlg.instantiate();
	const String yaml =
			"kind: dialogue\n"
			"id: aetherion_smoke\n"
			"startNode: greet\n"
			"nodes:\n"
			"  - id: greet\n"
			"    speaker: Alice\n"
			"    line:\n"
			"      text: \"Hello from Goethe SDK!\"\n";

	CHECK(dlg->load_from_yaml(yaml) == OK);
	CHECK(dlg->is_loaded());
	CHECK(dlg->get_id() == "aetherion_smoke");
	CHECK(dlg->get_node_count() == 1);

	Ref<GoetheDialogueRunner> runner;
	runner.instantiate();
	CHECK(runner->start(dlg) == OK);
	CHECK(runner->get_current_node_id() == "greet");
	Dictionary presented = runner->get_current_node();
	CHECK(String(presented.get("text", "")) == "Hello from Goethe SDK!");
	runner->advance();
	CHECK(runner->get_state() == GoetheDialogueRunner::STATE_COMPLETED);
}

} // namespace TestGoethe
