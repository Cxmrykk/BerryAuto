#include "dbus_portal.hpp"
#include "globals.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <gio/gio.h>
#include <gio/gunixfdlist.h> // Required to extract the file descriptor
#include <iostream>
#include <string>

static uint32_t negotiated_node_id = 0;
static GDBusConnection* portal_conn = nullptr;

struct PortalStepState
{
    GMainLoop* loop;
    bool completed;
    uint32_t response_code;
};

static std::string get_token_storage_path()
{
    const char* home_env = getenv("HOME");
    if (home_env)
        return std::string(home_env) + "/.config/berryauto_portal_token.txt";
    return "/tmp/berryauto_portal_token.txt";
}

static std::string get_portal_token()
{
    std::string path = get_token_storage_path();
    std::ifstream f(path);
    if (f.is_open())
    {
        std::string t;
        std::getline(f, t);
        return t;
    }
    return "";
}

static void save_portal_token(const char* t)
{
    std::string path = get_token_storage_path();
    std::ofstream f(path, std::ios::trunc);
    if (f.is_open())
    {
        f << t;
        LOG_I("[Portal] Token saved to persistent storage: " << path);
    }
}

static gboolean on_portal_timeout(gpointer user_data)
{
    PortalStepState* state = static_cast<PortalStepState*>(user_data);
    LOG_E("[Portal] CRITICAL: D-Bus request timed out!");
    LOG_E("[Portal] GNOME likely rejected the token and is waiting for a mouse click on the Pi!");
    state->response_code = 999;
    state->completed = true;
    if (g_main_loop_is_running(state->loop))
        g_main_loop_quit(state->loop);
    return G_SOURCE_REMOVE;
}

