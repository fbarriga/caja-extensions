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

#include <config.h>
#include "caja-xattr-tags-property-page-extension.h"

#include "autocomplete.h"
#include "tags-tree-view.h"
#include "tags-utils.h"

#include <glib/gi18n.h>
#include <libcaja-extension/caja-property-page-provider.h>

#define G_FILE_ATTRIBUTE_XATTR_XDG_TAGS "xattr::xdg.tags"

static void
load_tags_async (CajaXattrTagsPropertiesPage *page);

static void
update_file_tags_async (CajaXattrTagsPropertiesPage *page);

static void
property_page_provider_iface_init (CajaPropertyPageProviderIface *iface);

struct CajaXattrTagsPropertiesPageDetails {
    gchar *uri;
    GList *tags;

    GCancellable *cancellable;

    GtkWidget *vbox;
    GtkWidget *loading_label;
    GtkWidget *tag_entry;
    GtkWidget *treeview;

    GtkEntryCompletion *completion;
};

typedef struct {
    GObject parent;
} CajaXattrTagsPropertiesPageProvider;

typedef struct {
    GObjectClass parent;
} CajaXattrTagsPropertiesPageProviderClass;

static GType caja_xattr_tags_property_page_type = 0;

G_DEFINE_TYPE (CajaXattrTagsPropertiesPage, caja_xattr_tags_properties_page,
               GTK_TYPE_BOX);

static void
caja_xattr_tags_properties_page_finalize (GObject *object)
{
  CajaXattrTagsPropertiesPage *page = CAJA_XATTS_TAGS_PROPERTIES_PAGE (object);

  if (page->details) {
    g_free(page->details->uri);
    // TODO: Check if this is releasing everything...
    g_list_free(page->details->tags);

    if (page->details->cancellable) {
      g_cancellable_cancel (page->details->cancellable);
      g_object_unref (page->details->cancellable);
      page->details->cancellable = NULL;
    }
  }

  G_OBJECT_CLASS (caja_xattr_tags_properties_page_parent_class)->finalize (object);
}

static GtkWidget *
append_label (GtkWidget  *vbox,
              const char *str)
{
  GtkWidget *label = gtk_label_new (NULL);

  gtk_label_set_markup (GTK_LABEL (label), str);
#if GTK_CHECK_VERSION (3, 16, 0)
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_label_set_yalign (GTK_LABEL (label), 0);
#else
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
#endif

  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  return label;
}

static void
add_tag_click (GtkButton *button,
               gpointer  *data)
{
  CajaXattrTagsPropertiesPage *page = CAJA_XATTS_TAGS_PROPERTIES_PAGE (data);

  gchar *tag
      = g_strdup (gtk_entry_get_text (GTK_ENTRY(page->details->tag_entry)));

  tag = g_strstrip(tag);
  if (strlen (tag) == 0) {
    g_print ("tag cannot be an empty string.\n");
    g_free (tag);
    return;
  }

  // TODO: validate characters (comma not allowed?)

  GList *found = g_list_find_custom (page->details->tags,
                                     tag,
                                     (GCompareFunc) strcmp);
  if (found != NULL) {
    g_print ("tag already exists: %s\n", tag);
    g_free (tag);
    load_tags_async (page);
    return;
  }

  add_to_recent_tags (tag);
  del_item_autocomplete (page->details->completion, tag);

  // we could insert it in a sorted way but the speed gain isn't too much...
  page->details->tags = g_list_append (page->details->tags, tag);
  page->details->tags = g_list_sort (page->details->tags,
                                     (GCompareFunc) g_ascii_strcasecmp);

  gtk_entry_set_text (GTK_ENTRY(page->details->tag_entry), "");

  // TODO: make it async
  update_file_tags_async (page);
  load_tags_async (page);
  gtk_widget_grab_focus (page->details->tag_entry);
}

