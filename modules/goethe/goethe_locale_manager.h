/**************************************************************************/
/*  goethe_locale_manager.h                                               */
/**************************************************************************/

#pragma once

#include "scene/main/node.h"

class GoetheLocaleManager : public Node {
	GDCLASS(GoetheLocaleManager, Node);

	bool sync_godot_locale_ = false;
	String last_godot_locale_;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_locale(const String &p_locale);
	String get_locale() const;

	void load_catalog(const String &p_path);
	void load_from_package(const String &p_package_path, const String &p_locale);
	void unload_locale(const String &p_locale);
	void unload_all();

	String translate(const String &p_key) const;
	String translate_with_params(const String &p_key, const Dictionary &p_params) const;
	String translate_plural(const String &p_key, int64_t p_n) const;
	String translate_plural_with_params(const String &p_key, int64_t p_n, const Dictionary &p_params) const;

	bool has_key(const String &p_key) const;
	bool has_key_in_locale(const String &p_key, const String &p_locale) const;

	void set_sync_with_godot_locale(bool p_enable);
	bool get_sync_with_godot_locale() const;
};
