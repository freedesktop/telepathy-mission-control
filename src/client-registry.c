/* Map containing registered Telepathy clients
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "client-registry.h"

#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/telepathy-glib.h>

#include "mcd-debug.h"

G_DEFINE_TYPE (McdClientRegistry, _mcd_client_registry, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_DBUS_DAEMON
};

enum
{
    S_CLIENT_ADDED,
    S_READY,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

struct _McdClientRegistryPrivate
{
  /* hash table containing clients
   * owned gchar * well_known_name -> owned McdClientProxy */
  GHashTable *clients;

  TpDBusDaemon *dbus_daemon;

  /* Not really handles as such, but TpHandleRepoIface gives us a convenient
   * reference-counted string pool */
  TpHandleRepoIface *string_pool;

  /* We don't want to start dispatching until startup has finished. This
   * is defined as:
   * - activatable clients have been enumerated (ListActivatableNames)
   *   (1 lock)
   * - running clients have been enumerated (ListNames) (1 lock)
   * - each client found that way is ready (1 lock per client)
   * When nothing more is stopping us from dispatching channels, we signal
   * ready.
   * */
  gsize startup_lock;
  gboolean startup_completed;
};

static void
_mcd_client_registry_inc_startup_lock (McdClientRegistry *self)
{
  if (!self->priv->startup_completed)
    {
      DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
          self->priv->startup_lock, self->priv->startup_lock + 1);
      g_return_if_fail (self->priv->startup_lock > 0);
      self->priv->startup_lock++;
    }
}

static void
_mcd_client_registry_dec_startup_lock (McdClientRegistry *self)
{
  if (self->priv->startup_completed)
    return;

  DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
      self->priv->startup_lock, self->priv->startup_lock - 1);

  g_return_if_fail (self->priv->startup_lock > 0);

  self->priv->startup_lock--;

  if (self->priv->startup_lock == 0)
    {
      self->priv->startup_completed = TRUE;
      g_signal_emit (self, signals[S_READY], 0);
    }
}

static void mcd_client_registry_ready_cb (McdClientProxy *client,
    McdClientRegistry *self);
static void mcd_client_registry_gone_cb (McdClientProxy *client,
    McdClientRegistry *self);

static void
_mcd_client_registry_found_name (McdClientRegistry *self,
    const gchar *well_known_name,
    const gchar *unique_name_if_known,
    gboolean activatable)
{
  McdClientProxy *client;

  if (!g_str_has_prefix (well_known_name, TP_CLIENT_BUS_NAME_BASE))
    {
      /* This is not a Telepathy Client */
      return;
    }

  if (!_mcd_client_check_valid_name (
        well_known_name + MC_CLIENT_BUS_NAME_BASE_LEN, NULL))
    {
      /* This is probably meant to be a Telepathy Client, but it's not */
      DEBUG ("Ignoring invalid Client name: %s",
          well_known_name + MC_CLIENT_BUS_NAME_BASE_LEN);
      return;
    }

  client = g_hash_table_lookup (self->priv->clients, well_known_name);

  if (client != NULL)
    {
      if (activatable)
        {
          /* We already knew that it was active, but now we also know that
           * it is activatable */
          _mcd_client_proxy_set_activatable (client);
        }
      else
        {
          /* We already knew that it was activatable, but now we also know
           * that it is active */
          _mcd_client_proxy_set_active (client, unique_name_if_known);
        }

      return;
    }

  DEBUG ("Registering client %s", well_known_name);

  client = _mcd_client_proxy_new (self->priv->dbus_daemon,
      self->priv->string_pool, well_known_name, unique_name_if_known,
      activatable);
  g_hash_table_insert (self->priv->clients, g_strdup (well_known_name),
      client);

  /* paired with one in mcd_client_registry_ready_cb, when the
   * McdClientProxy is ready */
  _mcd_client_registry_inc_startup_lock (self);

  g_signal_connect (client, "ready",
                    G_CALLBACK (mcd_client_registry_ready_cb),
                    self);

  g_signal_connect (client, "gone",
                    G_CALLBACK (mcd_client_registry_gone_cb),
                    self);

  g_signal_emit (self, signals[S_CLIENT_ADDED], 0, client);
}