void
view_selected_foreach_func (GtkTreeModel  *model,
                            GtkTreePath   *path,
                            GtkTreeIter   *iter,
                            gpointer       data)
{
  CajaXattrTagsPropertiesPage *page = CAJA_XATTS_TAGS_PROPERTIES_PAGE (data);

  gchar *tag;
  gtk_tree_model_get (GTK_TREE_MODEL (model), iter, COL_NAME, &tag, -1);

  GList *found = g_list_find_custom (page->details->tags,
                                     tag,
                                     (GCompareFunc) strcmp);
  if (found == NULL) {
    g_print ("view_selected_foreach_func: tag not found: %s\n", tag);
    g_free(tag);
    return;
  }

  add_item_autocomplete (page->details->completion, tag);
  g_free(tag);

  page->details->tags = g_list_delete_link (page->details->tags, found);

  // TODO: make it async
  update_file_tags_async (page);
  load_tags_async (page);
}

// TODO: only enable button when something is selected...
static void
delete_tag_click (GtkButton *button,
                  gpointer  *data)
{
  CajaXattrTagsPropertiesPage *page = CAJA_XATTS_TAGS_PROPERTIES_PAGE (data);

  GtkTreeView *treeview = GTK_TREE_VIEW (page->details->treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  gtk_tree_selection_selected_foreach (selection,
                                       view_selected_foreach_func,
                                       data);

  //g_object_unref (selection);
}

static void
tag_list_selection_changed (GtkTreeSelection *selection, GtkButton *button)
{
  if (gtk_tree_selection_count_selected_rows (selection) > 0) {
    gtk_widget_set_sensitive (GTK_WIDGET (button), TRUE);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
  }
}

static void
update_property_page_widget (CajaXattrTagsPropertiesPage *page)
{
//  g_print("update_property_page_widget: called\n");
  GtkTreeView *treview = GTK_TREE_VIEW(page->details->treeview);

  // TODO: can we update the model instead of creating a new one ?
//  GtkTreeModel *old_model = gtk_tree_view_get_model (treview);
  GtkTreeModel *new_model = create_and_fill_model (page->details->tags);
  gtk_tree_view_set_model (treview, new_model);

//  g_object_unref (new_model);
//  g_object_unref (old_model);
}

static void
create_property_page_widget (CajaXattrTagsPropertiesPage *page)
{
//  g_print("create_property_page_widget: called\n");

  GtkWidget *grid = gtk_grid_new ();
  gtk_widget_set_vexpand (grid, TRUE);
  gtk_widget_set_hexpand (grid, TRUE);
  gtk_grid_set_row_homogeneous (GTK_GRID (grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 6);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 6);

  /* add new tag entry */
  page->details->tag_entry = gtk_entry_new ();
  gtk_widget_set_vexpand (page->details->tag_entry, FALSE);
  g_signal_connect(page->details->tag_entry,
                   "activate",
                   G_CALLBACK (add_tag_click),
                   page);
  gtk_grid_attach (GTK_GRID (grid), page->details->tag_entry, 0, 0, 1, 1);
  gtk_widget_show (page->details->tag_entry);
  page->details->completion = setup_entry_completion (
      GTK_ENTRY (page->details->tag_entry),
      page->details->tags);


  GtkWidget *button;
  /* Add button */
  button = gtk_button_new_with_label (_("Add Tag"));
  gtk_widget_set_vexpand (button, FALSE);
  g_signal_connect(button, "clicked", G_CALLBACK (add_tag_click), page);
  gtk_grid_attach (GTK_GRID (grid), button, 1, 0, 1, 1);
  gtk_widget_show (button);

  /* scrollable window holding the list of tags */
  GtkWidget *scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_grid_attach (GTK_GRID (grid), scrolled_window, 0, 1, 2, 1);
  gtk_widget_show (scrolled_window);

  /* list of tags */
  page->details->treeview = create_view_and_model (page->details->tags);
  gtk_widget_set_vexpand (page->details->treeview, TRUE);
  gtk_widget_set_hexpand (page->details->treeview, TRUE);
  gtk_container_add (GTK_CONTAINER (scrolled_window), page->details->treeview);
  gtk_widget_show (page->details->treeview);

  /* Delete button */
  GtkWidget *del_button = gtk_button_new_with_label (_("Delete Selected"));
  gtk_widget_set_vexpand (del_button, FALSE);
  g_signal_connect(del_button, "clicked", G_CALLBACK (delete_tag_click), page);
  gtk_grid_attach (GTK_GRID (grid), del_button, 0, 2, 2, 1);
  gtk_widget_show (del_button);

  /* enable / disable delete button if tags are selected */
  gtk_widget_set_sensitive (GTK_WIDGET (del_button), FALSE);
  GtkTreeSelection *selection
      = gtk_tree_view_get_selection (GTK_TREE_VIEW (page->details->treeview));
  g_signal_connect (selection, "changed",
                    G_CALLBACK (tag_list_selection_changed),
                    del_button);


  gtk_box_pack_start (GTK_BOX(page->details->vbox),
                      grid,
                      TRUE,
                      TRUE,
                      0);

  gtk_widget_show_all (GTK_WIDGET(page));
}

static void
file_query_info_callback (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      data)
{
//  g_print ("file_query_info_callback: called\n");

  CajaXattrTagsPropertiesPage *page = CAJA_XATTS_TAGS_PROPERTIES_PAGE (data);

  GError *error = NULL;
  GFileInfo *info = g_file_query_info_finish (G_FILE (source_object),
                                              res,
                                              &error);

  if (page->details->loading_label != NULL) {
    gtk_widget_destroy (page->details->loading_label);
    page->details->loading_label = NULL;
  }

  if (info != NULL) {
    if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_XATTR_XDG_TAGS)) {
      const gchar *tags = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_XATTR_XDG_TAGS);
      page->details->tags = xattr_tags_str_to_list (tags);
    } else {
      g_print ("file_query_info_callback: attribute not found\n");
    }
    g_object_unref (info);
  }

  if (error) {
    g_print ("file_query_info_callback: error: TODO\n");
    g_error_free (error);
  }

  if (page->details->treeview == NULL) {
    create_property_page_widget(page);
  } else {
    update_property_page_widget(page);
  }
}

