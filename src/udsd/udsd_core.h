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

#include "udsd_media.h"
#include "udsd_reader.h"
#include "sacd_metabase.h"
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>

static const char* SACD_PREF{ "sacd://" };

class ATTR_DLL_LOCAL udsd_handler_t
{
public:
	uint8_t                          m_md5[16]{};
	std::shared_ptr<udsd_media_t>    m_media;
	std::shared_ptr<udsd_reader_t>   m_reader;
	std::shared_ptr<sacd_metabase_t> m_metabase;
};

class ATTR_DLL_LOCAL udsd_core_t
{
	static std::list<udsd_handler_t> s_cached_handlers;
	static std::mutex                s_cached_mutex;
protected:
	media_type_e                     media_type;
	uint32_t                         access_mode;
	std::shared_ptr<udsd_media_t>    udsd_media;
	std::shared_ptr<udsd_reader_t>   udsd_reader;
	std::shared_ptr<sacd_metabase_t> sacd_metabase;
public:
  static bool g_is_our_content_type(const std::string& type);
  static bool g_is_our_path(const std::string& path, const std::string& ext);
	static void g_quit();
	udsd_core_t();
  bool open(const std::string& path);
};
