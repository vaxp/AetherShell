#include <gtk/gtk.h>
#include "osd.h"
#include "notify.h"
#include "osd_sound.h"
#include "osd_udev.h"
#include "config.h"

static void on_config_reloaded(void) {
    g_print("🎨 Configuration reloaded\n");
    notify_reload_ui();
    osd_sound_reload();
}

int main(int argc, char *argv[]) {
    // تهيئة GTK
    gtk_init(&argc, &argv);

    g_print("🎨 Starting vaxp_gui (OSD + Notify)\n");

    // Load configuration
    config_init();
    config_monitor_init(on_config_reloaded);

    // تهيئة نظام الصوت
    osd_sound_init();

    // تهيئة نظام udev لمراقبة الشاحن والـ USB
    osd_udev_init();

    // تهيئة نظام OSD للغات
    osd_init();

    // تهيئة نظام الإشعارات
    notify_init();

    // تشغيل حلقة GTK الأساسية المشتركة
    gtk_main();

    return 0;
}