static void
load_tags_async (CajaXattrTagsPropertiesPage *page)
{
  GFile *file;

  g_assert (CAJA_IS_XATTR_TAGS_PROPERTIES_PAGE (page));
  g_assert (page->details->uri != NULL);

  page->details->cancellable = g_cancellable_new ();
  file = g_file_new_for_uri (page->details->uri);

  g_file_query_info_async (file,
                           G_FILE_ATTRIBUTE_XATTR_XDG_TAGS,
                           0,
                           G_PRIORITY_DEFAULT,
                           page->details->cancellable,
                           file_query_info_callback,
                           page);

  g_object_unref (file);
}


static void
update_file_tags_async (CajaXattrTagsPropertiesPage *page)
{
  GFile *file;

  g_assert (CAJA_IS_XATTR_TAGS_PROPERTIES_PAGE (page));
  g_assert (page->details->uri != NULL);

  page->details->cancellable = g_cancellable_new ();
  file = g_file_new_for_uri (page->details->uri);

  // TODO: this should be async!

  gchar *list = xattr_tags_list_to_str (page->details->tags);
  if (list == NULL) {
    list = g_strdup("");
  }

//  g_print ("update_file_tags_async: new list: %s\n", list);
  g_file_set_attribute_string (file,
                               G_FILE_ATTRIBUTE_XATTR_XDG_TAGS,
                               list,
                               G_FILE_QUERY_INFO_NONE,
                               page->details->cancellable,
                               NULL);

  g_free (list);
  g_object_unref (file);
}

static void
caja_xattr_tags_properties_page_class_init (CajaXattrTagsPropertiesPageClass *class)
{
//  g_print ("caja_xattr_tags_properties_page_class_init: called\n");

  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (class);

  object_class->finalize = caja_xattr_tags_properties_page_finalize;

  g_type_class_add_private (object_class,
                            sizeof (CajaXattrTagsPropertiesPageDetails));
}

static void
caja_xattr_tags_properties_page_init (CajaXattrTagsPropertiesPage *page)
{
//  g_print ("caja_xattr_tags_properties_page_init: called\n");

  page->details = G_TYPE_INSTANCE_GET_PRIVATE (page,
                                               CAJA_TYPE_XATTR_TAGS_PROPERTIES_PAGE,
                                               CajaXattrTagsPropertiesPageDetails);

  gtk_orientable_set_orientation (GTK_ORIENTABLE (page),
                                  GTK_ORIENTATION_VERTICAL);

  gtk_box_set_homogeneous (GTK_BOX (page), FALSE);
  gtk_box_set_spacing (GTK_BOX (page), 2);
  gtk_container_set_border_width (GTK_CONTAINER (page), 6);

  page->details->vbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  page->details->loading_label =
      append_label (page->details->vbox, _("Loading tags..."));
  gtk_box_pack_start (GTK_BOX (page),
                      page->details->vbox,
                      FALSE, TRUE, 2);

  gtk_widget_show_all (GTK_WIDGET (page));
}

