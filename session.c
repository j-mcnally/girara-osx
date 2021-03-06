/* See LICENSE file for license and copyright information */

#include <stdlib.h>
#include <glib/gi18n-lib.h>

#include "session.h"
#include "settings.h"
#include "datastructures.h"
#include "internal.h"
#include "commands.h"
#include "callbacks.h"
#include "shortcuts.h"
#include "config.h"
#include "utils.h"
#include "input-history.h"

#if GTK_MAJOR_VERSION == 2
#include "gtk2-compat.h"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define DO_PRAGMA(x) _Pragma(#x)
#else
#define DO_PRAGMA(x)
#endif

#define IGNORE_DEPRECATED \
  DO_PRAGMA(GCC diagnostic push) \
  DO_PRAGMA(GCC diagnostic ignored "-Wdeprecated-declarations")
#define UNIGNORE \
  DO_PRAGMA(GCC diagnostic pop)

static int
cb_sort_settings(girara_setting_t* lhs, girara_setting_t* rhs)
{
  return g_strcmp0(girara_setting_get_name(lhs), girara_setting_get_name(rhs));
}

static void
ensure_gettext_initialized(void)
{
  static gsize initialized = 0;
  if (g_once_init_enter(&initialized) == TRUE) {
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    g_once_init_leave(&initialized, 1);
  }
}

girara_session_t*
girara_session_create()
{
  ensure_gettext_initialized();

  girara_session_t* session = g_slice_alloc0(sizeof(girara_session_t));
  session->private_data     = g_slice_alloc0(sizeof(girara_session_private_t));

  /* init values */
  session->bindings.mouse_events       = girara_list_new2(
      (girara_free_function_t) girara_mouse_event_free);
  session->bindings.commands           = girara_list_new2(
      (girara_free_function_t) girara_command_free);
  session->bindings.special_commands   = girara_list_new2(
      (girara_free_function_t) girara_special_command_free);
  session->bindings.shortcuts          = girara_list_new2(
      (girara_free_function_t) girara_shortcut_free);
  session->bindings.inputbar_shortcuts = girara_list_new2(
      (girara_free_function_t) girara_inputbar_shortcut_free);

  session->elements.statusbar_items = girara_list_new2(
      (girara_free_function_t) girara_statusbar_item_free);

  /* settings */
  session->private_data->settings = girara_sorted_list_new2(
      (girara_compare_function_t) cb_sort_settings,
      (girara_free_function_t) girara_setting_free);

  /* init modes */
  session->modes.identifiers  = girara_list_new2(
  (girara_free_function_t) girara_mode_string_free);
  girara_mode_t normal_mode   = girara_mode_add(session, "normal");
  girara_mode_t inputbar_mode = girara_mode_add(session, "inputbar");
  session->modes.normal       = normal_mode;
  session->modes.current_mode = normal_mode;
  session->modes.inputbar     = inputbar_mode;

  /* config handles */
  session->config.handles           = girara_list_new2(
      (girara_free_function_t) girara_config_handle_free);
  session->config.shortcut_mappings = girara_list_new2(
      (girara_free_function_t) girara_shortcut_mapping_free);
  session->config.argument_mappings = girara_list_new2(
      (girara_free_function_t) girara_argument_mapping_free);

  /* command history */
  session->command_history = girara_input_history_new(NULL);

  /* load default values */
  girara_config_load_default(session);

  /* create widgets */
#if GTK_MAJOR_VERSION == 2
  session->gtk.box                      = GTK_BOX(gtk_vbox_new(FALSE, 0));
  session->private_data->gtk.bottom_box = GTK_BOX(gtk_vbox_new(FALSE, 0));
  session->gtk.statusbar_entries        = GTK_BOX(gtk_hbox_new(FALSE, 0));
  session->gtk.tabbar                   = gtk_hbox_new(TRUE, 0);
  session->gtk.inputbar_box             = GTK_BOX(gtk_hbox_new(TRUE, 0));
#else
  session->gtk.box                      = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  session->private_data->gtk.overlay    = gtk_overlay_new();
  session->private_data->gtk.bottom_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  session->gtk.statusbar_entries        = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  session->gtk.tabbar                   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  session->gtk.inputbar_box             = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_box_set_homogeneous(GTK_BOX(session->gtk.tabbar), TRUE);
  gtk_box_set_homogeneous(session->gtk.inputbar_box, TRUE);
#endif
  session->gtk.view              = gtk_scrolled_window_new(NULL, NULL);
  session->gtk.viewport          = gtk_viewport_new(NULL, NULL);
  session->gtk.statusbar         = gtk_event_box_new();
  session->gtk.notification_area = gtk_event_box_new();
  session->gtk.notification_text = gtk_label_new(NULL);
  session->gtk.inputbar_dialog   = GTK_LABEL(gtk_label_new(NULL));
  session->gtk.inputbar_entry    = GTK_ENTRY(gtk_entry_new());
  session->gtk.inputbar          = gtk_event_box_new();
  session->gtk.tabs              = GTK_NOTEBOOK(gtk_notebook_new());

  /* deprecated members */
  IGNORE_DEPRECATED
  session->settings               = session->private_data->settings;
  session->global.command_history = girara_get_command_history(session);
  UNIGNORE

  return session;
}

