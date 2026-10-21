#include <gtk/gtk.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *pin_label;
    GtkWidget *status;
    GtkWidget *login_button;
    char username[128];
    char pin[128];
    size_t pin_len;
    gboolean busy;
} App;

static const char *css =
    "window { background: #1e1e1e; }"
    ".card { background: #2b2b2b; border-radius: 24px; padding: 28px; }"
    ".avatar { background: #3584e4; color: white; border-radius: 48px; "
    "font-size: 42px; font-weight: 700; min-width: 96px; min-height: 96px; }"
    ".user { color: #f6f5f4; font-size: 23px; font-weight: 600; }"
    ".hint { color: #c0bfbc; font-size: 15px; }"
    ".pin { color: #f6f5f4; font-size: 28px; letter-spacing: 8px; min-height: 42px; }"
    "button.key { min-width: 86px; min-height: 64px; margin: 4px; border-radius: 16px; "
    "font-size: 25px; }"
    "button.action { min-height: 58px; border-radius: 14px; font-size: 18px; font-weight: 700; }"
    ".error { color: #ff7b7b; font-size: 15px; }";

static void set_status(App *app, const char *text, gboolean error) {
    gtk_label_set_text(GTK_LABEL(app->status), text);
    if (error) gtk_widget_add_css_class(app->status, "error");
    else gtk_widget_remove_css_class(app->status, "error");
}

static void update_pin(App *app) {
    char dots[256] = {0};
    for (size_t i = 0; i < app->pin_len && i < 120; i++) strcat(dots, "● ");
    gtk_label_set_text(GTK_LABEL(app->pin_label), dots);
}

static int read_full(int fd, void *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, (char *)buf + done, len - done);
        if (n <= 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

static char *ipc_request(int fd, const char *json) {
    uint32_t len = (uint32_t)strlen(json);
    if (write(fd, &len, sizeof(len)) != (ssize_t)sizeof(len)) return NULL;
    if (write(fd, json, len) != (ssize_t)len) return NULL;
    uint32_t response_len = 0;
    if (read_full(fd, &response_len, sizeof(response_len)) < 0 || response_len > 1048576) return NULL;
    char *response = calloc(response_len + 1, 1);
    if (!response || read_full(fd, response, response_len) < 0) {
        free(response);
        return NULL;
    }
    return response;
}

static int connect_greetd(void) {
    const char *path = g_getenv("GREETD_SOCK");
    if (!path) path = "/run/greetd.sock";
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void authenticate(App *app) {
    int fd = connect_greetd();
    if (fd < 0) {
        set_status(app, "Login service unavailable", TRUE);
        return;
    }

    char create[256];
    g_snprintf(create, sizeof(create), "{\"type\":\"create_session\",\"username\":\"%s\"}", app->username);
    char *reply = ipc_request(fd, create);
    while (reply && strstr(reply, "\"type\":\"auth_message\"")) {
        char auth[512];
        g_snprintf(auth, sizeof(auth), "{\"type\":\"post_auth_message_response\",\"response\":\"%s\"}", app->pin);
        free(reply);
        reply = ipc_request(fd, auth);
    }

    if (!reply || !strstr(reply, "\"type\":\"success\"")) {
        set_status(app, "Incorrect PIN", TRUE);
        free(reply);
        close(fd);
        return;
    }
    free(reply);

    reply = ipc_request(fd,
        "{\"type\":\"start_session\",\"cmd\":[\"/usr/local/bin/phosh-session-wrapper\"],"
        "\"env\":[\"XDG_CURRENT_DESKTOP=Phosh\",\"XDG_SESSION_DESKTOP=phosh\","
        "\"XDG_SESSION_TYPE=wayland\",\"WAYLAND_DISPLAY=wayland-0\",\"DISPLAY=\","
        "\"GDK_BACKEND=wayland\",\"QT_QPA_PLATFORM=wayland\","
        "\"GTK_IM_MODULE=wayland\",\"QT_IM_MODULE=wayland\"]}");
    if (reply && strstr(reply, "\"type\":\"success\"")) {
        free(reply);
        close(fd);
        g_application_quit(g_application_get_default());
        return;
    }
    if (!reply || !strstr(reply, "\"type\":\"success\""))
        set_status(app, "Could not start Phosh", TRUE);
    free(reply);
    close(fd);
}

static void login_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    if (app->busy || app->pin_len == 0) return;
    app->busy = TRUE;
    gtk_widget_set_sensitive(app->login_button, FALSE);
    set_status(app, "Checking PIN…", FALSE);
    authenticate(app);
    if (app->window && gtk_widget_get_visible(app->window)) {
        app->busy = FALSE;
        gtk_widget_set_sensitive(app->login_button, TRUE);
        app->pin_len = 0;
        app->pin[0] = '\0';
        update_pin(app);
    }
}

static void key_clicked(GtkButton *button, gpointer data) {
    App *app = data;
    const char *key = gtk_button_get_label(button);
    if (strcmp(key, "⌫") == 0) {
        if (app->pin_len) app->pin[--app->pin_len] = '\0';
    } else if (strcmp(key, "Clear") == 0) {
        app->pin_len = 0;
        app->pin[0] = '\0';
    } else if (app->pin_len + 1 < sizeof(app->pin)) {
        app->pin[app->pin_len++] = key[0];
        app->pin[app->pin_len] = '\0';
    }
    update_pin(app);
}

static GtkWidget *key_button(App *app, GtkGrid *grid, const char *label, int col, int row, int width) {
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "key");
    gtk_grid_attach(grid, button, col, row, width, 1);
    g_signal_connect(button, "clicked", G_CALLBACK(key_clicked), app);
    return button;
}

