#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>

#define WALLPAPER_CONFIG_FILE "/home/x/.config/vaxp/wallpaper"
#define WALLPAPER_DIR "/usr/share/backgrounds"
#define WALLPAPER_DIRS_CONFIG "/home/x/.config/vaxp/wallpaper-dirs"

static GtkWidget *main_window = NULL;

#define ANIM_CONFIG_FILE "/home/x/.config/vaxp/wallpaper-anim"

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

typedef struct {
    GtkWidget *flow;
    char *dir_path;
    GDir *dir;
} DirLoader;

static gboolean load_next_image(gpointer user_data) {
    DirLoader *loader = user_data;
    const char *fname;

    for (int i = 0; i < 3; i++) {
        fname = g_dir_read_name(loader->dir);
        if (!fname) {
            g_dir_close(loader->dir);
            g_free(loader->dir_path);
            g_free(loader);
            return G_SOURCE_REMOVE;
        }

        if (!is_image_file(fname)) continue;

        char *full_path = g_strdup_printf("%s/%s", loader->dir_path, fname);
        GdkPixbuf *thumb = gdk_pixbuf_new_from_file_at_scale(full_path, 180, 110, FALSE, NULL);
        GtkWidget *img = thumb
            ? gtk_image_new_from_pixbuf(thumb)
            : gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_DIALOG);
        if (thumb) g_object_unref(thumb);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_box_pack_start(GTK_BOX(vbox), img, FALSE, FALSE, 0);

        GtkWidget *lbl = gtk_label_new(fname);
        gtk_label_set_max_width_chars(GTK_LABEL(lbl), 22);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);

        g_object_set_data_full(G_OBJECT(vbox), "wallpaper-path", full_path, g_free);
        gtk_flow_box_insert(GTK_FLOW_BOX(loader->flow), vbox, -1);
        gtk_widget_show_all(vbox);
    }

    return G_SOURCE_CONTINUE;
}

static void add_images_from_dir(const char *dir_path, GtkWidget *flow) {
    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir) return;

    DirLoader *loader = g_new(DirLoader, 1);
    loader->flow = flow;
    loader->dir_path = g_strdup(dir_path);
    loader->dir = dir;

    g_idle_add(load_next_image, loader);
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

static void on_anim_changed(GtkComboBox *combo, gpointer user_data) {
    (void)user_data;
    int id = gtk_combo_box_get_active(combo);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d\n", id);
    ensure_config_dir();
    g_file_set_contents(ANIM_CONFIG_FILE, buf, -1, NULL);
}

static int get_saved_anim(void) {
    char *contents = NULL;
    int id = 0;
    if (g_file_get_contents(ANIM_CONFIG_FILE, &contents, NULL, NULL)) {
        id = atoi(contents);
        g_free(contents);
    }
    if (id < 0 || id > 4) id = 0;
    return id;
}

static void on_wallpaper_selected(GtkFlowBox *box, GtkFlowBoxChild *child, gpointer user_data) {
    (void)box;
    (void)user_data;
    GtkWidget *vbox = gtk_bin_get_child(GTK_BIN(child));
    const char *path = g_object_get_data(G_OBJECT(vbox), "wallpaper-path");
    if (path) {
        set_wallpaper(path);
    }
}

int main(int argc, char *argv[]) {
    GtkWidget *content;
    GtkWidget *toolbar;
    GtkWidget *lbl_path;
    GtkWidget *anim_combo;
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

    GdkScreen *screen = gtk_widget_get_screen(main_window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(main_window, visual);
        gtk_widget_set_app_paintable(main_window, TRUE);
    }

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window, window.background, dialog { background-color: rgba(0, 0, 0, 0.3); background-image: none; }"
        "label { color: white; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    content = gtk_dialog_get_content_area(GTK_DIALOG(main_window));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_bottom(toolbar, 6);
    gtk_box_pack_start(GTK_BOX(content), toolbar, FALSE, FALSE, 0);

    lbl_path = gtk_label_new("Showing: " WALLPAPER_DIR);
    gtk_widget_set_halign(lbl_path, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(toolbar), lbl_path, TRUE, TRUE, 0);

    anim_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(anim_combo), "1. Sliding Doors");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(anim_combo), "2. Circle Reveal");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(anim_combo), "3. Smooth Crossfade");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(anim_combo), "4. Wipe Right");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(anim_combo), "5. Zoom Out & Fade");
    gtk_combo_box_set_active(GTK_COMBO_BOX(anim_combo), get_saved_anim());
    g_signal_connect(anim_combo, "changed", G_CALLBACK(on_anim_changed), NULL);
    gtk_box_pack_end(GTK_BOX(toolbar), anim_combo, FALSE, FALSE, 0);

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

    g_signal_connect(flow, "child-activated", G_CALLBACK(on_wallpaper_selected), NULL);
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
