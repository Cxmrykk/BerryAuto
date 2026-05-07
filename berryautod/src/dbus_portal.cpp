#include "dbus_portal.hpp"
#include "globals.hpp"
#include <algorithm>
#include <cstdlib>
#include <gio/gio.h>
#include <gio/gunixfdlist.h> // Required to extract the file descriptor
#include <iostream>
#include <string>

static GDBusConnection* portal_conn = nullptr;

// Helper to query the active monitor connector string (e.g., "HDMI-A-1")
static std::string get_mutter_connector()
{
    std::string connector = "";
    // Check local bin first, fallback to system PATH
    FILE* fp = popen(
        "/usr/local/bin/gnome-randr query --listactivemonitors | grep -v 'Monitors:' | head -n 1 | awk '{print $NF}'",
        "r");
    if (!fp)
        fp = popen("gnome-randr query --listactivemonitors | grep -v 'Monitors:' | head -n 1 | awk '{print $NF}'", "r");

    if (fp)
    {
        char buf[128];
        if (fgets(buf, sizeof(buf), fp))
        {
            connector = buf;
            // Trim trailing newline
            connector.erase(std::remove(connector.begin(), connector.end(), '\n'), connector.end());
        }
        pclose(fp);
    }
    return connector;
}

bool negotiate_wayland_screencast(uint32_t& out_node_id, int& out_fd)
{
    if (!portal_conn)
    {
        GError* error = nullptr;
        portal_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!portal_conn)
        {
            LOG_E("[Portal] Failed to connect to D-Bus Session: " << error->message);
            g_error_free(error);
            return false;
        }
    }

    LOG_I("[Portal] Bypassing XDG Portal! Interfacing directly with GNOME Mutter's Private API...");
    GError* error = nullptr;

    // STEP 1: CreateSession
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&props, "{sv}", "cursor-mode", g_variant_new_uint32(0)); // 0 = Hidden cursor

    GVariant* res1 =
        g_dbus_connection_call_sync(portal_conn, "org.gnome.Mutter.ScreenCast", "/org/gnome/Mutter/ScreenCast",
                                    "org.gnome.Mutter.ScreenCast", "CreateSession", g_variant_new("(a{sv})", &props),
                                    G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    if (error)
    {
        LOG_E("[Portal] Mutter CreateSession failed: " << error->message);
        g_error_free(error);
        return false;
    }

    const gchar* session_path;
    g_variant_get(res1, "(&o)", &session_path);
    std::string session_path_str = session_path;
    g_variant_unref(res1);
    LOG_I("[Portal] Mutter Session created: " << session_path_str);

    // STEP 2: RecordMonitor
    std::string connector = get_mutter_connector();
    if (connector.empty())
    {
        LOG_E("[Portal] Failed to detect active monitor. Defaulting to HDMI-1.");
        connector = "HDMI-1";
    }
    LOG_I("[Portal] Requesting to record physical monitor: " << connector);

    g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
    GVariant* res2 = g_dbus_connection_call_sync(portal_conn, "org.gnome.Mutter.ScreenCast", session_path_str.c_str(),
                                                 "org.gnome.Mutter.ScreenCast.Session", "RecordMonitor",
                                                 g_variant_new("(sa{sv})", connector.c_str(), &props),
                                                 G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    if (error)
    {
        LOG_E("[Portal] Mutter RecordMonitor failed: " << error->message);
        g_error_free(error);
        return false;
    }

    const gchar* stream_path;
    g_variant_get(res2, "(&o)", &stream_path);
    std::string stream_path_str = stream_path;
    g_variant_unref(res2);
    LOG_I("[Portal] Mutter Stream created: " << stream_path_str);

    // STEP 3: Start Stream
    LOG_I("[Portal] Starting Mutter Stream (No UI prompt!)...");
    GVariant* res3 = g_dbus_connection_call_sync(portal_conn, "org.gnome.Mutter.ScreenCast", session_path_str.c_str(),
                                                 "org.gnome.Mutter.ScreenCast.Session", "Start", nullptr, nullptr,
                                                 G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    if (error)
    {
        LOG_E("[Portal] Mutter Start failed: " << error->message);
        g_error_free(error);
        return false;
    }
    if (res3)
        g_variant_unref(res3);

    // STEP 4: Extract Node ID & File Descriptor securely
    LOG_I("[Portal] Extracting isolated PipeWire Node ID and Auth FD...");

    GUnixFDList* fd_list = nullptr;
    GVariant* fd_res = g_dbus_connection_call_with_unix_fd_list_sync(
        portal_conn, "org.gnome.Mutter.ScreenCast", stream_path_str.c_str(), "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.gnome.Mutter.ScreenCast.Stream", "PipeWireStreamFd"), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &fd_list, nullptr, &error);

    if (error)
    {
        LOG_E("[Portal] Failed to get PipeWireStreamFd: " << error->message);
        g_error_free(error);
        return false;
    }

    if (fd_res && fd_list)
    {
        GVariant* inner = nullptr;
        g_variant_get(fd_res, "(v)", &inner);
        int32_t handle = -1;
        g_variant_get(inner, "h", &handle);
        out_fd = g_unix_fd_list_get(fd_list, handle, nullptr);
        g_variant_unref(inner);
        g_object_unref(fd_list);
        g_variant_unref(fd_res);
    }

    GVariant* node_res = g_dbus_connection_call_sync(
        portal_conn, "org.gnome.Mutter.ScreenCast", stream_path_str.c_str(), "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.gnome.Mutter.ScreenCast.Stream", "PipeWireNodeId"), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    if (error)
    {
        LOG_E("[Portal] Failed to get PipeWireNodeId: " << error->message);
        g_error_free(error);
        return false;
    }

    if (node_res)
    {
        GVariant* inner = nullptr;
        g_variant_get(node_res, "(v)", &inner);
        g_variant_get(inner, "u", &out_node_id);
        g_variant_unref(inner);
        g_variant_unref(node_res);
    }

    LOG_I("[Portal] Native GNOME negotiation complete! Node ID: " << out_node_id << ", FD: " << out_fd);
    return true;
}
