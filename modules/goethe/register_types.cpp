/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/

#include "register_types.h"

#include "goethe_api.h"
#include "goethe_dialogue.h"
#include "goethe_dialogue_runner.h"
#include "goethe_locale_manager.h"
#include "goethe_project.h"

#include "core/object/class_db.h"

void initialize_goethe_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(GoetheDialogue);
	GDREGISTER_CLASS(GoetheDialogueRunner);
	GDREGISTER_CLASS(GoetheProject);
	GDREGISTER_CLASS(GoetheLocaleManager);
}

void uninitialize_goethe_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GoetheApi::shutdown();
}
