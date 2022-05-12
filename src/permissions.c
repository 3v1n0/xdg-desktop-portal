/*
 * Copyright © 2016 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Matthias Clasen <mclasen@redhat.com>
 */

#include "config.h"

#include <string.h>

#include "permissions.h"

static XdpDbusImplPermissionStore *permission_store = NULL;

static gboolean
maybe_check_base_snap_permissions (const char   *app_id,
                                   const char   *table,
                                   const char   *id,
                                   char       ***out_permissions)
{
  const char *app_name_part;

  /* We used to check permissions for the base snap, so if those were granted
   * already we should just return those, migrating to the new name.
   * But we do it only for the main application.
   */
  if (g_str_has_prefix (app_id, "snap.") &&
      (app_name_part = strrchr (app_id, '_')))
    {
      g_autofree char *base_app_id = NULL;
      const char *app_name = app_name_part + 1;
      size_t app_name_part_len = strlen (app_name_part);
      size_t app_len;

      if (app_name_part_len < 2)
        return FALSE;

      app_len = strlen (app_id) - app_name_part_len;
      if (!app_len)
        return FALSE;

      g_assert (strchr (app_id, '_') == app_name_part);
      base_app_id = g_strndup (app_id, app_len);

      if (!g_str_equal (app_name, base_app_id + strlen ("snap.")))
        return FALSE;

      *out_permissions = get_permissions_sync (base_app_id, table, id);

      if (*out_permissions)
        {
          static const char **empty = { NULL };

          set_permissions_sync (app_id, table, id, *out_permissions);
          set_permissions_sync (base_app_id, table, id, empty);
        }

      return TRUE;
    }

  return FALSE;
}

char **
get_permissions_sync (const char *app_id,
                      const char *table,
                      const char *id)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) out_perms = NULL;
  g_autoptr(GVariant) out_data = NULL;
  g_autofree char **permissions = NULL;

  if (!xdp_dbus_impl_permission_store_call_lookup_sync (permission_store,
                                                        table,
                                                        id,
                                                        &out_perms,
                                                        &out_data,
                                                        NULL,
                                                        &error))
    {
      g_dbus_error_strip_remote_error (error);
      g_debug ("No '%s' permissions found: %s", table, error->message);

      if (maybe_check_base_snap_permissions (app_id, table, id, &permissions))
          return g_steal_pointer (&permissions);

      return NULL;
    }

  if (!g_variant_lookup (out_perms, app_id, "^a&s", &permissions))
    {
      g_debug ("No permissions stored for: %s %s, app %s", table, id, app_id);

      if (maybe_check_base_snap_permissions (app_id, table, id, &permissions))
          return g_steal_pointer (&permissions);

      return NULL;
    }

  return g_strdupv (permissions);
}

Permission
permissions_to_tristate (char **permissions)
{
  if (g_strv_length ((char **)permissions) != 1)
    {
      g_autofree char *a = g_strjoinv (" ", (char **)permissions);
      g_warning ("Wrong permission format, ignoring (%s)", a);
      return PERMISSION_UNSET;
    }

  if (strcmp (permissions[0], "yes") == 0)
    return PERMISSION_YES;
  else if (strcmp (permissions[0], "no") == 0)
    return PERMISSION_NO;
  else if (strcmp (permissions[0], "ask") == 0)
    return PERMISSION_ASK;
  else
    {
      g_autofree char *a = g_strjoinv (" ", (char **)permissions);
      g_warning ("Wrong permission format, ignoring (%s)", a);
    }

  return PERMISSION_UNSET;
}

char **
permissions_from_tristate (Permission permission)
{
  char *permission_str;
  char **permissions;

  switch (permission)
    {
    case PERMISSION_UNSET:
      return NULL;
    case PERMISSION_NO:
      permission_str = g_strdup ("no");
      break;
    case PERMISSION_YES:
      permission_str = g_strdup ("yes");
      break;
    case PERMISSION_ASK:
      permission_str = g_strdup ("ask");
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  permissions = g_new0 (char *, 2);
  permissions[0] = permission_str;

  return permissions;
}

void
set_permissions_sync (const char *app_id,
                      const char *table,
                      const char *id,
                      const char * const *permissions)
{
  g_autoptr(GError) error = NULL;

  if (!xdp_dbus_impl_permission_store_call_set_permission_sync (permission_store,
                                                                table,
                                                                TRUE,
                                                                id,
                                                                app_id,
                                                                permissions,
                                                                NULL,
                                                                &error))
    {
      g_dbus_error_strip_remote_error (error);
      g_warning ("Error updating permission store: %s", error->message);
    }
}

Permission
get_permission_sync (const char *app_id,
                     const char *table,
                     const char *id)
{
  g_auto(GStrv) perms = NULL;
  Permission ret = PERMISSION_UNSET;

  perms = get_permissions_sync (app_id, table, id);
  if (perms)
    ret = permissions_to_tristate (perms);

  return ret;
}

void set_permission_sync (const char *app_id,
                          const char *table,
                          const char *id,
                          Permission permission)
{
  g_auto(GStrv) perms = NULL;

  perms = permissions_from_tristate (permission);
  set_permissions_sync (app_id, table, id, (const char * const *)perms);
}

void
init_permission_store (GDBusConnection *connection)
{
  g_autoptr(GError) error = NULL;

  permission_store = xdp_dbus_impl_permission_store_proxy_new_sync (connection,
                                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                                    "org.freedesktop.impl.portal.PermissionStore",
                                                                    "/org/freedesktop/impl/portal/PermissionStore",
                                                                    NULL, &error);
  if (permission_store == NULL)
    g_warning ("No permission store: %s", error->message);
}

XdpDbusImplPermissionStore *
get_permission_store (void)
{
  return permission_store;
}