static void on_signal_response(GDBusConnection* conn, const gchar* sender, const gchar* path, const gchar* iface,
                               const gchar* signal, GVariant* params, gpointer user_data)
{
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;
    (void)signal;

    PortalStepState* state = static_cast<PortalStepState*>(user_data);
    uint32_t response = 0;
    GVariant* results = nullptr;

    g_variant_get(params, "(u@a{sv})", &response, &results);
    state->response_code = response;
    state->completed = true;

    LOG_I("[Portal] Portal Signal Received! Response Code: " << response);

    if (response != 0)
    {
        LOG_E("[Portal] Request failed or cancelled by Desktop Environment.");
    }
    else
    {
        GVariant* token_var = g_variant_lookup_value(results, "restore_token", G_VARIANT_TYPE_STRING);
        if (token_var)
        {
            const char* t_str = g_variant_get_string(token_var, nullptr);
            LOG_I("[Portal] Received valid Restore Token from GNOME: " << t_str);
            save_portal_token(t_str);
            g_variant_unref(token_var);
        }

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
                LOG_I("[Portal] Successfully negotiated PipeWire Node ID: " << negotiated_node_id);
                g_variant_unref(stream_props);
            }
            g_variant_unref(streams);
        }
    }

    if (results)
        g_variant_unref(results);

    if (g_main_loop_is_running(state->loop))
        g_main_loop_quit(state->loop);
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

    std::string sender = g_dbus_connection_get_unique_name(portal_conn);
    sender.erase(std::remove(sender.begin(), sender.end(), ':'), sender.end());
    std::replace(sender.begin(), sender.end(), '.', '_');

    std::string session_path = "/org/freedesktop/portal/desktop/session/" + sender + "/berryauto";
    std::string request_path = "/org/freedesktop/portal/desktop/request/" + sender;

    auto run_portal_step = [&](const std::string& method, const std::string& req_token, GVariant* args,
                               int timeout_sec) -> bool
    {
        GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
        PortalStepState state = {loop, false, 0};

        guint sub = g_dbus_connection_signal_subscribe(portal_conn, "org.freedesktop.portal.Desktop",
                                                       "org.freedesktop.portal.Request", "Response",
                                                       (request_path + "/" + req_token).c_str(), nullptr,
                                                       G_DBUS_SIGNAL_FLAGS_NONE, on_signal_response, &state, nullptr);

        LOG_I("[Portal] Calling " << method << " (Token: " << req_token << ")...");

        GError* error = nullptr;
        GVariant* res =
            g_dbus_connection_call_sync(portal_conn, "org.freedesktop.portal.Desktop",
                                        "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast",
                                        method.c_str(), args, nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
        if (error)
        {
            LOG_E("[Portal] " << method << " failed synchronously: " << error->message);
            g_error_free(error);
            g_dbus_connection_signal_unsubscribe(portal_conn, sub);
            g_main_loop_unref(loop);
            return false;
        }
        g_variant_unref(res);

        guint timeout_id = 0;
        if (!state.completed)
        {
            if (timeout_sec > 0)
                timeout_id = g_timeout_add_seconds(timeout_sec, on_portal_timeout, &state);

            g_main_loop_run(loop);

            if (timeout_id > 0)
                g_source_remove(timeout_id);
        }

        g_dbus_connection_signal_unsubscribe(portal_conn, sub);
        g_main_loop_unref(loop);

        return state.response_code == 0;
    };

    // STEP 1: CreateSession
    GVariantBuilder b1;
    g_variant_builder_init(&b1, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b1, "{sv}", "session_handle_token", g_variant_new_string("berryauto"));
    g_variant_builder_add(&b1, "{sv}", "handle_token", g_variant_new_string("req1"));

    if (!run_portal_step("CreateSession", "req1", g_variant_new("(a{sv})", &b1), 10))
        return false;

    // STEP 2: SelectSources
    GVariantBuilder b2;
    g_variant_builder_init(&b2, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b2, "{sv}", "multiple", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&b2, "{sv}", "types", g_variant_new_uint32(1));
    g_variant_builder_add(&b2, "{sv}", "handle_token", g_variant_new_string("req2"));
    g_variant_builder_add(&b2, "{sv}", "persist_mode", g_variant_new_uint32(2));

    std::string token = get_portal_token();
    if (!token.empty())
    {
        LOG_I("[Portal] Found saved token. Bypassing permission prompt...");
        g_variant_builder_add(&b2, "{sv}", "restore_token", g_variant_new_string(token.c_str()));
    }

    // High timeout for Step 2 incase the prompt actually pops up and requires interaction
    if (!run_portal_step("SelectSources", "req2", g_variant_new("(oa{sv})", session_path.c_str(), &b2), 60))
        return false;

    // STEP 3: Start
    GVariantBuilder b3;
    g_variant_builder_init(&b3, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b3, "{sv}", "handle_token", g_variant_new_string("req3"));

    if (!run_portal_step("Start", "req3", g_variant_new("(osa{sv})", session_path.c_str(), "", &b3), 15))
        return false;

    // STEP 4: Extract Node & Auth FD
    if (negotiated_node_id > 0)
    {
        LOG_I("[Portal] Extracting authenticated PipeWire FD...");
        GVariantBuilder b_fd;
        g_variant_builder_init(&b_fd, G_VARIANT_TYPE_VARDICT);

        GUnixFDList* fd_list = nullptr;
        GError* fd_error = nullptr;
        GVariant* res_fd = g_dbus_connection_call_with_unix_fd_list_sync(
            portal_conn, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.ScreenCast", "OpenPipeWireRemote",
            g_variant_new("(oa{sv})", session_path.c_str(), &b_fd), G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, -1,
            nullptr, &fd_list, nullptr, &fd_error);

        if (fd_error)
        {
            LOG_E("[Portal] OpenPipeWireRemote failed: " << fd_error->message);
            g_error_free(fd_error);
            return false;
        }

        if (res_fd && fd_list)
        {
            int32_t handle = -1;
            g_variant_get(res_fd, "(h)", &handle);
            out_fd = g_unix_fd_list_get(fd_list, handle, nullptr);
            g_object_unref(fd_list);
            g_variant_unref(res_fd);

            out_node_id = negotiated_node_id;
            return true;
        }
    }

    return false;
}