McdClientProxy *
_mcd_client_registry_lookup (McdClientRegistry *self,
    const gchar *well_known_name)
{
  g_return_val_if_fail (MCD_IS_CLIENT_REGISTRY (self), NULL);
  return g_hash_table_lookup (self->priv->clients, well_known_name);
}

static void
mcd_client_registry_disconnect_client_signals (gpointer k G_GNUC_UNUSED,
    gpointer v,
    gpointer data)
{
  g_signal_handlers_disconnect_by_func (v, mcd_client_registry_ready_cb, data);
  g_signal_handlers_disconnect_by_func (v, mcd_client_registry_gone_cb, data);

  if (!_mcd_client_proxy_is_ready (v))
    {
      /* we'll never receive the ready signal now, so release the lock that
       * it would otherwise have released */
      DEBUG ("client %s disappeared before it became ready - treating it "
             "as ready for our purposes", tp_proxy_get_bus_name (v));
      mcd_client_registry_ready_cb (v, data);
    }
}

static void
_mcd_client_registry_remove (McdClientRegistry *self,
    const gchar *well_known_name)
{
  McdClientProxy *client;

  client = g_hash_table_lookup (self->priv->clients, well_known_name);

  if (client != NULL)
    {
      mcd_client_registry_disconnect_client_signals (NULL,
          client, self);
    }

  g_hash_table_remove (self->priv->clients, well_known_name);
}

void _mcd_client_registry_init_hash_iter (McdClientRegistry *self,
    GHashTableIter *iter)
{
  g_return_if_fail (MCD_IS_CLIENT_REGISTRY (self));
  g_hash_table_iter_init (iter, self->priv->clients);
}

static void
_mcd_client_registry_init (McdClientRegistry *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MCD_TYPE_CLIENT_REGISTRY,
      McdClientRegistryPrivate);

  self->priv->startup_completed = FALSE;
  /* the ListNames call we'll make in _constructed is the initial lock */
  self->priv->startup_lock = 1;
  self->priv->clients = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);
}

static void
mcd_client_registry_list_activatable_names_cb (TpDBusDaemon *proxy,
    const gchar **names,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (weak_object);

  if (error != NULL)
    {
      DEBUG ("ListActivatableNames returned error, assuming none: %s %d: %s",
          g_quark_to_string (error->domain), error->code, error->message);
    }
  else if (names != NULL)
    {
      const gchar **iter = names;

      DEBUG ("ListActivatableNames returned");

      while (*iter != NULL)
        {
          _mcd_client_registry_found_name (self, *iter, NULL, TRUE);
          iter++;
        }
    }

  /* paired with the lock taken when the McdClientRegistry was constructed */
  _mcd_client_registry_dec_startup_lock (self);
}

static void
mcd_client_registry_list_names_cb (TpDBusDaemon *proxy,
    const gchar **names,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (weak_object);

  if (error != NULL)
    {
      DEBUG ("ListNames returned error, assuming none: %s %d: %s",
          g_quark_to_string (error->domain), error->code, error->message);
    }
  else if (names != NULL)
    {
      const gchar **iter = names;

      DEBUG ("ListNames returned");

      while (*iter != NULL)
        {
          _mcd_client_registry_found_name (self, *iter, NULL, FALSE);
          iter++;
        }
    }

  tp_cli_dbus_daemon_call_list_activatable_names (proxy, -1,
      mcd_client_registry_list_activatable_names_cb,
      NULL, NULL, weak_object);
  /* deliberately not calling _mcd_client_registry_dec_startup_lock here -
   * this function is "lock-neutral", similarly to list_names_cb (we would
   * take a lock for ListActivatableNames then release the one used for
   * ReloadConfig), so simplify by doing nothing */
}

static void
mcd_client_registry_name_owner_changed_cb (TpDBusDaemon *proxy,
    const gchar *name,
    const gchar *old_owner,
    const gchar *new_owner,
    gpointer user_data G_GNUC_UNUSED,
    GObject *weak_object)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (weak_object);

  /* dbus-glib guarantees this */
  g_assert (name != NULL);
  g_assert (old_owner != NULL);
  g_assert (new_owner != NULL);

  if (old_owner[0] == '\0' && new_owner[0] != '\0')
    {
      _mcd_client_registry_found_name (self, name, new_owner, FALSE);
    }
}

