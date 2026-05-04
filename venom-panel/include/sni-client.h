#ifndef SNI_TRAY_H
#define SNI_TRAY_H

#include <gtk/gtk.h>

// Creates the system tray widget. Connects to the venom_sni daemon via D-Bus.
// Returns a GtkBox populated with tray icons, updating dynamically.
GtkWidget* create_sni_tray_widget(void);

#endif // SNI_TRAY_H
