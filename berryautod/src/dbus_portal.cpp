#include "dbus_portal.hpp"
#include "globals.hpp"
#include <algorithm>
#include <gio/gio.h>
#include <iostream>
#include <string>

static uint32_t negotiated_node_id = 0;
static GMainLoop* dbus_loop = nullptr;

static void on_signal_response(GDBusConnection* conn, const gchar* sender, const gchar* path, const gchar* iface,
                               const gchar* signal, GVariant* params, gpointer user_data)
{
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;
    (void)signal;
    int step = GPOINTER_TO_INT(user_data);
    uint32_t response = 0;
    GVariant* results = nullptr;

    g_variant_get(params, "(u@a{sv})", &response, &results);

    if (response != 0)
    {
        LOG_E("[Portal] Request failed or cancelled (response=" << response
                                                                << "). Ensure chooser_type=none is set in config!");
        if (results)
            g_variant_unref(results);
        g_main_loop_quit(dbus_loop);
        return;
    }

    if (step == 3)
    {
        GVariant* streams = g_variant_lookup_value(results, "streams", G_VARIANT_TYPE("a(ua{sv})"));
        if (streams)
        {
            GVariantIter iter;
            g_variant_iter_init(&iter, streams);
            uint32_t node_id;
            GVariant* stream_props;
            if (g_variant_iter_next(&iter, "(u@a{sv})", &node_id, &stream_props))
            {
                negotiated_node_id = node_id;
                g_variant_unref(stream_props);
            }
            g_variant_unref(streams);
        }
    }

    if (results)
        g_variant_unref(results);
    g_main_loop_quit(dbus_loop);
}

bool negotiate_wayland_screencast(uint32_t& out_node_id)
{
    GError* error = nullptr;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!conn)
    {
        LOG_E("[Portal] Failed to connect to D-Bus Session: " << error->message);
        g_error_free(error);
        return false;
    }

    std::string sender = g_dbus_connection_get_unique_name(conn);
    sender.erase(std::remove(sender.begin(), sender.end(), ':'), sender.end());
    std::replace(sender.begin(), sender.end(), '.', '_');

    std::string session_path = "/org/freedesktop/portal/desktop/session/" + sender + "/berryauto";
    std::string request_path = "/org/freedesktop/portal/desktop/request/" + sender;

    dbus_loop = g_main_loop_new(nullptr, FALSE);

    // STEP 1: CreateSession
    guint sub1 =
        g_dbus_connection_signal_subscribe(conn, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
                                           "Response", (request_path + "/req1").c_str(), nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE, on_signal_response, GINT_TO_POINTER(1), nullptr);

    GVariantBuilder b1;
    g_variant_builder_init(&b1, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b1, "{sv}", "session_handle_token", g_variant_new_string("berryauto"));
    g_variant_builder_add(&b1, "{sv}", "handle_token", g_variant_new_string("req1"));

    GVariant* res1 = g_dbus_connection_call_sync(
        conn, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast",
        "CreateSession", g_variant_new("(a{sv})", &b1), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (error)
    {
        LOG_E("[Portal] CreateSession failed: " << error->message);
        return false;
    }
    g_variant_unref(res1);
    g_main_loop_run(dbus_loop);
    g_dbus_connection_signal_unsubscribe(conn, sub1);

    // STEP 2: SelectSources
    guint sub2 =
        g_dbus_connection_signal_subscribe(conn, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
                                           "Response", (request_path + "/req2").c_str(), nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE, on_signal_response, GINT_TO_POINTER(2), nullptr);

    GVariantBuilder b2;
    g_variant_builder_init(&b2, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b2, "{sv}", "multiple", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&b2, "{sv}", "types", g_variant_new_uint32(1)); // 1 = monitor
    g_variant_builder_add(&b2, "{sv}", "handle_token", g_variant_new_string("req2"));

    GVariant* res2 = g_dbus_connection_call_sync(conn, "org.freedesktop.portal.Desktop",
                                                 "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast",
                                                 "SelectSources", g_variant_new("(oa{sv})", session_path.c_str(), &b2),
                                                 nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (error)
    {
        LOG_E("[Portal] SelectSources failed: " << error->message);
        return false;
    }
    g_variant_unref(res2);
    g_main_loop_run(dbus_loop);
    g_dbus_connection_signal_unsubscribe(conn, sub2);

    // STEP 3: Start
    guint sub3 =
        g_dbus_connection_signal_subscribe(conn, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
                                           "Response", (request_path + "/req3").c_str(), nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE, on_signal_response, GINT_TO_POINTER(3), nullptr);

    GVariantBuilder b3;
    g_variant_builder_init(&b3, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b3, "{sv}", "handle_token", g_variant_new_string("req3"));

    GVariant* res3 = g_dbus_connection_call_sync(conn, "org.freedesktop.portal.Desktop",
                                                 "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast",
                                                 "Start", g_variant_new("(osa{sv})", session_path.c_str(), "", &b3),
                                                 nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (error)
    {
        LOG_E("[Portal] Start failed: " << error->message);
        return false;
    }
    g_variant_unref(res3);
    g_main_loop_run(dbus_loop);
    g_dbus_connection_signal_unsubscribe(conn, sub3);

    g_main_loop_unref(dbus_loop);
    g_object_unref(conn);

    if (negotiated_node_id > 0)
    {
        out_node_id = negotiated_node_id;
        return true;
    }
    return false;
}