bool
girara_session_init(girara_session_t* session, const char* sessionname)
{
  if (session == NULL) {
    return false;
  }

  /* window */
  if (session->gtk.embed){
    session->gtk.window = gtk_plug_new(session->gtk.embed);
  } else {
    session->gtk.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  }

  if (sessionname != NULL) {
    gtk_widget_set_name(GTK_WIDGET(session->gtk.window), sessionname);
  }

  GdkGeometry hints = {
    .base_height = 1,
    .base_width  = 1,
    .height_inc  = 0,
    .max_aspect  = 0,
    .max_height  = 0,
    .max_width   = 0,
    .min_aspect  = 0,
    .min_height  = 0,
    .min_width   = 0,
    .width_inc   = 0
  };

  gtk_window_set_geometry_hints(GTK_WINDOW(session->gtk.window), NULL, &hints, GDK_HINT_MIN_SIZE);

#if (GTK_MAJOR_VERSION == 3)
  gtk_window_set_has_resize_grip(GTK_WINDOW(session->gtk.window), FALSE);
#endif

#if (GTK_MAJOR_VERSION == 3)
  gtk_widget_override_background_color(GTK_WIDGET(session->gtk.window), GTK_STATE_NORMAL, &(session->style.default_background));
#else
  gtk_widget_modify_bg(GTK_WIDGET(session->gtk.window), GTK_STATE_NORMAL, &(session->style.default_background));
#endif

  /* view */
  session->signals.view_key_pressed = g_signal_connect(G_OBJECT(session->gtk.view), "key-press-event",
      G_CALLBACK(girara_callback_view_key_press_event), session);

  session->signals.view_button_press_event = g_signal_connect(G_OBJECT(session->gtk.view), "button-press-event",
      G_CALLBACK(girara_callback_view_button_press_event), session);

  session->signals.view_button_release_event = g_signal_connect(G_OBJECT(session->gtk.view), "button-release-event",
      G_CALLBACK(girara_callback_view_button_release_event), session);

  session->signals.view_motion_notify_event = g_signal_connect(G_OBJECT(session->gtk.view), "motion-notify-event",
      G_CALLBACK(girara_callback_view_button_motion_notify_event), session);

  session->signals.view_scroll_event = g_signal_connect(G_OBJECT(session->gtk.view), "scroll-event",
      G_CALLBACK(girara_callback_view_scroll_event), session);

  bool show_hscrollbar = false;
  bool show_vscrollbar = false;

  girara_setting_get(session, "show-h-scrollbar", &show_hscrollbar);
  girara_setting_get(session, "show-v-scrollbar", &show_vscrollbar);

#if (GTK_MAJOR_VERSION == 3)
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(session->gtk.view), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  GtkWidget *vscrollbar = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(session->gtk.view));
  GtkWidget *hscrollbar = gtk_scrolled_window_get_hscrollbar(GTK_SCROLLED_WINDOW(session->gtk.view));

  if (vscrollbar != NULL) {
    gtk_widget_set_visible(GTK_WIDGET(vscrollbar), show_vscrollbar);
  }

  if (hscrollbar != NULL) {
    gtk_widget_set_visible(GTK_WIDGET(hscrollbar), show_hscrollbar);
  }
