#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>

#define WALLPAPER_CONFIG_FILE "/home/x/.config/vaxp/wallpaper"
#define WALLPAPER_DIR "/usr/share/backgrounds"
#define WALLPAPER_DIRS_CONFIG "/home/x/.config/vaxp/wallpaper-dirs"

static GtkWidget *main_window = NULL;

static void ensure_config_dir(void) {
    g_mkdir_with_parents("/home/x/.config/vaxp", 0755);
}

static gboolean is_image_file(const char *name) {
    const char *exts[] = { ".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp", ".tiff", ".svg", NULL };
    char *lower = g_ascii_strdown(name, -1);
    gboolean ok = FALSE;

    for (int i = 0; exts[i]; i++) {
        if (g_str_has_suffix(lower, exts[i])) {
            ok = TRUE;
            break;
        }
    }

    g_free(lower);
    return ok;
}

static void add_images_from_dir(const char *dir_path, GtkWidget *flow) {
    GDir *dir = g_dir_open(dir_path, 0, NULL);
    const char *fname;

    if (!dir) return;

    while ((fname = g_dir_read_name(dir))) {
        char *full_path;
        GdkPixbuf *thumb;
        GtkWidget *img;
        GtkWidget *vbox;
        GtkWidget *lbl;

        if (!is_image_file(fname)) continue;

        full_path = g_strdup_printf("%s/%s", dir_path, fname);
        thumb = gdk_pixbuf_new_from_file_at_scale(full_path, 180, 110, FALSE, NULL);
        img = thumb
            ? gtk_image_new_from_pixbuf(thumb)
            : gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_DIALOG);
        if (thumb) g_object_unref(thumb);

        vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_box_pack_start(GTK_BOX(vbox), img, FALSE, FALSE, 0);

        lbl = gtk_label_new(fname);
        gtk_label_set_max_width_chars(GTK_LABEL(lbl), 22);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);

        g_object_set_data_full(G_OBJECT(vbox), "wallpaper-path", full_path, g_free);
        gtk_flow_box_insert(GTK_FLOW_BOX(flow), vbox, -1);
    }

    g_dir_close(dir);
    gtk_widget_show_all(flow);
}

static void on_browse_folder_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *chooser;
    GtkWidget *flow = GTK_WIDGET(user_data);

    (void)btn;

    chooser = gtk_file_chooser_dialog_new(
        "Select Wallpaper Folder",
        GTK_WINDOW(main_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(chooser), 600, 400);

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (folder) {
            char *existing = NULL;
            gboolean already_saved;

            add_images_from_dir(folder, flow);

            g_file_get_contents(WALLPAPER_DIRS_CONFIG, &existing, NULL, NULL);
            already_saved = existing && strstr(existing, folder) != NULL;
            if (!already_saved) {
                GString *buf = g_string_new(existing ? existing : "");
                if (buf->len > 0 && buf->str[buf->len - 1] != '\n')
                    g_string_append_c(buf, '\n');
                g_string_append_printf(buf, "%s\n", folder);
                ensure_config_dir();
                g_file_set_contents(WALLPAPER_DIRS_CONFIG, buf->str, -1, NULL);
                g_string_free(buf, TRUE);
            }

            g_free(existing);
            g_free(folder);
        }
    }

    gtk_widget_destroy(chooser);
}

static void set_wallpaper(const char *path) {
    ensure_config_dir();
    g_file_set_contents(WALLPAPER_CONFIG_FILE, path, -1, NULL);
}

int main(int argc, char *argv[]) {
    GtkWidget *content;
    GtkWidget *toolbar;
    GtkWidget *lbl_path;
    GtkWidget *browse_btn;
    GtkWidget *scroll;
    GtkWidget *flow;

    gtk_init(&argc, &argv);

    main_window = gtk_dialog_new_with_buttons(
        "Change Wallpaper",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Apply", GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(main_window), 800, 540);

    content = gtk_dialog_get_content_area(GTK_DIALOG(main_window));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_bottom(toolbar, 6);
    gtk_box_pack_start(GTK_BOX(content), toolbar, FALSE, FALSE, 0);

    lbl_path = gtk_label_new("Showing: " WALLPAPER_DIR);
    gtk_widget_set_halign(lbl_path, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(toolbar), lbl_path, TRUE, TRUE, 0);

    browse_btn = gtk_button_new_with_label("📁 Add Custom Folder");
    gtk_box_pack_end(GTK_BOX(toolbar), browse_btn, FALSE, FALSE, 0);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);

    flow = gtk_flow_box_new();
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_widget_set_margin_start(flow, 4);
    gtk_widget_set_margin_end(flow, 4);
    gtk_container_add(GTK_CONTAINER(scroll), flow);

    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_browse_folder_clicked), flow);

    add_images_from_dir(WALLPAPER_DIR, flow);

    {
        char *dirs_content = NULL;
        if (g_file_get_contents(WALLPAPER_DIRS_CONFIG, &dirs_content, NULL, NULL)) {
            gchar **lines = g_strsplit(dirs_content, "\n", -1);
            for (int i = 0; lines[i] != NULL; i++) {
                g_strstrip(lines[i]);
                if (strlen(lines[i]) > 1 && lines[i][0] == '/') {
                    add_images_from_dir(lines[i], flow);
                }
            }
            g_strfreev(lines);
            g_free(dirs_content);
        }
    }

    gtk_widget_show_all(main_window);

    if (gtk_dialog_run(GTK_DIALOG(main_window)) == GTK_RESPONSE_ACCEPT) {
        GList *selected = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(flow));
        if (selected) {
            GtkFlowBoxChild *child = GTK_FLOW_BOX_CHILD(selected->data);
            GtkWidget *box = gtk_bin_get_child(GTK_BIN(child));
            const char *path = g_object_get_data(G_OBJECT(box), "wallpaper-path");
            if (path) set_wallpaper(path);
            g_list_free(selected);
        }
    }

    gtk_widget_destroy(main_window);
    return 0;
}