static GList *
get_property_pages (CajaPropertyPageProvider *provider,
                    GList *files)
{
//    g_print ("get_property_pages: called\n");

  GList *pages = NULL;

  /* Only show the property page if 1 file is selected */
  if (!files || files->next != NULL) {
    g_print ("get_property_pages: cannot select more than 1 file.\n");
    return NULL;
  }

  CajaFileInfo *file = CAJA_FILE_INFO (files->data);
  CajaXattrTagsPropertiesPage *page =
      g_object_new (CAJA_TYPE_XATTR_TAGS_PROPERTIES_PAGE, NULL);

  page->details->uri = caja_file_info_get_uri (file);
  load_tags_async (page);

  CajaPropertyPage *real_page = caja_property_page_new
      ("CajaXattrTagsPropertiesPage::property_page",
       gtk_label_new (_("Tags")),
       GTK_WIDGET (page));
  pages = g_list_append (pages, real_page);

  return pages;
}

static void
caja_xattr_tags_property_page_instance_init (
    CajaXattrTagsPropertiesPage *cajaXattrTagsPropertyPage)
{
//    g_print ("caja_xattr_tags_property_page_instance_init: called\n");
}

static void
property_page_provider_iface_init (CajaPropertyPageProviderIface *iface)
{
//    g_print ("property_page_provider_iface_init: called\n");
}

static void
caja_xattr_tags_property_page_provider_iface_init (
    CajaPropertyPageProviderIface *iface)
{
//    g_print ("caja_xattr_tags_property_page_provider_iface_init: called\n");
  iface->get_pages = get_property_pages;
}


static void
caja_xattr_tags_properties_page_provider_init (
    CajaXattrTagsPropertiesPageProvider *sidebar)
{
//    g_print ("caja_xattr_tags_properties_page_provider_init: called\n");
}

static void
caja_xattr_tags_properties_page_provider_class_init (
    CajaXattrTagsPropertiesPageProviderClass *class)
{
//    g_print ("caja_xattr_tags_properties_page_provider_class_init: called\n");
}


void
caja_xattr_tags_property_page_register_type (GTypeModule *module)
{
//    g_print ("caja_xattr_tags_property_page_register_type: called\n");

  static const GTypeInfo info = {
      sizeof (CajaXattrTagsPropertiesPageClass),
      (GBaseInitFunc) NULL,
      (GBaseFinalizeFunc) NULL,
      (GClassInitFunc) caja_xattr_tags_properties_page_class_init,
      NULL,
      NULL,
      sizeof (CajaXattrTagsPropertiesPage),
      0,
      (GInstanceInitFunc) caja_xattr_tags_property_page_instance_init,
  };

  caja_xattr_tags_property_page_type = g_type_module_register_type (
      module,
      G_TYPE_OBJECT,
      "CajaXattrTagsPropertyPage",
      &info,
      0
  );


  static const GInterfaceInfo property_page_provider_iface_info = {
      (GInterfaceInitFunc) caja_xattr_tags_property_page_provider_iface_init,
      NULL,
      NULL
  };

  g_type_module_add_interface (module,
                               CAJA_TYPE_XATTR_TAGS_PROPERTIES_PAGE,
                               CAJA_TYPE_PROPERTY_PAGE_PROVIDER,
                               &property_page_provider_iface_info);

}

void
caja_module_initialize (GTypeModule *module)
{
  g_print ("Initializing caja-xattr-tags-property-page extension\n");
  caja_xattr_tags_property_page_register_type (module);
}

void
caja_module_shutdown (void)
{
}

/* List all the extension types.  */
void
caja_module_list_types (const GType **types,
                        int          *num_types)
{
  static GType type_list[1];

  type_list[0] = CAJA_TYPE_XATTR_TAGS_PROPERTIES_PAGE;

  *types = type_list;
  *num_types = 1;
}