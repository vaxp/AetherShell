#include <gtk/gtk.h>

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(box, 360, -1);
    gtk_container_add(GTK_CONTAINER(win), box);
    
    gint min_w, nat_w;
    gtk_widget_get_preferred_width(win, &min_w, &nat_w);
    g_print("min: %d, nat: %d\n", min_w, nat_w);
    return 0;
}
