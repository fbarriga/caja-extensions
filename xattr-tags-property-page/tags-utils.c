/*
 *  Caja xattr tags property page extension
 *
 *  Copyright (C) 2017 Felipe Barriga Richards
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Authors: Felipe Barriga Richards <spam@felipebarriga.cl>
 */

#include <gtk/gtk.h>

#include "tags-utils.h"

GList *
xattr_tags_str_to_list (const char *xattr_tags_str)
{
  GList *result = NULL;

  if (xattr_tags_str != NULL) {
    gchar **tags = g_strsplit (xattr_tags_str, ",", -1);
    for (int i = 0; tags[i]; i++) {
      gchar *tag = g_strstrip(tags[i]);
      result = g_list_append (result, g_strdup (tag));
    }
    g_strfreev (tags);
  }
  result = g_list_sort (result, (GCompareFunc) g_ascii_strcasecmp);

  return result;
}

gchar *
xattr_tags_list_to_str (const GList *tags)
{
  gchar *result = NULL;

  const GList *tags_iter = NULL;
  for (tags_iter = tags; tags_iter; tags_iter = tags_iter->next) {
    gchar *tmp;

    if (result != NULL)
      tmp = g_strconcat (result, ",", tags_iter->data, NULL);
    else
      tmp = g_strdup (tags_iter->data);

    if (result != NULL)
      g_free (result);

    result = tmp;
  }

  return result;
}