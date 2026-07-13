#include <gtk/gtk.h>
#include <malloc.h>
#include "wallpaper.h"

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    init_main_window();

    gtk_widget_show_all(main_window);

    init_wallpaper_monitor();
    load_saved_wallpaper();
    malloc_trim(0);
    
    gtk_main();

    return 0;
}