static void
mcd_client_registry_constructed (GObject *object)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (object);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (_mcd_client_registry_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  g_return_if_fail (self->priv->dbus_daemon != NULL);

  DEBUG ("Starting to look for clients");

  /* FIXME: ideally, this would be a more specific match, using arg0prefix
   * (when dbus-daemon supports that, which it doesn't yet) so we only get
   * new clients. */
  tp_cli_dbus_daemon_connect_to_name_owner_changed (self->priv->dbus_daemon,
      mcd_client_registry_name_owner_changed_cb, NULL, NULL, object, NULL);

  tp_cli_dbus_daemon_call_list_names (self->priv->dbus_daemon, -1,
      mcd_client_registry_list_names_cb, NULL, NULL, object);

  /* Dummy handle type, we're just using this as a string pool */
  self->priv->string_pool = tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_CONTACT,
      NULL, NULL);
}

static void
mcd_client_registry_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (object);

  switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
      g_assert (self->priv->dbus_daemon == NULL); /* it's construct-only */
      self->priv->dbus_daemon = TP_DBUS_DAEMON (g_value_dup_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mcd_client_registry_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (object);

  switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
      g_value_set_object (value, self->priv->dbus_daemon);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mcd_client_registry_dispose (GObject *object)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (object);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (_mcd_client_registry_parent_class)->dispose;

  tp_clear_object (&self->priv->dbus_daemon);
  tp_clear_object (&self->priv->string_pool);

  if (self->priv->clients != NULL)
    {
      g_hash_table_foreach (self->priv->clients,
          mcd_client_registry_disconnect_client_signals, self);

    }

  tp_clear_pointer (&self->priv->clients, g_hash_table_destroy);

  if (chain_up != NULL)
    chain_up (object);
}

