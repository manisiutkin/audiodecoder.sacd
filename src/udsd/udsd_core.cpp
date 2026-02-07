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

#include "udsd_core.h"
#include "udsd_reader_dsdiff.h"
#include "udsd_reader_dsf.h"
#include "udsd_reader_sacd.h"

#include "../Settings.h"
#include "kodi/Filesystem.h"

#include <memory>

namespace
{

std::string getFileExt(const std::string& s)
{
  size_t i = s.rfind('.', s.length());
  if (i != std::string::npos)
  {
    return (s.substr(i + 1, s.length() - i));
  }

  return ("");
}

bool icasecmp(const std::string& l, const std::string& r)
{
  return l.size() == r.size() && equal(l.cbegin(), l.cend(), r.cbegin(),
                                       [](std::string::value_type l1, std::string::value_type r1) {
                                         return toupper(l1) == toupper(r1);
                                       });
}

} /* namespace */

static bool is_sacd_prefix(const std::string& path)
{
  return path.compare(0, strlen(SACD_PREF), SACD_PREF) == 0;
}

static void get_sacd_md5(const char* path, uint8_t md5[16])
{
  /*
  auto md5_string = pfc::string_filename(p_path);
  for (auto i = 0u; i < 16; i++)
  {
    if (2 * i + 1 < md5_string.get_length())
    {
      p_md5[i] = uint8_t(std::strtol(md5_string.subString(2 * i, 2).get_ptr(), nullptr, 16));
    }
  }
  */
}

bool udsd_core_t::g_is_our_content_type(const std::string& type)
{
  return false;
}

bool udsd_core_t::g_is_our_path(const std::string& path, const std::string& ext)
{
  std::string filename_ext = kodi::vfs::GetFileName(path);
  return ((icasecmp(ext, "ISO") || icasecmp(ext, "SACD") || icasecmp(ext, "DAT")) && udsd_reader_sacd_t::g_is_sacd(path.c_str())) ||
         icasecmp(ext, "DFF") || icasecmp(ext, "DSF"); /* ||
         (icasecmp(filename_ext, "") || icasecmp(filename_ext, "MASTER1.TOC")) &&
             path.length() > 7 && sacd_disc_t::g_is_sacd(path[7]);*/
}

std::list<udsd_handler_t> udsd_core_t::s_cached_handlers;
std::mutex                udsd_core_t::s_cached_mutex;

void udsd_core_t::g_quit() {
	std::lock_guard<std::mutex> lock(s_cached_mutex);
	s_cached_handlers.clear();
}

udsd_core_t::udsd_core_t() : media_type(media_type_e::INVALID), access_mode(ACCESS_MODE_NULL)
{
}

