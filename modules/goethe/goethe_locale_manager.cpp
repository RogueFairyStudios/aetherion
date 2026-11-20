/**************************************************************************/
/*  goethe_locale_manager.cpp                                             */
/**************************************************************************/

#include "goethe_locale_manager.h"

#include "goethe_api.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/string/translation_server.h"

static void _dict_to_c_params(const Dictionary &p_params, Vector<CharString> &r_kbuf, Vector<CharString> &r_vbuf, Vector<const char *> &r_kptrs, Vector<const char *> &r_vptrs) {
	Array keys = p_params.keys();
	int n = keys.size();
	r_kbuf.resize(n);
	r_vbuf.resize(n);
	r_kptrs.resize(n);
	r_vptrs.resize(n);
	for (int i = 0; i < n; ++i) {
		r_kbuf.write[i] = String(keys[i]).utf8();
		r_vbuf.write[i] = p_params[keys[i]].stringify().utf8();
		r_kptrs.write[i] = r_kbuf[i].get_data();
		r_vptrs.write[i] = r_vbuf[i].get_data();
	}
}

void GoetheLocaleManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_locale", "locale"), &GoetheLocaleManager::set_locale);
	ClassDB::bind_method(D_METHOD("get_locale"), &GoetheLocaleManager::get_locale);

	ClassDB::bind_method(D_METHOD("load_catalog", "path"), &GoetheLocaleManager::load_catalog);
	ClassDB::bind_method(D_METHOD("load_from_package", "package_path", "locale"), &GoetheLocaleManager::load_from_package);
	ClassDB::bind_method(D_METHOD("unload_locale", "locale"), &GoetheLocaleManager::unload_locale);
	ClassDB::bind_method(D_METHOD("unload_all"), &GoetheLocaleManager::unload_all);

	ClassDB::bind_method(D_METHOD("translate", "key"), &GoetheLocaleManager::translate);
	ClassDB::bind_method(D_METHOD("translate_with_params", "key", "params"), &GoetheLocaleManager::translate_with_params);
	ClassDB::bind_method(D_METHOD("translate_plural", "key", "n"), &GoetheLocaleManager::translate_plural);
	ClassDB::bind_method(D_METHOD("translate_plural_with_params", "key", "n", "params"), &GoetheLocaleManager::translate_plural_with_params);

	ClassDB::bind_method(D_METHOD("has_key", "key"), &GoetheLocaleManager::has_key);
	ClassDB::bind_method(D_METHOD("has_key_in_locale", "key", "locale"), &GoetheLocaleManager::has_key_in_locale);

	ClassDB::bind_method(D_METHOD("set_sync_with_godot_locale", "enable"), &GoetheLocaleManager::set_sync_with_godot_locale);
	ClassDB::bind_method(D_METHOD("get_sync_with_godot_locale"), &GoetheLocaleManager::get_sync_with_godot_locale);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sync_with_godot_locale"), "set_sync_with_godot_locale", "get_sync_with_godot_locale");
	ADD_SIGNAL(MethodInfo("locale_changed", PropertyInfo(Variant::STRING, "new_locale")));
}

void GoetheLocaleManager::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS && sync_godot_locale_) {
		TranslationServer *ts = TranslationServer::get_singleton();
		if (ts != nullptr) {
			String cur = ts->get_locale();
			if (cur != last_godot_locale_) {
				last_godot_locale_ = cur;
				set_locale(cur);
			}
		}
	}
}

void GoetheLocaleManager::set_locale(const String &p_locale) {
	const GoetheApiTable &api = GoetheApi::get();
	if (api.available && api.set_locale) {
		CharString cs = p_locale.utf8();
		api.set_locale(cs.get_data());
	}
	emit_signal("locale_changed", p_locale);
}

String GoetheLocaleManager::get_locale() const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || !api.get_locale) {
		return "en";
	}
	return GoetheApi::take_c_string(api.get_locale());
}