#else
  GtkPolicyType h_policy, v_policy;
  h_policy = show_hscrollbar ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER;
  v_policy = show_vscrollbar ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER;
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(session->gtk.view), h_policy, v_policy);
#endif

  /* viewport */
  gtk_container_add(GTK_CONTAINER(session->gtk.view), session->gtk.viewport);
  gtk_viewport_set_shadow_type(GTK_VIEWPORT(session->gtk.viewport), GTK_SHADOW_NONE);

#if (GTK_MAJOR_VERSION == 3)
  gtk_widget_override_background_color(GTK_WIDGET(session->gtk.viewport), GTK_STATE_NORMAL, &(session->style.default_background));
#else
  gtk_widget_modify_bg(GTK_WIDGET(session->gtk.viewport), GTK_STATE_NORMAL, &(session->style.default_background));
#endif

  /* statusbar */
  gtk_container_add(GTK_CONTAINER(session->gtk.statusbar), GTK_WIDGET(session->gtk.statusbar_entries));

  /* notification area */
  gtk_container_add(GTK_CONTAINER(session->gtk.notification_area), GTK_WIDGET(session->gtk.notification_text));
  gtk_misc_set_alignment(GTK_MISC(session->gtk.notification_text), 0.0, 0.5);
  gtk_label_set_use_markup(GTK_LABEL(session->gtk.notification_text), TRUE);

  /* inputbar */
  gtk_entry_set_has_frame(session->gtk.inputbar_entry, FALSE);
  gtk_editable_set_editable(GTK_EDITABLE(session->gtk.inputbar_entry), TRUE);

  /* we want inputbar_entry the same height as notification_text and statusbar,
     so that when inputbar_entry is hidden, the size of the bottom_box remains
     the same. We need to get rid of the builtin padding in the GtkEntry
     widget. */

  guint ypadding = 2;         /* total amount of padding (top + bottom) */
  guint leftpadding = 4;      /* left padding */
  girara_setting_get(session, "statusbar-padding", &ypadding);

#if (GTK_MAJOR_VERSION == 3)
  /* gtk_entry_set_inner_border is deprecated since gtk 3.4 and does nothing. */
  GtkCssProvider* provider = gtk_css_provider_new();

  static const char CSS_PATTERN[] = "#bottom_box { border-style: none; margin: 0px 0px 0px 0px; padding:%dpx 0px %dpx %dpx; }";
  char* css = g_strdup_printf(CSS_PATTERN, ypadding - ypadding/2, ypadding/2, leftpadding);
  gtk_css_provider_load_from_data(provider, css, strlen(css), NULL);
  g_free(css);

  GdkDisplay* display = gdk_display_get_default();
  GdkScreen* screen = gdk_display_get_default_screen(display);
  gtk_style_context_add_provider_for_screen(screen,
                                            GTK_STYLE_PROVIDER(provider),
                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);

  gtk_widget_set_name(GTK_WIDGET(session->gtk.inputbar_entry), "bottom_box");
  gtk_widget_set_name(GTK_WIDGET(session->gtk.notification_text), "bottom_box");