bool udsd_core_t::open(const std::string& path)
{
  std::string filename_ext = kodi::vfs::GetFileName(path);
  std::string ext = getFileExt(filename_ext);
  auto is_sacd_disc = false;
  media_type = media_type_e::INVALID;

  if (icasecmp(ext, "ISO"))
  {
    media_type = media_type_e::ISO;
  }
  else if (icasecmp(ext, "DAT"))
  {
    media_type = media_type_e::ISO;
  }
  else if (icasecmp(ext, "SACD"))
  {
    media_type = media_type_e::ISO;
  }
  else if (icasecmp(ext, "DFF"))
  {
    media_type = media_type_e::DSDIFF;
  }
  else if (icasecmp(ext, "DSF"))
  {
    media_type = media_type_e::DSF;
  }
  /* TODO Find way to handle disk read direct */
  /*else if ((icasecmp(filename_ext, "") || icasecmp(filename_ext, "MASTER1.TOC")) &&
           path.length() > 7 && sacd_disc_t::g_is_sacd(path[7]))
  {
    media_type = media_type_e::ISO;
    is_sacd_disc = true;
  }*/
  if (media_type == media_type_e::INVALID)
  {
    kodi::Log(ADDON_LOG_ERROR, "unsupported format '%s'", path.c_str());
    return false;
  }
  auto cached{ false };
  uint8_t md5[16];
  udsd_handler_t handler;
  auto is_disc = is_sacd_prefix(path);
  if (is_disc)
  {
    std::lock_guard<std::mutex> lock(s_cached_mutex);
    get_sacd_md5(path.data(), md5);
    for (auto i = s_cached_handlers.begin(); i != s_cached_handlers.end(); i++)
    {
      if (std::memcmp(md5, i->m_md5, 16) == 0)
      {
        handler = *i;
        cached = true;
        break;
      }
    }
  }
  if (cached)
  {
    udsd_media = handler.m_media;
  }
  else
  {
    if (is_disc)
    {
      udsd_media = std::make_shared<udsd_media_disc_t>();
      if (!udsd_media)
      {
        kodi::Log(ADDON_LOG_ERROR, "memory overflow '%s'", path.c_str());
        return false;
      }
    }
    else
    {
      udsd_media = std::make_shared<udsd_media_file_t>();
      if (!udsd_media)
      {
        kodi::Log(ADDON_LOG_ERROR, "memory overflow '%s'", path.c_str());
        return false;
      }
    }
  }
  if (!cached)
  {
    if (!udsd_media->open(path, false))
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to open media type %i on '%s'", media_type, path.c_str());
      return false;
    }
    if (is_disc)
    {
      handler.m_media = udsd_media;
    }
  }
  if (cached)
  {
    udsd_reader = handler.m_reader;
  }
  else
  {
    switch (media_type)
    {
      case media_type_e::ISO:
        udsd_reader = std::make_shared<udsd_reader_sacd_t>();
        if (!udsd_reader)
        {
          kodi::Log(ADDON_LOG_ERROR, "memory overflow '%s'", path.c_str());
          return false;
        }
        break;
      case media_type_e::DSDIFF:
        udsd_reader = std::make_shared<udsd_reader_dsdiff_t>();
        if (!udsd_reader)
        {
          kodi::Log(ADDON_LOG_ERROR, "memory overflow '%s'", path.c_str());
          return false;
        }
        break;
      case media_type_e::DSF:
        udsd_reader = std::make_shared<udsd_reader_dsf_t>();
        if (!udsd_reader)
        {
          kodi::Log(ADDON_LOG_ERROR, "memory overflow '%s'", path.c_str());
          return false;
        }
        break;
      default:
        kodi::Log(ADDON_LOG_ERROR, "unsupported format %i on '%s'", media_type, path.c_str());
        return false;
    }
    if (cached)
    {
      udsd_reader = handler.m_reader;
    }
    else
    {
      if (!udsd_reader->open(udsd_media.get()))
      {
        kodi::Log(ADDON_LOG_ERROR, "Failed to open media reader for type %i on '%s'", media_type, path.c_str());
        return false;
      }
      if (is_disc)
      {
        handler.m_reader = udsd_reader;
      }
    }
  }
  access_mode = ACCESS_MODE_NULL;
  switch (CSACDSettings::GetInstance().GetPlaybackArea())
  {
    case AREA_NULL:
      access_mode |= ACCESS_MODE_TWOCH | ACCESS_MODE_MULCH;
      break;
    case AREA_TWOCH:
      access_mode |= ACCESS_MODE_TWOCH;
      break;
    case AREA_MULCH:
      access_mode |= ACCESS_MODE_MULCH;
      break;
  }
  if (CSACDSettings::GetInstance().GetEditedMaster())
  {
    access_mode |= ACCESS_MODE_EDITED_MASTER_TRACK;
  }
  udsd_reader->set_mode(access_mode);
  /*
  if (CSACDSettings::GetInstance().GetMetabaseTags())
  {
    switch (media_type)
    {
      case media_type_e::ISO:
        if (cached)
        {
          sacd_metabase = handler.m_metabase;
        }
        else
        {
          auto metabase_path = path;
          metabase_path.replace(path.rfind("."), 3, "xml");
          sacd_metabase = std::make_shared<sacd_metabase_t>(
              static_cast<udsd_reader_sacd_t*>(udsd_reader.get()),
              (!is_disc && CSACDSettings::GetInstance().GetStoreTagsWithISO())
              ? metabase_path : nullptr);
          if (is_disc)
          {
            handler.m_metabase = sacd_metabase;
          }
        }
        break;
      default:
        break;
    }
  }
  */
  switch (media_type)
  {
    case media_type_e::ISO:
      if (!cached)
      {
        std::lock_guard<std::mutex> lock(s_cached_mutex);
        static_cast<udsd_reader_sacd_t*>(udsd_reader.get())->get_md5(handler.m_md5);
        s_cached_handlers.push_front(handler);
      }
      break;
  }
  return true;
}