void GoetheLocaleManager::load_catalog(const String &p_path) {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available) {
		return;
	}
	CharString content;
	if (GoetheApi::read_godot_text_file(p_path, content) == OK) {
		api.locale_load_string(content.get_data());
		return;
	}
	CharString fp = p_path.utf8();
	api.locale_load_file(fp.get_data());
}

void GoetheLocaleManager::load_from_package(const String &p_package_path, const String &p_locale) {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || !api.locale_load_from_package) {
		return;
	}
	String resolved = p_package_path;
	if (ProjectSettings::get_singleton() && (p_package_path.begins_with("res://") || p_package_path.begins_with("user://"))) {
		resolved = ProjectSettings::get_singleton()->globalize_path(p_package_path);
	}
	CharString pkg = resolved.utf8();
	CharString loc = p_locale.utf8();
	api.locale_load_from_package(pkg.get_data(), loc.get_data());
}

void GoetheLocaleManager::unload_locale(const String &p_locale) {
	const GoetheApiTable &api = GoetheApi::get();
	if (api.available && api.locale_unload) {
		CharString loc = p_locale.utf8();
		api.locale_unload(loc.get_data());
	}
}

void GoetheLocaleManager::unload_all() {
	const GoetheApiTable &api = GoetheApi::get();
	if (api.available && api.locale_unload_all) {
		api.locale_unload_all();
	}
}

String GoetheLocaleManager::translate(const String &p_key) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || !api.translate) {
		return p_key;
	}
	CharString k = p_key.utf8();
	return GoetheApi::take_c_string(api.translate(k.get_data()));
}

String GoetheLocaleManager::translate_with_params(const String &p_key, const Dictionary &p_params) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || !api.translate_with_params) {
		return p_key;
	}
	Vector<CharString> kbuf, vbuf;
	Vector<const char *> kptrs, vptrs;
	_dict_to_c_params(p_params, kbuf, vbuf, kptrs, vptrs);
	CharString k = p_key.utf8();
	return GoetheApi::take_c_string(api.translate_with_params(k.get_data(), kptrs.ptr(), vptrs.ptr(), kptrs.size()));
}

String GoetheLocaleManager::translate_plural(const String &p_key, int64_t p_n) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || !api.translate_plural) {
		return p_key;
	}
	CharString k = p_key.utf8();
	return GoetheApi::take_c_string(api.translate_plural(k.get_data(), p_n));
}

String GoetheLocaleManager::translate_plural_with_params(const String &p_key, int64_t p_n, const Dictionary &p_params) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || !api.translate_plural_with_params) {
		return p_key;
	}
	Vector<CharString> kbuf, vbuf;
	Vector<const char *> kptrs, vptrs;
	_dict_to_c_params(p_params, kbuf, vbuf, kptrs, vptrs);
	CharString k = p_key.utf8();
	return GoetheApi::take_c_string(api.translate_plural_with_params(k.get_data(), p_n, kptrs.ptr(), vptrs.ptr(), kptrs.size()));
}

bool GoetheLocaleManager::has_key(const String &p_key) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || !api.locale_has_key) {
		return false;
	}
	CharString k = p_key.utf8();
	return api.locale_has_key(k.get_data()) != 0;
}

bool GoetheLocaleManager::has_key_in_locale(const String &p_key, const String &p_locale) const {
	const GoetheApiTable &api = GoetheApi::get();
	if (!api.available || !api.locale_has_key_in_locale) {
		return false;
	}
	CharString k = p_key.utf8();
	CharString loc = p_locale.utf8();
	return api.locale_has_key_in_locale(k.get_data(), loc.get_data()) != 0;
}

void GoetheLocaleManager::set_sync_with_godot_locale(bool p_enable) {
	sync_godot_locale_ = p_enable;
	set_process(p_enable);
	if (p_enable && TranslationServer::get_singleton()) {
		last_godot_locale_ = TranslationServer::get_singleton()->get_locale();
		set_locale(last_godot_locale_);
	}
}

bool GoetheLocaleManager::get_sync_with_godot_locale() const {
	return sync_godot_locale_;
}