#else
  GtkBorder inner_border = {
      .left = leftpadding,
      .right = 0,
      .top = ypadding - ypadding/2,
      .bottom = ypadding/2
  };

  gtk_entry_set_inner_border(session->gtk.inputbar_entry, &inner_border);

  /* obtain the actual inputbar height */
  /* TODO: for some reason does not match */
  GtkRequisition req;
  gtk_widget_size_request(GTK_WIDGET(session->gtk.inputbar_entry), &req);
  /* have no idea where the extra 5 pixels come from. without this, the other
     widgets get larger than the inputbar */
  guint statusbar_height = req.height - 5;

  /* force all widgets to have the same height as inputbar */
  gtk_widget_set_size_request(GTK_WIDGET(session->gtk.inputbar_entry), -1, statusbar_height);
  gtk_widget_set_size_request(GTK_WIDGET(session->gtk.statusbar_entries), -1, statusbar_height);
  gtk_widget_set_size_request(GTK_WIDGET(session->gtk.notification_text), -1, statusbar_height);

  /* set horizontal padding. same for statusbar entries in statusbar.c */
  gtk_misc_set_padding(GTK_MISC(session->gtk.notification_text), leftpadding, 0);
#endif

  session->signals.inputbar_key_pressed = g_signal_connect(
      G_OBJECT(session->gtk.inputbar_entry),
      "key-press-event",
      G_CALLBACK(girara_callback_inputbar_key_press_event),
      session
    );

  session->signals.inputbar_changed = g_signal_connect(
      G_OBJECT(session->gtk.inputbar_entry),
      "changed",
      G_CALLBACK(girara_callback_inputbar_changed_event),
      session
    );

  session->signals.inputbar_activate = g_signal_connect(
      G_OBJECT(session->gtk.inputbar_entry),
      "activate",
      G_CALLBACK(girara_callback_inputbar_activate),
      session
    );

  gtk_box_set_homogeneous(session->gtk.inputbar_box, FALSE);
  gtk_box_set_spacing(session->gtk.inputbar_box, 5);

  /* inputbar box */
  gtk_box_pack_start(GTK_BOX(session->gtk.inputbar_box),  GTK_WIDGET(session->gtk.inputbar_dialog), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(session->gtk.inputbar_box),  GTK_WIDGET(session->gtk.inputbar_entry),  TRUE,  TRUE,  0);
  gtk_container_add(GTK_CONTAINER(session->gtk.inputbar), GTK_WIDGET(session->gtk.inputbar_box));

  /* bottom box */
  gtk_box_set_spacing(session->private_data->gtk.bottom_box, 0);

  gtk_box_pack_end(GTK_BOX(session->private_data->gtk.bottom_box), GTK_WIDGET(session->gtk.inputbar), TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(session->private_data->gtk.bottom_box), GTK_WIDGET(session->gtk.notification_area), TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(session->private_data->gtk.bottom_box), GTK_WIDGET(session->gtk.statusbar), TRUE, TRUE, 0);

  /* tabs */
  gtk_notebook_set_show_border(session->gtk.tabs, FALSE);
  gtk_notebook_set_show_tabs(session->gtk.tabs,   FALSE);

#if (GTK_MAJOR_VERSION == 3)
  /* packing */
  gtk_box_set_spacing(session->gtk.box, 0);
  gtk_box_pack_start(session->gtk.box, GTK_WIDGET(session->gtk.tabbar),            FALSE, FALSE, 0);
  gtk_box_pack_start(session->gtk.box, GTK_WIDGET(session->gtk.view),              TRUE,  TRUE, 0);

  /* box */
  gtk_container_add(GTK_CONTAINER(session->private_data->gtk.overlay), GTK_WIDGET(session->gtk.box));
  /* overlay */
  g_object_set(session->private_data->gtk.bottom_box, "halign", GTK_ALIGN_FILL, NULL);
  g_object_set(session->private_data->gtk.bottom_box, "valign", GTK_ALIGN_END, NULL);

  gtk_overlay_add_overlay(GTK_OVERLAY(session->private_data->gtk.overlay), GTK_WIDGET(session->private_data->gtk.bottom_box));
  gtk_container_add(GTK_CONTAINER(session->gtk.window), GTK_WIDGET(session->private_data->gtk.overlay));

