#include "include/dynamic_color/dynamic_color_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DYNAMIC_COLOR_PLUGIN(obj)                                     \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), dynamic_color_plugin_get_type(), \
                              DynamicColorPlugin))

#define FALLBACK_COLOR 0xFF673AB7

struct _DynamicColorPlugin {
  GObject parent_instance;
  FlPluginRegistrar* registrar;
};

G_DEFINE_TYPE(DynamicColorPlugin, dynamic_color_plugin, g_object_get_type())

static int rgba_to_argb(const GdkRGBA* color) {
  int a = lround(color->alpha * 255);
  int r = lround(color->red * 255);
  int g = lround(color->green * 255);
  int b = lround(color->blue * 255);
  return (a << 24) | (r << 16) | (g << 8) | b;
}

static int get_accent_color_gtk_fallback(GtkWidget* widget) {
  if (!widget) {
      g_warning("GTK Fallback: Invalid widget provided.");
      return FALLBACK_COLOR;
  }
  GdkRGBA color;
  GtkStyleContext* context = gtk_widget_get_style_context(widget);
  if (!gtk_style_context_lookup_color(context, "accent_color", &color)) {
      if (!gtk_style_context_lookup_color(context, "theme_selected_bg_color", &color)) {
          g_warning("GTK Fallback: Failed to find 'accent_color' or 'theme_selected_bg_color'. Using default color.");
          return FALLBACK_COLOR;
      }
  }
  return rgba_to_argb(&color);
}

static int get_accent_color(GtkWidget* widget) {
  GSettings* settings = nullptr;
  gchar* accent_color_str = nullptr;
  int result_color = -1;

  const char* schema_id = "org.gnome.desktop.interface";
  const char* key = "accent-color";

  GSettingsSchemaSource* schema_source = g_settings_schema_source_get_default();
  if (schema_source && g_settings_schema_source_lookup(schema_source, schema_id, TRUE)) {
      settings = g_settings_new(schema_id);
      if (settings) {
          if (g_settings_schema_source_lookup(schema_source, schema_id, TRUE)) {
              accent_color_str = g_settings_get_string(settings, key);

              if (accent_color_str != nullptr && strlen(accent_color_str) > 0) {
                  GdkRGBA parsed_color;
                  if (gdk_rgba_parse(&parsed_color, accent_color_str)) {
                      result_color = rgba_to_argb(&parsed_color);
                      g_debug("Color obtained from GSettings (%s): %s -> %#08x", key, accent_color_str, result_color);
                  } else {
                      g_warning("Failed to parse GSettings value '%s' for key '%s' as a color.", accent_color_str, key);
                  }
              } else {
                  g_debug("GSettings key '%s' is empty or null. Using fallback.", key);
              }

              if (accent_color_str) g_free(accent_color_str);
          } else {
              g_debug("GSettings key '%s' does not exist in schema '%s'. Using fallback.", key, schema_id);
          }

          g_object_unref(settings);
      } else {
          g_warning("Failed to create GSettings object for schema '%s'. Using fallback.", schema_id);
      }
  } else {
      g_debug("GSettings schema '%s' not found. Using fallback.", schema_id);
  }

  if (result_color == -1) {
      g_debug("Using GTK fallback to get the accent color.");
      result_color = get_accent_color_gtk_fallback(widget);
  }

  if (result_color == -1) {
      g_warning("Failed in both GSettings and GTK fallback. Returning global default color.");
      return FALLBACK_COLOR;
  }

  return result_color;
}

static void dynamic_color_plugin_handle_method_call(DynamicColorPlugin* self,
                                                    FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;

  const gchar* method = fl_method_call_get_name(method_call);
  if (strcmp(method, "getAccentColor") == 0) {
    FlView* view = fl_plugin_registrar_get_view(self->registrar);
    if (!view) {
        g_warning("Failed to get FlView to retrieve the GTK widget.");
        g_autoptr(FlValue) result = fl_value_new_int(FALLBACK_COLOR);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(result));
    } else {
        int argb = get_accent_color(GTK_WIDGET(view));
        g_autoptr(FlValue) result = fl_value_new_int(argb);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(result));
    }
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  fl_method_call_respond(method_call, response, nullptr);
}

static void dynamic_color_plugin_dispose(GObject* object) {
    DynamicColorPlugin* self = DYNAMIC_COLOR_PLUGIN(object);
    if (self->registrar) {
        g_object_unref(self->registrar);
        self->registrar = nullptr;
    }
    G_OBJECT_CLASS(dynamic_color_plugin_parent_class)->dispose(object);
}


static void dynamic_color_plugin_class_init(DynamicColorPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = dynamic_color_plugin_dispose;
}

static void dynamic_color_plugin_init(DynamicColorPlugin* self) {}

static void method_call_cb(FlMethodChannel* channel, FlMethodCall* method_call,
                           gpointer user_data) {
  DynamicColorPlugin* plugin = DYNAMIC_COLOR_PLUGIN(user_data);
  dynamic_color_plugin_handle_method_call(plugin, method_call);
}

void dynamic_color_plugin_register_with_registrar(
    FlPluginRegistrar* registrar) {
  DynamicColorPlugin* plugin = DYNAMIC_COLOR_PLUGIN(
      g_object_new(dynamic_color_plugin_get_type(), nullptr));
  plugin->registrar = FL_PLUGIN_REGISTRAR(g_object_ref(registrar));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  g_autoptr(FlMethodChannel) channel = fl_method_channel_new(
      fl_plugin_registrar_get_messenger(registrar),
      "io.material.plugins/dynamic_color", FL_METHOD_CODEC(codec));

  fl_method_channel_set_method_call_handler(channel, method_call_cb,
                                          g_object_ref(plugin),
                                          g_object_unref);

  g_object_unref(plugin);
}
