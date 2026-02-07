/*
 *  Copyright (C) 2020 Team Kodi <https://kodi.tv>
 *  Copyright (c) 2011-2026 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

/*
 * Code taken SACD Decoder plugin (from foo_input_sacd) by Team Kodi.
 * Original author Maxim V.Anisiutkin.
 */

#include "sacd_metabase.h"
#include <array>
#include <sys/stat.h>

auto MB_TAG_ROOT       = "root";
auto MB_TAG_STORE      = "store";
auto MB_TAG_TRACK      = "track";
auto MB_TAG_INFO       = "info";
auto MB_TAG_META       = "meta";
auto MB_TAG_REPLAYGAIN = "replaygain";
auto MB_TAG_ALBUMART   = "albumart";

auto MB_ATT_ID         = "id";
auto MB_ATT_NAME       = "name";
auto MB_ATT_TYPE       = "type";
auto MB_ATT_VALUE      = "value";
auto MB_ATT_VALSEP     = ";";
auto MB_ATT_VERSION    = "version";

auto METABASE_CATALOG  = "sacd_metabase";
auto METABASE_TYPE     = "SACD";
auto METABASE_VERSION  = "1.2";

/*
* 
static auto utf2xml = [](auto src) {
	auto dst{ src };
	dst.replace_string("\r", "&#13;");
	dst.replace_string("\n", "&#10;");
	return dst;
};

static auto xml2utf = [](auto src) {
	auto dst{ src };
	dst.replace_string("&#13;", "\r");
	dst.replace_string("&#10;", "\n");
	return dst;
};

static auto is_linkable_tag = [](auto tag_name) {
	return !((tag_name == "dynamic range") || (tag_name == "album dynamic range"));
};

sacd_metabase_t::sacd_metabase_t(udsd_reader_sacd_t* disc, const char* metafile) {
	initialized = false;
	store_id = disc->get_md5();
	if (store_id.empty()) {
		return;
	}
	store_path = core_api::get_profile_path();
	store_path.end_with_slash();
	store_path.add_string(METABASE_CATALOG);
	auto store_file = store_path;
	store_file.end_with_slash();
	store_file.add_string(store_id);
	store_file.add_string(".xml");
	if (!metafile) {
		if (!filesystem::g_exists(store_path, fb2k::noAbort)) {
			filesystem::g_create_directory(store_path, fb2k::noAbort);
			if (filesystem::g_exists(store_path, fb2k::noAbort)) {
				console::info(pfc::string_printf("SACD metabase folder \"%s\" has been created", store_path.toString()));
			}
			else {
				popup_message::g_show(pfc::string_printf("Cannot create SACD metabase folder \"%s\"", store_path.toString()), METABASE_CATALOG, popup_message::icon_error);
			}
		}
	}
	else {
		if (!filesystem::g_exists(metafile, fb2k::noAbort)) {
			if (filesystem::g_exists(store_file, fb2k::noAbort)) {
				filesystem::g_copy(store_file, metafile, fb2k::noAbort);
				if (filesystem::g_exists(metafile, fb2k::noAbort)) {
					console::info(pfc::string_printf("SACD metabase file is copied from \"%s\" to \"%s\"", store_file.toString(), metafile));
				}
				else {
					popup_message::g_show(pfc::string_printf("Cannot copy SACD metabase file to \"%s\"", metafile), METABASE_CATALOG, popup_message::icon_error);
				}
			}
		}
	}
	xml_file = metafile ? pfc::string8(metafile) : store_file;
	initialized = init_xmldoc(METABASE_TYPE);
	if (!initialized) {
		console::error("Cannot initialize XML document");
	}
}

sacd_metabase_t::~sacd_metabase_t() {
}

void sacd_metabase_t::get_track_info(uint32_t track_number, file_info& track_info) {
	auto node_track{ get_node(MB_TAG_TRACK, pfc::format_int(track_number)) };
	if (!node_track) {
		return;
	}
	auto replaygain_info{ track_info.get_replaygain() };
	auto node_tag{ node_track.first_child() };
	while (node_tag) {
		auto node_name{ pfc::string8(node_tag.name()) };
		auto tag_name{ node_tag.attribute(MB_ATT_NAME) };
		auto tag_value{ node_tag.attribute(MB_ATT_VALUE) };
		if (tag_name && tag_value) {
			auto att_name{ pfc::string8(tag_name.value()) };
			auto att_value{ xml2utf(pfc::string8(tag_value.value())) };
			if (node_name == MB_TAG_META) {
				auto head_chunk{ true };
				auto sep_pos{ size_t(0) };
				do {
					sep_pos = att_value.find_first(MB_ATT_VALSEP);
					auto tag_value_head{ pfc::string8() };
					tag_value_head.add_string(att_value, sep_pos);
					while (tag_value_head.length() > 0 && tag_value_head[0] == ' ') {
						tag_value_head.remove_chars(0, 1);
					}
					if (head_chunk) {
						track_info.meta_set(att_name, tag_value_head);
						head_chunk = false;
					}
					else {
						track_info.meta_add(att_name, tag_value_head);
					}
					att_value.remove_chars(0, sep_pos + 1);
				} while (sep_pos != ~0);
			}
			if (node_name == MB_TAG_REPLAYGAIN) {
				if (att_name == "replaygain_track_gain") {
					replaygain_info.set_track_gain_text(att_value);
				}
				if (att_name == "replaygain_track_peak") {
					replaygain_info.set_track_peak_text(att_value);
				}
				if (att_name == "replaygain_album_gain") {
					replaygain_info.set_album_gain_text(att_value);
				}
				if (att_name == "replaygain_album_peak") {
					replaygain_info.set_album_peak_text(att_value);
				}
			}
		}
		node_tag = node_tag.next_sibling();
	}
	track_info.set_replaygain(replaygain_info);
}

void sacd_metabase_t::set_track_info(uint32_t track_number, const file_info& track_info, bool is_linked) {
	auto node_track{ get_node(MB_TAG_TRACK, pfc::format_int(track_number), true) };
	if (!node_track) {
		return;
	}
	delete_track_tags(node_track, MB_TAG_META, is_linked);
	for (auto i = 0u; i < track_info.meta_get_count(); i++) {
		auto tag_name{ pfc::string8(track_info.meta_enum_name(i)) };
		auto tag_value{ pfc::string8(track_info.meta_enum_value(i, 0)) };
		for (auto j = 1u; j < track_info.meta_enum_value_count(i); j++) {
			tag_value += MB_ATT_VALSEP;
			tag_value += track_info.meta_enum_value(i, j);
		}
		if (!is_linked || (is_linked && is_linkable_tag(tag_name))) {
			insert_track_tag(node_track, MB_TAG_META, tag_name, utf2xml(tag_value));
		}
	}
	if (!is_linked) {
		delete_track_tags(node_track, MB_TAG_REPLAYGAIN, false);
		auto replaygain_info{ track_info.get_replaygain() };
		replaygain_info::t_text_buffer tag_value;
		if (replaygain_info.is_track_gain_present()) {
			if (replaygain_info.format_track_gain(tag_value)) {
				insert_track_tag(node_track, MB_TAG_REPLAYGAIN, "replaygain_track_gain", tag_value);
			}
		}
		if (replaygain_info.is_track_peak_present()) {
			if (replaygain_info.format_track_peak(tag_value)) {
				insert_track_tag(node_track, MB_TAG_REPLAYGAIN, "replaygain_track_peak", tag_value);
			}
		}
		if (replaygain_info.is_album_gain_present()) {
			if (replaygain_info.format_album_gain(tag_value)) {
				insert_track_tag(node_track, MB_TAG_REPLAYGAIN, "replaygain_album_gain", tag_value);
			}
		}
		if (replaygain_info.is_album_peak_present()) {
			if (replaygain_info.format_album_peak(tag_value)) {
				insert_track_tag(node_track, MB_TAG_REPLAYGAIN, "replaygain_album_peak", tag_value);
			}
		}
	}
	auto list_tags{ node_track.first_child() };
	if (!list_tags) {
		auto node_store{ node_track.parent() };
		if (node_store) {
			node_store.remove_child(node_track.name());
		}
	}
}

void sacd_metabase_t::get_albumart(uint32_t albumart_id, std::vector<t_uint8>& albumart_data) {
	auto node_albumart{ get_node(MB_TAG_ALBUMART, pfc::format_int(albumart_id)) };
	if (!node_albumart) {
		return;
	}
	auto elem_cdata = node_albumart.first_child();
	if (elem_cdata) {
		auto cdata{ pfc::string8(elem_cdata.value()) };
		pfc::array_t<uint8_t> albumart_decode;
		pfc::base64_decode_array(albumart_decode, cdata);
		albumart_data.assign(albumart_decode.get_ptr(), albumart_decode.get_ptr() + albumart_decode.get_size());
	}
}

void sacd_metabase_t::set_albumart(uint32_t albumart_id, const std::vector<t_uint8>& albumart_data) {
	auto create_node = albumart_data.size() > 0;
	auto node_albumart{ get_node(MB_TAG_ALBUMART, pfc::format_int(albumart_id), create_node) };
	if (!node_albumart) {
		return;
	}
	if (create_node) {
		node_albumart.remove_children();
		pfc::string8 albumart_base64;
		base64_encode(albumart_base64, albumart_data.data(), albumart_data.size());
		node_albumart.append_child(node_cdata).set_value(albumart_base64.get_ptr());
	}
	else {
		node_albumart.parent().remove_child(node_albumart);
	}
}

class xml_media_t : public xml_writer {
	file::ptr f;
public:
	xml_media_t(const char* p_path, filesystem::t_open_mode p_mode) {
		try {
			filesystem::g_open(f, p_path, p_mode, fb2k::noAbort);
		}
		catch (exception_io_not_found const&) {
			console::info(pfc::string_printf("SACD metabase file \"%s\" does not exist", p_path));
		}
	}
	std::vector<uint8_t> read() {
		std::vector<uint8_t> data;
		if (f.is_valid()) {
			data.resize(size_t(f->get_size(fb2k::noAbort)));
			data.resize(f->read(data.data(), data.size(), fb2k::noAbort));
		}
		return data;
	}
	void write(const void* p_data, size_t p_size) {
		if (f.is_valid()) {
			f->write(p_data, p_size, fb2k::noAbort);
		}
	}
};

void sacd_metabase_t::commit() {
	xml_media_t media(xml_file, filesystem::open_mode_write_new);
	xml_doc.save(media, PUGIXML_TEXT("\t"), format_raw, encoding_utf8);
}

bool sacd_metabase_t::init_xmldoc(const char* store_type) {
	auto init_ok{ false };
	xml_media_t media(xml_file, filesystem::open_mode_read);
	auto data = media.read();
	xml_doc.load_buffer(data.data(), data.size(), parse_full, encoding_utf8);
	auto elem_root{ xml_doc.child(MB_TAG_ROOT) };
	if (elem_root) {
		auto elem_store{ elem_root.child(MB_TAG_STORE) };
		if (elem_store) {
			init_ok = true;
		}
	}
	if (!init_ok) {
		xml_doc.reset();
		auto decl_root{ xml_doc.append_child(node_declaration) };
		decl_root.append_attribute("version").set_value("1.0");
		decl_root.append_attribute("encoding").set_value("utf-8");
		auto comm_root{ xml_doc.append_child(node_comment) };
		comm_root.set_value("SACD metabase file");
		auto elem_root{ xml_doc.append_child(node_element) };
		elem_root.set_name(MB_TAG_ROOT);
		auto elem_store{ elem_root.append_child(node_element) };
		elem_store.set_name(MB_TAG_STORE);
		elem_store.append_attribute(MB_ATT_ID).set_value(store_id.c_str());
		elem_store.append_attribute(MB_ATT_TYPE).set_value(store_type);
		elem_store.append_attribute(MB_ATT_VERSION).set_value(METABASE_VERSION);
		init_ok = true;
	}
	return init_ok;
}

void sacd_metabase_t::delete_track_tags(xml_node node_track, const char* tag_type, bool is_linked) {
	bool continue_to_remove{ true };
	while (continue_to_remove) {
		auto elem_tag{ node_track.first_child() };
		continue_to_remove = false;
		while (elem_tag) {
			auto tag_name{ pfc::string8(elem_tag.name()) };
			if (tag_name == tag_type) {
				auto att_name{ elem_tag.attribute(MB_ATT_NAME) };
				if (att_name) {
					if (!is_linked || (is_linked && (tag_name == MB_TAG_META) && is_linkable_tag(tag_name))) {
						node_track.remove_child(elem_tag);
						continue_to_remove = true;
						break;
					}
				}
			}
			elem_tag = elem_tag.next_sibling();
		}
	}
}

void sacd_metabase_t::insert_track_tag(xml_node node_track, const char* tag_type, const char* tag_name, const char* tag_value) {
	auto elem_tag{ node_track.append_child(node_element) };
	elem_tag.set_name(tag_type);
	elem_tag.append_attribute(MB_ATT_NAME).set_value(tag_name);
	elem_tag.append_attribute(MB_ATT_VALUE).set_value(tag_value);
}

xml_node sacd_metabase_t::get_node(const char* tag_type, const char* att_id, bool create) {
	xml_node node_tag_type;
	auto elem_root{ xml_doc.child(MB_TAG_ROOT) };
	if (elem_root) {
		auto elem_store{ elem_root.child(MB_TAG_STORE) };
		if (elem_store) {
			auto elem_att_id{ elem_store.attribute(MB_ATT_ID) };
			auto elem_att_type{ elem_store.attribute(MB_ATT_TYPE) };
			if (elem_att_id && elem_att_type) {
				if ((pfc::string8(elem_att_id.value()) == store_id) && (pfc::string8(elem_att_type.value()) == METABASE_TYPE)) {
					auto elem_tag{ elem_store.first_child() };
					while (elem_tag) {
						auto elem_name{ pfc::string8(elem_tag.name()) };
						if (elem_name == tag_type) {
							auto elem_att_id{ elem_tag.attribute(MB_ATT_ID) };
							if (elem_att_id) {
								if (pfc::string8(elem_att_id.value()) == att_id) {
									node_tag_type = elem_tag;
									break;
								}
							}
						}
						elem_tag = elem_tag.next_sibling();
					}
				}
			}
		}
	}
	if (!node_tag_type && create) {
		node_tag_type = new_node(tag_type, att_id);
	}
	return node_tag_type;
}

xml_node sacd_metabase_t::new_node(const char* tag_type, const char* att_id) {
	auto elem_root{ xml_doc.child(MB_TAG_ROOT) };
	if (elem_root) {
		auto elem_store{ elem_root.child(MB_TAG_STORE) };
		if (elem_store) {
			auto elem_tag{ elem_store.append_child(node_element) };
			elem_tag.set_name(tag_type);
			elem_tag.append_attribute(MB_ATT_ID).set_value(att_id);
			return elem_tag;
		}
	}
	return xml_node{};
}

*/