#else
  /* packing */
  gtk_box_set_spacing(session->gtk.box, 0);
  gtk_box_pack_start(session->gtk.box, GTK_WIDGET(session->gtk.tabbar),                 FALSE, FALSE, 0);
  gtk_box_pack_start(session->gtk.box, GTK_WIDGET(session->gtk.view),                   TRUE,  TRUE, 0);
  gtk_box_pack_end(session->gtk.box, GTK_WIDGET(session->private_data->gtk.bottom_box), FALSE, FALSE, 0);

  /* box */
  gtk_container_add(GTK_CONTAINER(session->gtk.window), GTK_WIDGET(session->gtk.box));
#endif

  /* parse color values */
  typedef struct color_setting_mapping_s
  {
    char* identifier;
    GdkRGBA *color;
  } color_setting_mapping_t;

  const color_setting_mapping_t color_setting_mappings[] = {
    {"default-fg",              &(session->style.default_foreground)},
    {"default-bg",              &(session->style.default_background)},
    {"inputbar-fg",             &(session->style.inputbar_foreground)},
    {"inputbar-bg",             &(session->style.inputbar_background)},
    {"statusbar-fg",            &(session->style.statusbar_foreground)},
    {"statusbar-bg",            &(session->style.statusbar_background)},
    {"completion-fg",           &(session->style.completion_foreground)},
    {"completion-bg",           &(session->style.completion_background)},
    {"completion-group-fg",     &(session->style.completion_group_foreground)},
    {"completion-group-bg",     &(session->style.completion_group_background)},
    {"completion-highlight-fg", &(session->style.completion_highlight_foreground)},
    {"completion-highlight-bg", &(session->style.completion_highlight_background)},
    {"notification-error-fg",   &(session->style.notification_error_foreground)},
    {"notification-error-bg",   &(session->style.notification_error_background)},
    {"notification-warning-fg", &(session->style.notification_warning_foreground)},
    {"notification-warning-bg", &(session->style.notification_warning_background)},
    {"notification-fg",         &(session->style.notification_default_foreground)},
    {"notification-bg",         &(session->style.notification_default_background)},
    {"tabbar-fg",               &(session->style.tabbar_foreground)},
    {"tabbar-bg",               &(session->style.tabbar_background)},
    {"tabbar-focus-fg",         &(session->style.tabbar_focus_foreground)},
    {"tabbar-focus-bg",         &(session->style.tabbar_focus_background)},
  };

  for (size_t i = 0; i < LENGTH(color_setting_mappings); i++) {
    char* tmp_value = NULL;
    girara_setting_get(session, color_setting_mappings[i].identifier, &tmp_value);
    if (tmp_value != NULL) {
      gdk_rgba_parse(color_setting_mappings[i].color, tmp_value);
      g_free(tmp_value);
    }
  }

  /* view */
  gtk_widget_override_background_color(GTK_WIDGET(session->gtk.viewport),
      GTK_STATE_NORMAL, &(session->style.default_background));

  /* statusbar */
  gtk_widget_override_background_color(GTK_WIDGET(session->gtk.statusbar),
      GTK_STATE_NORMAL, &(session->style.statusbar_background));

  /* inputbar */
