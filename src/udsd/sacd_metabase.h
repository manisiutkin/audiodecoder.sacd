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

#pragma once

#include "udsd_reader_sacd.h"

/*
#define PUGIXML_HEADER_ONLY
#include <pugixml.hpp>
using namespace pugi;
*/

class sacd_metabase_t {
/*
	pfc::string8 store_id;
	pfc::string8 store_path;
	pfc::string8 xml_file;
	xml_document xml_doc;
	bool         initialized;
public:
	sacd_metabase_t(udsd_reader_sacd_t* disc, const char* metafile);
	~sacd_metabase_t();
	void get_track_info(uint32_t track_number, file_info& track_info);
	void set_track_info(uint32_t track_number, const file_info& track_info, bool is_linked = false);
	void get_albumart(uint32_t albumart_id, std::vector<t_uint8>& albumart_data);
	void set_albumart(uint32_t albumart_id, const std::vector<t_uint8>& albumart_data);
	void commit();
private:
	bool init_xmldoc(const char* store_type);
	void delete_track_tags(xml_node node_track, const char* tag_type, bool is_linked);
	void insert_track_tag(xml_node node_track, const char* tag_type, const char* tag_name, const char* tag_value);
	xml_node get_node(const char* tag_type, const char* att_id, bool create = false);
	xml_node new_node(const char* tag_type, const char* att_id);
*/
};
