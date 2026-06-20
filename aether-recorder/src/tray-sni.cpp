#include "tray-sni.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <gio/gio.h>

static const char *tray_introspection_xml = R"(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <method name="ContextMenu">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
    </method>
    <method name="Activate">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
    </method>
    <method name="Scroll">
      <arg type="i" name="delta" direction="in"/>
      <arg type="s" name="orientation" direction="in"/>
    </method>
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="i" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="IconThemePath" type="s" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <signal name="NewStatus">
      <arg type="s" name="status"/>
    </signal>
    <signal name="NewIcon"/>
    <signal name="NewTitle"/>
  </interface>
</node>
)";

struct RecordingTray::Impl
{
    explicit Impl(std::function<void()> stop_callback)
        : stop_callback(std::move(stop_callback))
    {
        worker = std::thread([this] {
            run();
        });

        std::unique_lock<std::mutex> lock(mutex);
        ready_cv.wait(lock, [this] {
            return ready;
        });
    }

    ~Impl()
    {
        if (context)
        {
            g_main_context_invoke(context, [](gpointer data) -> gboolean {
                auto *self = static_cast<Impl*>(data);
                if (self->loop)
                {
                    g_main_loop_quit(self->loop);
                }

                return G_SOURCE_REMOVE;
            }, this);
        }

        if (worker.joinable())
        {
            worker.join();
        }
    }

    void set_recording(bool value)
    {
        recording = value;

        if (!context)
        {
            return;
        }

        g_main_context_invoke(context, [](gpointer data) -> gboolean {
            auto *self = static_cast<Impl*>(data);
            if (self->recording)
            {
                self->register_with_watcher();
            }

            self->emit_status();
            return G_SOURCE_REMOVE;
        }, this);
    }

    void run()
    {
        context = g_main_context_new();
        g_main_context_push_thread_default(context);

        GError *error = nullptr;
        connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!connection)
        {
            clear_error(error);
            mark_ready();
            cleanup_context();
            return;
        }

        node_info = g_dbus_node_info_new_for_xml(tray_introspection_xml, &error);
        if (!node_info)
        {
            clear_error(error);
            mark_ready();
            cleanup_context();
            return;
        }

        registration_id = g_dbus_connection_register_object(
            connection,
            "/StatusNotifierItem",
            node_info->interfaces[0],
            &vtable,
            this,
            nullptr,
            &error);

        if (registration_id == 0)
        {
            clear_error(error);
            mark_ready();
            cleanup_context();
            return;
        }

        watcher_id = g_bus_watch_name_on_connection(
            connection,
            "org.kde.StatusNotifierWatcher",
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            on_watcher_appeared,
            on_watcher_vanished,
            this,
            nullptr);

        loop = g_main_loop_new(context, FALSE);
        mark_ready();
        g_main_loop_run(loop);

        if (watcher_id)
        {
            g_bus_unwatch_name(watcher_id);
            watcher_id = 0;
        }

        if (registration_id)
        {
            g_dbus_connection_unregister_object(connection, registration_id);
            registration_id = 0;
        }