#if (GTK_MAJOR_VERSION == 3)
  gtk_widget_override_background_color(GTK_WIDGET(session->gtk.inputbar_entry),
      GTK_STATE_NORMAL, &(session->style.inputbar_background));
  gtk_widget_override_color(GTK_WIDGET(session->gtk.inputbar_entry),
      GTK_STATE_NORMAL, &(session->style.inputbar_foreground));

  gtk_widget_override_background_color(GTK_WIDGET(session->gtk.inputbar),
      GTK_STATE_NORMAL, &(session->style.inputbar_background));
  gtk_widget_override_color(GTK_WIDGET(session->gtk.inputbar_dialog),
      GTK_STATE_NORMAL, &(session->style.inputbar_foreground));

  /* notification area */
  gtk_widget_override_background_color(GTK_WIDGET(session->gtk.notification_area),
      GTK_STATE_NORMAL, &(session->style.notification_default_background));
  gtk_widget_override_color(GTK_WIDGET(session->gtk.notification_text),
      GTK_STATE_NORMAL, &(session->style.notification_default_foreground));
#else
  /* inputbar */
  gtk_widget_modify_base(GTK_WIDGET(session->gtk.inputbar_entry), GTK_STATE_NORMAL, &(session->style.inputbar_background));
  gtk_widget_modify_text(GTK_WIDGET(session->gtk.inputbar_entry), GTK_STATE_NORMAL, &(session->style.inputbar_foreground));

  gtk_widget_modify_bg(GTK_WIDGET(session->gtk.inputbar),        GTK_STATE_NORMAL, &(session->style.inputbar_background));
  gtk_widget_modify_fg(GTK_WIDGET(session->gtk.inputbar_dialog), GTK_STATE_NORMAL, &(session->style.inputbar_foreground));

  /* notification area */
  gtk_widget_modify_bg(GTK_WIDGET(session->gtk.notification_area),
    GTK_STATE_NORMAL, &(session->style.notification_default_background));
  gtk_widget_modify_text(GTK_WIDGET(session->gtk.notification_text),
    GTK_STATE_NORMAL, &(session->style.notification_default_foreground));
#endif

  if (session->style.font == NULL) {
    /* set default font */
    girara_setting_set(session, "font", "monospace normal 9");
  } else {
    gtk_widget_override_font(GTK_WIDGET(session->gtk.inputbar_entry),    session->style.font);
    gtk_widget_override_font(GTK_WIDGET(session->gtk.inputbar_dialog),   session->style.font);
    gtk_widget_override_font(GTK_WIDGET(session->gtk.notification_text), session->style.font);
  }

  /* set window size */
  int window_width = 0;
  int window_height = 0;
  girara_setting_get(session, "window-width", &window_width);
  girara_setting_get(session, "window-height", &window_height);

  if (window_width > 0 && window_height > 0) {
    gtk_window_set_default_size(GTK_WINDOW(session->gtk.window), window_width, window_height);
  }

  gtk_widget_show_all(GTK_WIDGET(session->gtk.window));
  gtk_widget_hide(GTK_WIDGET(session->gtk.notification_area));
  gtk_widget_hide(GTK_WIDGET(session->gtk.inputbar_dialog));

  if (session->global.autohide_inputbar == true) {
    gtk_widget_hide(GTK_WIDGET(session->gtk.inputbar));
  }

  if (session->global.hide_statusbar == true) {
    gtk_widget_hide(GTK_WIDGET(session->gtk.statusbar));
  }

  char* window_icon = NULL;
  girara_setting_get(session, "window-icon", &window_icon);
  if (window_icon != NULL) {
    if (strlen(window_icon) != 0) {
      girara_setting_set(session, "window-icon", window_icon);
    }
    g_free(window_icon);
  }

  gtk_widget_grab_focus(GTK_WIDGET(session->gtk.view));

  return true;
}

static void
girara_session_private_free(girara_session_private_t* session)
{
  g_return_if_fail(session != NULL);

  /* clean up settings */
  girara_list_free(session->settings);
  session->settings = NULL;

  g_slice_free(girara_session_private_t, session);
}

