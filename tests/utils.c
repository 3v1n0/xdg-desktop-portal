#include <config.h>

#include "utils.h"

#include <glib/gstdio.h>

/*
 * Set a property. Unlike gdbus-codegen-generated wrapper functions, this
 * waits for the property change to take effect.
 *
 * If @value is floating, ownership is taken.
 */
gboolean
tests_set_property_sync (GDBusProxy *proxy,
                         const char *iface,
                         const char *property,
                         GVariant *value,
                         GError **error)
{
  g_autoptr (GVariant) res = NULL;

  res = g_dbus_proxy_call_sync (proxy,
                                "org.freedesktop.DBus.Properties.Set",
                                g_variant_new ("(ssv)", iface, property, value),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                error);
  return (res != NULL);
}

/* We need this to ensure that dbus-daemon launched by GTestDBus is not
 * causing our tests to hang (see GNOME/glib#2537), so we are redirecting
 * all its output to stderr, while reading its pid and address to manage it.
 * As bonus point, now the services output will be visible in test logs.
 * This can be removed once GNOME/glib!2354 will be merged.
 */
void
setup_dbus_daemon_wrapper (const char *outdir)
{
  g_autofree gchar *file_name = NULL;
  g_autofree gchar *test_path = NULL;
  g_autoptr (GError) error = NULL;
  const char dbus_daemon_script[] = \
   "#!/usr/bin/env bash\n"
    "export PATH=\"$ORIGINAL_PATH\"\n"
    "\n"
    "cleanup() {\n"
    "    [ -n \"$pid\" ] && kill $pid || true\n"
    "    rm -f \"$XDG_RUNTIME_DIR/dbus_pid\"\n"
    "    rm -f \"$XDG_RUNTIME_DIR/dbus_address\"\n"
    "    exec 5<&-\n"
    "    exec 6<&-\n"
    "}\n"
    "\n"
    "mkfifo \"$XDG_RUNTIME_DIR/dbus_pid\"\n"
    "exec 5<> \"$XDG_RUNTIME_DIR/dbus_pid\"\n"
    "\n"
    "mkfifo \"$XDG_RUNTIME_DIR/dbus_address\"\n"
    "exec 6<> \"$XDG_RUNTIME_DIR/dbus_address\"\n"
    "\n"
    "dbus-daemon \"$@\" --print-pid=5 --print-address=6 1>&2 &\n"
    "read -ru 5 pid\n"
    "read -ru 6 address\n"
    "echo \"$address\"\n"
    "echo waiting for dbus-daemon PID $pid... >&2\n"
    "\n"
    "trap \"cleanup\" EXIT\n"
    "wait $pid\n"
    "pid=\n";

  g_print("%s\n", dbus_daemon_script);

  test_path = g_strjoin (":", outdir, g_getenv ("PATH"), NULL);
  g_setenv ("ORIGINAL_PATH", g_getenv ("PATH"), TRUE);
  g_setenv ("PATH", test_path, TRUE);

  file_name = g_build_filename (outdir, "dbus-daemon", NULL);
  g_file_set_contents (file_name, dbus_daemon_script, -1, &error);
  g_chmod (file_name, 0700);
  g_assert_no_error (error);
}
