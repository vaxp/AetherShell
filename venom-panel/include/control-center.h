/*
 * control-center.h
 *
 * Header for venom-style Control Center widget with support for:
 *   • Volume control (PulseAudio)
 *   • Brightness control (libudev)
 *   • Wi-Fi/Ethernet (NetworkManager libnm)
 *   • Bluetooth (BlueZ via GDBus)
 */

#ifndef CONTROL_CENTER_H
#define CONTROL_CENTER_H

#include <gtk/gtk.h>

GtkWidget* create_control_center(void);

#endif