bool
girara_session_destroy(girara_session_t* session)
{
  g_return_val_if_fail(session != NULL, FALSE);

  /* clean up style */
  if (session->style.font) {
    pango_font_description_free(session->style.font);
  }

  /* clean up shortcuts */
  girara_list_free(session->bindings.shortcuts);
  session->bindings.shortcuts = NULL;

  /* clean up inputbar shortcuts */
  girara_list_free(session->bindings.inputbar_shortcuts);
  session->bindings.inputbar_shortcuts = NULL;

  /* clean up commands */
  girara_list_free(session->bindings.commands);
  session->bindings.commands = NULL;

  /* clean up special commands */
  girara_list_free(session->bindings.special_commands);
  session->bindings.special_commands = NULL;

  /* clean up mouse events */
  girara_list_free(session->bindings.mouse_events);
  session->bindings.mouse_events = NULL;

  /* clean up input histry */
  g_object_unref(session->command_history);
  session->command_history = NULL;

  /* clean up statusbar items */
  girara_list_free(session->elements.statusbar_items);
  session->elements.statusbar_items = NULL;

  /* clean up config handles */
  girara_list_free(session->config.handles);
  session->config.handles = NULL;

  /* clean up shortcut mappings */
  girara_list_free(session->config.shortcut_mappings);
  session->config.shortcut_mappings = NULL;

  /* clean up argument mappings */
  girara_list_free(session->config.argument_mappings);
  session->config.argument_mappings = NULL;

  /* clean up modes */
  girara_list_free(session->modes.identifiers);
  session->modes.identifiers = NULL;

  /* clean up buffer */
  if (session->buffer.command) {
    g_string_free(session->buffer.command, TRUE);
  }

  if (session->global.buffer) {
    g_string_free(session->global.buffer,  TRUE);
  }

  session->buffer.command = NULL;
  session->global.buffer  = NULL;

  /* clean up private data */
  girara_session_private_free(session->private_data);
  session->private_data = NULL;
  IGNORE_DEPRECATED
  session->settings = NULL;
  UNIGNORE

  /* clean up session */
  g_slice_free(girara_session_t, session);

  return TRUE;
}

char*
girara_buffer_get(girara_session_t* session)
{
  g_return_val_if_fail(session != NULL, NULL);

  return (session->global.buffer) ? g_strdup(session->global.buffer->str) : NULL;
}

void
girara_notify(girara_session_t* session, int level, const char* format, ...)
{
  if (session == NULL
      || session->gtk.notification_text == NULL
      || session->gtk.notification_area == NULL
      || session->gtk.inputbar == NULL
      || session->gtk.view == NULL) {
    return;
  }

  switch (level) {
    case GIRARA_ERROR:
      gtk_widget_override_background_color(GTK_WIDGET(session->gtk.notification_area),
          GTK_STATE_NORMAL, &(session->style.notification_error_background));
      gtk_widget_override_color(GTK_WIDGET(session->gtk.notification_text),
          GTK_STATE_NORMAL, &(session->style.notification_error_foreground));
      break;
    case GIRARA_WARNING:
      gtk_widget_override_background_color(GTK_WIDGET(session->gtk.notification_area),
          GTK_STATE_NORMAL, &(session->style.notification_warning_background));
      gtk_widget_override_color(GTK_WIDGET(session->gtk.notification_text),
          GTK_STATE_NORMAL, &(session->style.notification_warning_foreground));
      break;
    case GIRARA_INFO:
      gtk_widget_override_background_color(GTK_WIDGET(session->gtk.notification_area),
          GTK_STATE_NORMAL, &(session->style.notification_default_background));
      gtk_widget_override_color(GTK_WIDGET(session->gtk.notification_text),
          GTK_STATE_NORMAL, &(session->style.notification_default_foreground));
      break;
    default:
      return;
  }

  /* prepare message */
  va_list ap;
  va_start(ap, format);
  char* message = g_strdup_vprintf(format, ap);
  va_end(ap);

  gtk_label_set_markup(GTK_LABEL(session->gtk.notification_text), message);
  g_free(message);

  /* update visibility */
  gtk_widget_show(GTK_WIDGET(session->gtk.notification_area));
  gtk_widget_hide(GTK_WIDGET(session->gtk.inputbar));

  gtk_widget_grab_focus(GTK_WIDGET(session->gtk.view));
}

