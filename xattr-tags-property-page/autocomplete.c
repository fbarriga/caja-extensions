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

#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

#include "autocomplete.h"

static inline gint
recent_tags_capacity ()
{
  GSettings *settings;
  gint capacity;

  settings = g_settings_new (CAJA_XATTRS_SCHEMA);
  capacity = g_settings_get_int (settings,
                                 CAJA_XATTRS_SCHEMA_RECENT_TAGS_CAPACITY_KEY);
  g_object_unref (settings);
  return capacity;
}

gboolean
recent_tags_enabled ()
{
  GSettings *settings;
  gboolean enabled;

  settings = g_settings_new (CAJA_XATTRS_SCHEMA);
  enabled = g_settings_get_boolean (settings,
                                    CAJA_XATTRS_SCHEMA_RECENT_TAGS_ENABLED_KEY);
  g_object_unref (settings);
  return enabled;
}

gchar **
get_recent_tags ()
{
  gchar **tags;
  GSettings *settings;

  settings = g_settings_new (CAJA_XATTRS_SCHEMA);
  tags = g_settings_get_strv (settings,
                              CAJA_XATTRS_SCHEMA_RECENT_TAGS_LIST_KEY);
  g_object_unref (settings);
  return tags;
}

// TODO: check if already exists before adding it
void
add_item_autocomplete (GtkEntryCompletion *completion, const gchar *tag)
{
  GtkTreeModel *model = gtk_entry_completion_get_model (
      GTK_ENTRY_COMPLETION (completion));

  GtkTreeIter iter;
  gtk_list_store_append (GTK_LIST_STORE (model), &iter);
  gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, g_strdup (tag), -1);
}

void
del_item_autocomplete (GtkEntryCompletion *completion, const gchar *tag)
{
  GtkTreeModel *model = gtk_entry_completion_get_model (
      GTK_ENTRY_COMPLETION (completion));

  GtkTreeIter iter;
  gboolean valid;

  valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(model), &iter);

  gchar *value;
  while (valid) {
    gtk_tree_model_get (model, &iter, 0, &value, -1);
    if (g_strcmp0 (tag, value) == 0) {
      valid = gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
    } else {
      valid = gtk_tree_model_iter_next (model, &iter);
    }

    g_free (value);
  }
}

gboolean
add_to_recent_tags (const gchar *_tag)
{
  if (!recent_tags_enabled ()) {
    return FALSE;
  }

  gchar *tag = g_strstrip(g_strdup (_tag));
  if (strlen (tag) == 0) {
    g_print ("add_to_recent_tags: tag cannot be an empty string.\n");
    g_free (tag);
    return FALSE;
  }

  gchar **tags = get_recent_tags ();
  if (g_strv_contains ((const gchar *const *) tags, tag)) {
//    g_print ("add_to_recent_tags: already have that tag, skipping.\n");
    g_free (tag);
    g_strfreev (tags);
    return FALSE;
  }

  GArray *array = g_array_new (TRUE, TRUE, sizeof (gchar *));
  for (gint i = 0; tags[i] != NULL; ++i) {
    array = g_array_append_val(array, tags[i]);
  }
  array = g_array_append_val(array, tag);

  gboolean result;
  GSettings *settings = g_settings_new (CAJA_XATTRS_SCHEMA);
  result = g_settings_set_strv (settings,
                                CAJA_XATTRS_SCHEMA_RECENT_TAGS_LIST_KEY,
                                (const gchar **) array->data);

  g_object_unref (settings);
  g_strfreev (tags);
  g_array_free (array, TRUE);

  g_free (tag);

  return result;
}

GtkTreeModel *
create_completion_model (GList *existing_tags)
{
  GtkListStore *store = gtk_list_store_new (1, G_TYPE_STRING);

  if (!recent_tags_enabled ()) {
    return GTK_TREE_MODEL (store);
  }

  GtkTreeIter iter;
  gchar **tags = get_recent_tags ();

  /* only autocomplete useful tags */
  for (int i = 0; tags[i] != NULL; ++i) {
    GList *found = g_list_find_custom (existing_tags,
                                       tags[i],
                                       (GCompareFunc) strcmp);
    if (found == NULL) {
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, g_strdup (tags[i]), -1);
    }
  }

  return GTK_TREE_MODEL (store);
}

GtkEntryCompletion *
setup_entry_completion (GtkEntry *entry, GList *tags)
{
  GtkEntryCompletion *entry_completion = gtk_entry_completion_new ();
  gtk_entry_set_completion (GTK_ENTRY (entry), entry_completion);
  //g_object_unref (entry_completion);

  GtkTreeModel *completion_model = create_completion_model (tags);
  gtk_entry_completion_set_model (entry_completion, completion_model);
  //g_object_unref (completion_model);

  gtk_entry_completion_set_text_column (entry_completion, 0);

  return entry_completion;
}