static void app_activate(GtkApplication *gtk_app, gpointer data) {
    App *app = data;
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "Phosh login");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 640, 760);

    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center, GTK_ALIGN_CENTER);
    gtk_window_set_child(GTK_WINDOW(app->window), center);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_set_size_request(card, 430, -1);
    gtk_box_append(GTK_BOX(center), card);

    GtkWidget *avatar = gtk_label_new("T");
    gtk_widget_add_css_class(avatar, "avatar");
    gtk_widget_set_halign(avatar, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), avatar);

    GtkWidget *user = gtk_label_new(app->username);
    gtk_widget_add_css_class(user, "user");
    gtk_widget_set_halign(user, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), user);

    GtkWidget *hint = gtk_label_new("Enter your PIN to continue");
    gtk_widget_add_css_class(hint, "hint");
    gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), hint);

    app->pin_label = gtk_label_new("");
    gtk_widget_add_css_class(app->pin_label, "pin");
    gtk_widget_set_halign(app->pin_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), app->pin_label);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    const char *numbers[] = {"1","2","3","4","5","6","7","8","9"};
    for (int i = 0; i < 9; i++) key_button(app, GTK_GRID(grid), numbers[i], i % 3, i / 3, 1);
    key_button(app, GTK_GRID(grid), "Clear", 0, 3, 1);
    key_button(app, GTK_GRID(grid), "0", 1, 3, 1);
    key_button(app, GTK_GRID(grid), "⌫", 2, 3, 1);
    gtk_box_append(GTK_BOX(card), grid);

    app->login_button = gtk_button_new_with_label("Continue");
    gtk_widget_add_css_class(app->login_button, "action");
    gtk_box_append(GTK_BOX(card), app->login_button);
    g_signal_connect(app->login_button, "clicked", G_CALLBACK(login_clicked), app);

    app->status = gtk_label_new("Phosh mobile session");
    gtk_widget_add_css_class(app->status, "hint");
    gtk_widget_set_halign(app->status, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), app->status);

    gtk_window_present(GTK_WINDOW(app->window));
}

static void load_config(App *app) {
    g_strlcpy(app->username, "tails1154", sizeof(app->username));
    gchar *contents = NULL;
    if (g_file_get_contents("/etc/phosh-greeter.conf", &contents, NULL, NULL)) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (gchar **line = lines; *line; line++) {
            if (g_str_has_prefix(*line, "username="))
                g_strlcpy(app->username, *line + 9, sizeof(app->username));
        }
        g_strfreev(lines);
        g_free(contents);
    }
}

int main(int argc, char **argv) {
    App app = {0};
    load_config(&app);
    GtkApplication *gtk_app = gtk_application_new("org.tails1154.PhoshGreeter", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(app_activate), &app);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