void girara_dialog(girara_session_t* session, const char* dialog, bool
    invisible, girara_callback_inputbar_key_press_event_t key_press_event,
    girara_callback_inputbar_activate_t activate_event, void* data)
{
  if (session == NULL || session->gtk.inputbar == NULL
      || session->gtk.inputbar_dialog == NULL
      || session->gtk.inputbar_entry == NULL) {
    return;
  }

  gtk_widget_show(GTK_WIDGET(session->gtk.inputbar_dialog));

  /* set dialog message */
  if (dialog != NULL) {
    gtk_label_set_markup(session->gtk.inputbar_dialog, dialog);
  }

  /* set input visibility */
  if (invisible == true) {
    gtk_entry_set_visibility(session->gtk.inputbar_entry, FALSE);
  } else {
    gtk_entry_set_visibility(session->gtk.inputbar_entry, TRUE);
  }

  /* set handler */
  session->signals.inputbar_custom_activate        = activate_event;
  session->signals.inputbar_custom_key_press_event = key_press_event;
  session->signals.inputbar_custom_data            = data;

  /* focus inputbar */
  girara_sc_focus_inputbar(session, NULL, NULL, 0);
}

bool
girara_set_view(girara_session_t* session, GtkWidget* widget)
{
  g_return_val_if_fail(session != NULL, false);

  GtkWidget* child = gtk_bin_get_child(GTK_BIN(session->gtk.viewport));

  if (child) {
    g_object_ref(child);
    gtk_container_remove(GTK_CONTAINER(session->gtk.viewport), child);
  }

  gtk_container_add(GTK_CONTAINER(session->gtk.viewport), widget);
  gtk_widget_show_all(widget);

  return true;
}

void
girara_mode_set(girara_session_t* session, girara_mode_t mode)
{
  g_return_if_fail(session != NULL);

  session->modes.current_mode = mode;
}

girara_mode_t
girara_mode_add(girara_session_t* session, const char* name)
{
  g_return_val_if_fail(session  != NULL, FALSE);
  g_return_val_if_fail(name != NULL && name[0] != 0x0, FALSE);

  girara_mode_t last_index = 0;
  GIRARA_LIST_FOREACH(session->modes.identifiers, girara_mode_string_t*, iter, mode)
    if (mode->index > last_index) {
      last_index = mode->index;
    }
  GIRARA_LIST_FOREACH_END(session->modes.identifiers, girara_mode_string_t*, iter, mode);

  /* create new mode identifier */
  girara_mode_string_t* mode = g_slice_new(girara_mode_string_t);
  mode->index = last_index + 1;
  mode->name = g_strdup(name);
  girara_list_append(session->modes.identifiers, mode);

  return mode->index;
}

void
girara_mode_string_free(girara_mode_string_t* mode)
{
  if (mode == NULL) {
    return;
  }

  g_free(mode->name);
  g_slice_free(girara_mode_string_t, mode);
}

girara_mode_t
girara_mode_get(girara_session_t* session)
{
  g_return_val_if_fail(session != NULL, 0);

  return session->modes.current_mode;
}

bool
girara_set_window_title(girara_session_t* session, const char* name)
{
  if (session == NULL || session->gtk.window == NULL || name == NULL) {
    return false;
  }

  gtk_window_set_title(GTK_WINDOW(session->gtk.window), name);

  return true;
}

girara_list_t*
girara_get_command_history(girara_session_t* session)
{
  g_return_val_if_fail(session != NULL, NULL);
  return girara_input_history_list(session->command_history);
}