static void
_mcd_client_registry_class_init (McdClientRegistryClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  g_type_class_add_private (cls, sizeof (McdClientRegistryPrivate));

  object_class->constructed = mcd_client_registry_constructed;
  object_class->get_property = mcd_client_registry_get_property;
  object_class->set_property = mcd_client_registry_set_property;
  object_class->dispose = mcd_client_registry_dispose;

  g_object_class_install_property (object_class, PROP_DBUS_DAEMON,
      g_param_spec_object ("dbus-daemon", "D-Bus daemon", "D-Bus daemon",
        TP_TYPE_DBUS_DAEMON,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  signals[S_CLIENT_ADDED] = g_signal_new ("client-added",
      G_OBJECT_CLASS_TYPE (cls),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, MCD_TYPE_CLIENT_PROXY);

  signals[S_READY] = g_signal_new ("ready",
      G_OBJECT_CLASS_TYPE (cls),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);
}

McdClientRegistry *
_mcd_client_registry_new (TpDBusDaemon *dbus_daemon)
{
  return g_object_new (MCD_TYPE_CLIENT_REGISTRY,
      "dbus-daemon", dbus_daemon,
      NULL);
}

static void
mcd_client_registry_ready_cb (McdClientProxy *client,
    McdClientRegistry *self)
{
  DEBUG ("%s", tp_proxy_get_bus_name (client));

  g_signal_handlers_disconnect_by_func (client,
      mcd_client_registry_ready_cb, self);

  /* paired with the one in _mcd_client_registry_found_name */
  _mcd_client_registry_dec_startup_lock (self);
}

static void
mcd_client_registry_gone_cb (McdClientProxy *client,
    McdClientRegistry *self)
{
  _mcd_client_registry_remove (self, tp_proxy_get_bus_name (client));
}

GPtrArray *
_mcd_client_registry_dup_client_caps (McdClientRegistry *self)
{
  GPtrArray *vas;
  GHashTableIter iter;
  gpointer p;

  g_return_val_if_fail (MCD_IS_CLIENT_REGISTRY (self), NULL);

  vas = g_ptr_array_sized_new (g_hash_table_size (self->priv->clients));

  g_hash_table_iter_init (&iter, self->priv->clients);

  while (g_hash_table_iter_next (&iter, NULL, &p))
    {
      g_ptr_array_add (vas,
          _mcd_client_proxy_dup_handler_capabilities (p));
    }

  return vas;
}

gboolean
_mcd_client_registry_is_ready (McdClientRegistry *self)
{
  g_return_val_if_fail (MCD_IS_CLIENT_REGISTRY (self), FALSE);
  return self->priv->startup_completed;
}

typedef struct
{
    McdClientProxy *client;
    gboolean bypass;
    gsize quality;
} PossibleHandler;

static gint
possible_handler_cmp (gconstpointer a_,
                      gconstpointer b_)
{
    const PossibleHandler *a = a_;
    const PossibleHandler *b = b_;

    if (a->bypass)
    {
        if (!b->bypass)
        {
            /* BypassApproval wins, so a is better than b */
            return 1;
        }
    }
    else if (b->bypass)
    {
        /* BypassApproval wins, so b is better than a */
        return -1;
    }

    if (a->quality < b->quality)
    {
        return -1;
    }

    if (b->quality < a->quality)
    {
        return 1;
    }

    return 0;
}

GList *
_mcd_client_registry_list_possible_handlers (McdClientRegistry *self,
    McdRequest *request,
    const GList *channels,
    const gchar *must_have_unique_name)
{
    GList *handlers = NULL;
    const GList *iter;
    GList *handlers_iter;
    GHashTableIter client_iter;
    gpointer client_p;

    _mcd_client_registry_init_hash_iter (self, &client_iter);

    while (g_hash_table_iter_next (&client_iter, NULL, &client_p))
    {
        McdClientProxy *client = MCD_CLIENT_PROXY (client_p);
        gsize total_quality = 0;

        if (must_have_unique_name != NULL &&
            tp_strdiff (must_have_unique_name,
                        _mcd_client_proxy_get_unique_name (client)))
        {
            /* we're trying to redispatch to an existing handler, and this is
             * not it */
            continue;
        }

        if (!tp_proxy_has_interface_by_id (client,
                                           TP_IFACE_QUARK_CLIENT_HANDLER))
        {
            /* not a handler at all */
            continue;
        }

        if (channels == NULL)
        {
            /* We don't know any channels' properties (the next loop will not
             * execute), so we must work out the quality of match from the
             * channel request. We can assume that the request will return one
             * channel, with the requested properties, plus Requested == TRUE.
             */
            g_assert (request != NULL);
            total_quality = _mcd_client_match_filters (
                _mcd_request_get_properties (request),
                _mcd_client_proxy_get_handler_filters (client), TRUE);
        }

        for (iter = channels; iter != NULL; iter = iter->next)
        {
            TpChannel *channel = iter->data;
            GHashTable *properties;
            guint quality;

            g_assert (TP_IS_CHANNEL (channel));
            properties = tp_channel_borrow_immutable_properties (channel);

            quality = _mcd_client_match_filters (properties,
                _mcd_client_proxy_get_handler_filters (client), FALSE);

            if (quality == 0)
            {
                total_quality = 0;
                break;
            }
            else
            {
                total_quality += quality;
            }
        }

        if (total_quality > 0)
        {
            PossibleHandler *ph = g_slice_new0 (PossibleHandler);

            ph->client = client;
            ph->bypass = _mcd_client_proxy_get_bypass_approval (client);
            ph->quality = total_quality;

            handlers = g_list_prepend (handlers, ph);
        }
    }

    /* if no handlers can take them all, fail - unless we're operating on
     * a request that specified a preferred handler, in which case assume
     * it's suitable */
    if (handlers == NULL)
    {
        McdClientProxy *client;
        const gchar *preferred_handler = NULL;

        if (request != NULL)
        {
            preferred_handler = _mcd_request_get_preferred_handler (request);
        }

        if (preferred_handler == NULL || preferred_handler[0] == '\0')
        {
            return NULL;
        }

        client = _mcd_client_registry_lookup (self, preferred_handler);

        if (client == NULL)
        {
            return NULL;
        }

        return g_list_append (NULL, client);
    }

    /* We have at least one handler that can take the whole batch. Sort
     * the possible handlers, most preferred first (i.e. sort by ascending
     * quality then reverse) */
    handlers = g_list_sort (handlers, possible_handler_cmp);
    handlers = g_list_reverse (handlers);

    /* convert in-place from a list of PossibleHandler to a list of
     * McdClientProxy */
    for (handlers_iter = handlers;
         handlers_iter != NULL;
         handlers_iter = handlers_iter->next)
    {
        PossibleHandler *ph = handlers_iter->data;

        handlers_iter->data = ph->client;
        g_slice_free (PossibleHandler, ph);
    }

    return handlers;
}