        cleanup_context();
    }

    void register_with_watcher()
    {
        if (!connection || registered)
        {
            return;
        }

        GError *error = nullptr;
        GVariant *reply = g_dbus_connection_call_sync(
            connection,
            "org.kde.StatusNotifierWatcher",
            "/StatusNotifierWatcher",
            "org.kde.StatusNotifierWatcher",
            "RegisterStatusNotifierItem",
            g_variant_new("(s)", "/StatusNotifierItem"),
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            nullptr,
            &error);

        if (reply)
        {
            g_variant_unref(reply);
            registered = true;
            emit_status();
        } else
        {
            clear_error(error);
        }
    }

    void emit_status()
    {
        if (!connection)
        {
            return;
        }

        const char *status = recording ? "Active" : "Passive";
        g_dbus_connection_emit_signal(
            connection,
            nullptr,
            "/StatusNotifierItem",
            "org.kde.StatusNotifierItem",
            "NewStatus",
            g_variant_new("(s)", status),
            nullptr);

        GVariantBuilder changed;
        g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&changed, "{sv}", "Status", g_variant_new_string(status));

        GVariantBuilder invalidated;
        g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));

        g_dbus_connection_emit_signal(
            connection,
            nullptr,
            "/StatusNotifierItem",
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            g_variant_new("(sa{sv}as)", "org.kde.StatusNotifierItem", &changed, &invalidated),
            nullptr);
    }

    std::string status() const
    {
        return recording ? "Active" : "Passive";
    }

    void activate()
    {
        if (recording && stop_callback)
        {
            stop_callback();
        }
    }

    void mark_ready()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ready = true;
        ready_cv.notify_all();
    }

    void cleanup_context()
    {
        if (loop)
        {
            g_main_loop_unref(loop);
            loop = nullptr;
        }

        if (node_info)
        {
            g_dbus_node_info_unref(node_info);
            node_info = nullptr;
        }

        g_clear_object(&connection);

        if (context)
        {
            g_main_context_pop_thread_default(context);
            g_main_context_unref(context);
            context = nullptr;
        }
    }

    static void clear_error(GError *error)
    {
        if (error)
        {
            g_error_free(error);
        }
    }

    static void on_watcher_appeared(GDBusConnection*, const gchar*, const gchar*, gpointer data)
    {
        auto *self = static_cast<Impl*>(data);
        if (self->recording)
        {
            self->register_with_watcher();
        }
    }

    static void on_watcher_vanished(GDBusConnection*, const gchar*, gpointer data)
    {
        auto *self = static_cast<Impl*>(data);
        self->registered = false;
    }

    static void method_call(
        GDBusConnection*,
        const gchar*,
        const gchar*,
        const gchar*,
        const gchar *method_name,
        GVariant*,
        GDBusMethodInvocation *invocation,
        gpointer data)
    {
        auto *self = static_cast<Impl*>(data);

        if (g_strcmp0(method_name, "Activate") == 0 ||
            g_strcmp0(method_name, "SecondaryActivate") == 0 ||
            g_strcmp0(method_name, "ContextMenu") == 0)
        {
            self->activate();
        }

        g_dbus_method_invocation_return_value(invocation, nullptr);
    }

    static GVariant* get_property(
        GDBusConnection*,
        const gchar*,
        const gchar*,
        const gchar*,
        const gchar *property_name,
        GError**,
        gpointer data)
    {
        auto *self = static_cast<Impl*>(data);

        if (g_strcmp0(property_name, "Category") == 0)
        {
            return g_variant_new_string("ApplicationStatus");
        }

        if (g_strcmp0(property_name, "Id") == 0)
        {
            return g_variant_new_string("aether-recorder-recording");
        }

        if (g_strcmp0(property_name, "Title") == 0)
        {
            return g_variant_new_string(self->recording ? "aether-recorder is recording" : "aether-recorder");
        }

        if (g_strcmp0(property_name, "Status") == 0)
        {
            return g_variant_new_string(self->status().c_str());
        }

        if (g_strcmp0(property_name, "WindowId") == 0)
        {
            return g_variant_new_int32(0);
        }

        if (g_strcmp0(property_name, "IconName") == 0 ||
            g_strcmp0(property_name, "AttentionIconName") == 0)
        {
            return g_variant_new_string("media-record");
        }

        if (g_strcmp0(property_name, "IconThemePath") == 0)
        {
            return g_variant_new_string("");
        }

        if (g_strcmp0(property_name, "ItemIsMenu") == 0)
        {
            return g_variant_new_boolean(FALSE);
        }

        return nullptr;
    }

    static const GDBusInterfaceVTable vtable;

    std::function<void()> stop_callback;
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable ready_cv;
    bool ready = false;
    std::atomic<bool> recording{false};
    bool registered = false;

    GMainContext *context = nullptr;
    GMainLoop *loop = nullptr;
    GDBusConnection *connection = nullptr;
    GDBusNodeInfo *node_info = nullptr;
    guint registration_id = 0;
    guint watcher_id = 0;
};

const GDBusInterfaceVTable RecordingTray::Impl::vtable = {
    RecordingTray::Impl::method_call,
    RecordingTray::Impl::get_property,
    nullptr,
    {nullptr},
};

RecordingTray::RecordingTray(std::function<void()> stop_callback)
    : impl(new Impl(std::move(stop_callback)))
{
}

RecordingTray::~RecordingTray() = default;

void RecordingTray::set_recording(bool recording)
{
    impl->set_recording(recording);
}
