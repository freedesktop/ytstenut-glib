/*
 * Copyright © 2011 Intel Corp.
 *
 * This  library is free  software; you can  redistribute it and/or
 * modify it  under  the terms  of the  GNU Lesser  General  Public
 * License  as published  by the Free  Software  Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed  in the hope that it will be useful,
 * but  WITHOUT ANY WARRANTY; without even  the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Authored by: Tomas Frydrych <tf@linux.intel.com>
 *              Rob Staudinger <robsta@linux.intel.com>
 */
#include "config.h"

#include <string.h>
#include <rest/rest-xml-parser.h>
#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/connection-manager.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/account.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/contact.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-ytstenut-glib/telepathy-ytstenut-glib.h>

#include "ytstenut-internal.h"
#include "yts-adapter-factory.h"
#include "yts-client-internal.h"
#include "yts-client-status.h"
#include "yts-contact-internal.h"
#include "yts-enum-types.h"
#include "yts-error-message.h"
#include "yts-event-message.h"
#include "yts-file-transfer.h"
#include "yts-incoming-file-internal.h"
#include "yts-invocation-message.h"
#include "yts-marshal.h"
#include "yts-metadata-internal.h"
#include "yts-outgoing-file-internal.h"
#include "yts-response-message.h"
#include "yts-roster-impl.h"
#include "yts-service.h"
#include "yts-service-adapter.h"
#include "yts-xml.h"

#include "profile/yts-profile.h"
#include "profile/yts-profile-adapter.h"
#include "profile/yts-profile-impl.h"

#define RECONNECT_DELAY 20 /* in seconds */

static void yts_client_make_connection (YtsClient *client);

G_DEFINE_TYPE (YtsClient, yts_client, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), YTS_TYPE_CLIENT, YtsClientPrivate))

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN PACKAGE"\0client\0"G_STRLOC

/**
 * SECTION: yts-client
 * @short_description: Represents a connection to the Ytstenut mesh.
 *
 * #YtsClient is an object that mediates connection between the current
 * application and the Ytstenut application mesh. It provides access to roster
 * of availalble services (#YtsRoster) and means to advertises status within
 * the mesh.
 */

typedef struct {
  YtsRoster       *roster;    /* the roster of this client */
  YtsRoster       *unwanted;  /* roster of unwanted items */
  YtsClientStatus *client_status;

  /* connection parameters */
  char        *account_id;
  char        *service_id;
  YtsProtocol  protocol;

  /* Telepathy bits */
  TpYtsAccountManager  *tp_am;
  TpAccount            *tp_account;
  TpConnection         *tp_conn;
  TpProxy              *tp_debug_proxy;
  TpYtsStatus          *tp_status;
  TpYtsClient          *tp_client;
  TpBaseClient         *tp_file_handler;

  /* Implemented services */
  GHashTable  *services;

  /* Ongoing invocations */
  GHashTable  *invocations;

  /* Registered proxies */
  GHashTable *proxies;

  /* callback ids */
  guint reconnect_id;

  bool authenticated;   /* are we authenticated ? */
  bool ready;           /* is TP setup done ? */
  bool connect;         /* connect once we get our connection ? */
  bool reconnect;       /* should we attempt to reconnect ? */
  bool dialing;         /* are we currently acquiring connection ? */
  bool members_pending; /* requery members when TP set up completed ? */
  bool prepared;        /* are connection features set up ? */
  bool disposed;        /* dispose guard */

} YtsClientPrivate;

enum
{
  AUTHENTICATED,
  READY,
  DISCONNECTED,
  RAW_MESSAGE,
  TEXT_MESSAGE,
  LIST_MESSAGE,
  DICTIONARY_MESSAGE,
  ERROR,
  INCOMING_FILE,
  N_SIGNALS,
};

enum
{
  PROP_0,
  PROP_ACCOUNT_ID,
  PROP_CONTACT_ID,
  PROP_SERVICE_ID,
  PROP_PROTOCOL,

  PROP_TP_ACCOUNT,
  PROP_TP_STATUS
};

static guint signals[N_SIGNALS] = {0};

/*
 * ServiceData
 */

typedef struct {
  YtsClient  *client;
  char        *capability;
} ServiceData;

static ServiceData *
service_data_create (YtsClient *client,
                     char const *capability)
{
  ServiceData *self;

  g_return_val_if_fail (YTS_IS_CLIENT (client), NULL);
  g_return_val_if_fail (capability, NULL);

  self = g_new0 (ServiceData, 1);
  self->client = g_object_ref (client);
  self->capability = g_strdup (capability);

  return self;
}

static void
service_data_destroy (ServiceData *self)
{
  g_return_if_fail (self);

  g_object_unref (self->client);
  g_free (self->capability);
  g_free (self);
}

/*
 * InvocationData
 */

/* PONDERING this should probably be configurable. */
#define INVOCATION_RESPONSE_TIMEOUT_S 20

typedef struct {
  YtsClient    *client;            /* free pointer, no ref */
  YtsContact   *contact;           /* free pointer, no ref */
  char          *proxy_id;
  char          *invocation_id;
  unsigned int   timeout_s;
  unsigned int   timeout_id;
} InvocationData;

static void
invocation_data_destroy (InvocationData *self)
{
  g_return_if_fail (self);

  if (self->timeout_id) {
    g_source_remove (self->timeout_id);
    self->timeout_id = 0;
  }

  g_free (self->proxy_id);
  g_free (self->invocation_id);
  g_free (self);
}

static bool
client_conclude_invocation (YtsClient  *self,
                            char const  *invocation_id)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  bool found;

  found = g_hash_table_remove (priv->invocations, invocation_id);
  if (!found) {
    g_warning ("%s : Pending invocation for ID %s not found",
               G_STRLOC,
               invocation_id);
    return false;
  }

  return true;
}

static bool
_invocation_timeout (InvocationData *self)
{
  g_critical ("%s : Invocation %s timed out after %i seconds",
              G_STRLOC,
              self->invocation_id,
              self->timeout_s);

  /* This destroys self */
  client_conclude_invocation (self->client, self->invocation_id);

  // TODO emit timeout / error

  /* Remove timeout */
  return false;
}

static InvocationData *
invocation_data_create (YtsClient    *client,
                        YtsContact   *contact,
                        char const    *proxy_id,
                        char const    *invocation_id,
                        unsigned int   timeout_s)
{
  InvocationData *self;

  self = g_new0 (InvocationData, 1);
  self->client = client;
  self->contact = contact;
  self->proxy_id = g_strdup (proxy_id);
  self->invocation_id = g_strdup (invocation_id);
  self->timeout_s = timeout_s;
  self->timeout_id = g_timeout_add_seconds (timeout_s,
                                            (GSourceFunc) _invocation_timeout,
                                            self);

  return self;
}

static bool
client_establish_invocation (YtsClient   *self,
                             char const   *invocation_id,
                             YtsContact  *contact,
                             char const   *proxy_id)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  InvocationData *invocation_data;

  invocation_data = g_hash_table_lookup (priv->invocations,
                                         invocation_id);
  if (invocation_data) {
    /* Already an invocation running with this ID, bail out. */
    g_critical ("%s: Already have an invocation for ID %s",
                G_STRLOC,
                invocation_id);
    return false;
  }

  invocation_data = invocation_data_create (self,
                                            contact,
                                            proxy_id,
                                            invocation_id,
                                            INVOCATION_RESPONSE_TIMEOUT_S);
  g_hash_table_insert (priv->invocations,
                       g_strdup (invocation_id),
                       invocation_data);

  return true;
}

/*
 * ProxyData
 */

typedef struct {
  YtsContact const *contact;    /* free pointer, no ref. */
  char              *proxy_id;
} ProxyData;

static ProxyData *
proxy_data_create (YtsContact const  *contact,
                   char const         *proxy_id)
{
  ProxyData *self;

  self = g_new0 (ProxyData, 1);
  self->contact = contact;
  self->proxy_id = g_strdup (proxy_id);

  return self;
}

static void
proxy_data_destroy (ProxyData *self)
{
  g_free (self->proxy_id);
  g_free (self);
}

/*
 * ProxyList
 */

typedef struct {
  GList *list;
} ProxyList;

static ProxyList *
proxy_list_create_with_proxy (YtsContact const *contact,
                              char const        *proxy_id)
{
  ProxyList *self;
  ProxyData *data;

  self = g_new0 (ProxyList, 1);

  data = proxy_data_create (contact, proxy_id);

  self->list = g_list_append (NULL, data);

  return self;
}

static bool
proxy_list_ensure_proxy (ProxyList          *self,
                         YtsContact const  *contact,
                         char const         *proxy_id)
{
  GList const *iter;
  ProxyData   *proxy_data;

  g_return_val_if_fail (self, false);
  g_warn_if_fail (self->list);

  for (iter = self->list; iter; iter = iter->next) {
    proxy_data = (ProxyData *) iter->data;
    if (proxy_data->contact == contact &&
        0 == g_strcmp0 (proxy_data->proxy_id, proxy_id)) {
      /* Proxy already in list */
      return false;
    }
  }

  proxy_data = proxy_data_create (contact, proxy_id);
  self->list = g_list_prepend (self->list, proxy_data);

  return true;
}

static void
proxy_list_purge_contact (ProxyList         *self,
                          YtsContact const *contact)
{
  GList *iter;
  bool   found;

  g_return_if_fail (self);
  g_return_if_fail (self->list);

  // FIXME need to do this in a smarter way.
  do {
    found = false;
    for (iter = self->list; iter; iter = iter->next) {

      ProxyData *data = (ProxyData *) iter->data;

      if (data->contact == contact) {
        proxy_data_destroy (data);
        iter->data = NULL;
        self->list = g_list_delete_link (self->list, iter);
        found = true;
        break;
      }
    }
  } while (found);
}

static void
proxy_list_purge_proxy_id (ProxyList  *self,
                           char const *proxy_id)
{
  GList *iter;
  bool   found;

  g_return_if_fail (self);
  g_return_if_fail (self->list);

  // FIXME need to do this in a smarter way.
  do {
    found = false;
    for (iter = self->list; iter; iter = iter->next) {

      ProxyData *data = (ProxyData *) iter->data;

      if (0 == g_strcmp0 (data->proxy_id, proxy_id)) {
        proxy_data_destroy (data);
        iter->data = NULL;
        self->list = g_list_delete_link (self->list, iter);
        found = true;
        break;
      }
    }
  } while (found);
}

static bool
proxy_list_is_empty (ProxyList  *self)
{
  g_return_val_if_fail (self, true);

  return self->list == NULL;
}

static void
proxy_list_destroy (ProxyList *self)
{
  g_return_if_fail (self);

  if (self->list) {
    do {
      ProxyData *data = (ProxyData *) self->list->data;
      proxy_data_destroy (data);
      self->list->data = NULL;
    } while (NULL != (self->list = g_list_delete_link (self->list, self->list)));
  }

  g_free (self);
}

/*
 * YtsClient
 */

static void
_tp_yts_status_advertise_status_cb (GObject       *source_object,
                                    GAsyncResult  *result,
                                    gpointer       user_data)
{
  TpYtsStatus *status = TP_YTS_STATUS (source_object);
  GError      *error = NULL;

  if (!tp_yts_status_advertise_status_finish (status, result, &error)) {
      g_critical ("Failed to advertise status: %s", error->message);
  } else {
    g_message ("Advertising of status succeeded");
  }

  g_clear_error (&error);
}

static bool
_client_status_foreach_capability_advertise_status (YtsClientStatus const *client_status,
                                                    char const            *capability,
                                                    char const            *status_xml,
                                                    YtsClient             *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  tp_yts_status_advertise_status_async (priv->tp_status,
                                        capability,
                                        priv->service_id,
                                        status_xml,
                                        NULL,
                                        _tp_yts_status_advertise_status_cb,
                                        self);

  return true;
}

static void
yts_client_cleanup_connection_resources (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  /*
   * Clean up items associated with this connection.
   */

  priv->ready    = FALSE;
  priv->prepared = FALSE;

  /*
   * Empty roster
   */
  if (priv->roster)
    yts_roster_clear (priv->roster);

  if (priv->tp_conn)
    {
      g_object_unref (priv->tp_conn);
      priv->tp_conn = NULL;
    }
}

static gboolean
yts_client_reconnect_cb (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  priv->reconnect_id = 0;

  yts_client_connect (self);

  /* one off */
  return FALSE;
}

static void
yts_client_reconnect_after (YtsClient *self, guint after_seconds)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  g_return_if_fail (YTS_IS_CLIENT (self));

  priv->reconnect = TRUE;

  priv->reconnect_id =
    g_timeout_add_seconds (after_seconds,
                           (GSourceFunc) yts_client_reconnect_cb,
                           self);
}

/*
 * Callback for #TpProxy::interface-added: we need to add the signals we
 * care for here.
 *
 * TODO -- should we not be able to connect directly to the signal bypassing
 * the unsightly TP machinery ?
 */
static void
yts_client_debug_iface_added_cb (TpProxy    *tproxy,
                                  guint       id,
                                  DBusGProxy *proxy,
                                  gpointer    data)
{
  if (id != TP_IFACE_QUARK_DEBUG)
    return;

  dbus_g_proxy_add_signal (proxy, "NewDebugMessage",
                           G_TYPE_DOUBLE,
                           G_TYPE_STRING,
                           G_TYPE_UINT,
                           G_TYPE_STRING,
                           G_TYPE_INVALID);
}

/*
 * Handler for Mgr debug output.
 */
static void
yts_client_debug_msg_cb (TpProxy    *proxy,
                          gdouble     timestamp,
                          char const *domain,
                          guint       level,
                          char const *msg,
                          gpointer    data,
                          GObject    *weak_object)
{
  char            *log_domain;
  GLogLevelFlags   log_level;

  log_domain = g_strdup_printf ("%s%c%s%c%s",
                                PACKAGE, '\0', "telepathy", '\0', domain);

  switch (level) {
    case 0:
      log_level = G_LOG_LEVEL_ERROR;
      break;
    case 1:
      log_level = G_LOG_LEVEL_CRITICAL;
      break;
    case 2:
      log_level = G_LOG_LEVEL_WARNING;
      break;
    case 3:
      log_level = G_LOG_LEVEL_MESSAGE;
      break;
    default:
      log_level = G_LOG_LEVEL_INFO;
  }

  g_log (log_domain, log_level, "%s", msg);
  g_free (log_domain);
}

/*
 * The machinery for adding the NewDebugMessage signal; this is PITA, and can
 * probably be autogenerated from somewhere, but no documentation.
 *
 * TODO - check we cannot connect directly to the dbus proxy avoiding all
 * this unsightly marshaling.
 *
 * First, the collect function
 */
static void
yts_client_debug_msg_collect (DBusGProxy              *proxy,
                               gdouble                  timestamp,
                               char const              *domain,
                               guint                    level,
                               char const              *msg,
                               TpProxySignalConnection *signal)
{
  GValueArray *args = g_value_array_new (4);
  GValue t = { 0 };

  g_value_init (&t, G_TYPE_DOUBLE);
  g_value_set_double (&t, timestamp);
  g_value_array_append (args, &t);
  g_value_unset (&t);

  g_value_init (&t, G_TYPE_STRING);
  g_value_set_string (&t, domain);
  g_value_array_append (args, &t);
  g_value_unset (&t);

  g_value_init (&t, G_TYPE_UINT);
  g_value_set_uint (&t, level);
  g_value_array_append (args, &t);
  g_value_unset (&t);

  g_value_init (&t, G_TYPE_STRING);
  g_value_set_string (&t, msg);
  g_value_array_append (args, &t);

  tp_proxy_signal_connection_v0_take_results (signal, args);
}

typedef void (*YtsClientMgrNewDebugMsg)(TpProxy *,
                                         gdouble,
                                         char const *,
                                         guint,
                                         char const *,
                                         gpointer, GObject *);

/*
 * The callback invoker
 */
static void
yts_client_debug_msg_invoke (TpProxy     *proxy,
                              GError      *error,
                              GValueArray *args,
                              GCallback    callback,
                              gpointer     data,
                              GObject     *weak_object)
{
  YtsClientMgrNewDebugMsg cb = (YtsClientMgrNewDebugMsg) callback;

  if (cb)
    {
      cb (g_object_ref (proxy),
          g_value_get_double (args->values),
          g_value_get_string (args->values + 1),
          g_value_get_uint (args->values + 2),
          g_value_get_string (args->values + 3),
          data,
          weak_object);

      g_object_unref (proxy);
    }

  g_value_array_free (args);
}

/*
 * Connects to the signal(s) and enable debugging output.
 */
static void
yts_client_connect_debug_signals (YtsClient *client, TpProxy *proxy)
{
  GError   *error = NULL;
  GValue    v = {0};
  GType     expected[] =
    {
      G_TYPE_DOUBLE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING,
      G_TYPE_INVALID
    };

  g_value_init (&v, G_TYPE_BOOLEAN);
  g_value_set_boolean (&v, TRUE);

  tp_proxy_signal_connection_v0_new (proxy,
                                     TP_IFACE_QUARK_DEBUG,
                                     "NewDebugMessage",
                                     &expected[0],
                                     G_CALLBACK (yts_client_debug_msg_collect),
                                     yts_client_debug_msg_invoke,
                                     G_CALLBACK (yts_client_debug_msg_cb),
                                     client,
                                     NULL,
                                     (GObject*)client,
                                     &error);

  if (error)
    {
      g_message ("%s", error->message);
      g_clear_error (&error);
    }

  tp_cli_dbus_properties_call_set (proxy, -1, TP_IFACE_DEBUG,
                                   "Enabled", &v, NULL, NULL, NULL, NULL);
}

static void
yts_client_setup_debug  (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  TpDBusDaemon      *dbus;
  TpProxy           *proxy;
  char              *busname;
  char const        *mgr_name = NULL;
  GError            *error = NULL;

  dbus = tp_dbus_daemon_dup (&error);

  if (error != NULL)
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
      return;
    }

  switch (priv->protocol) {
    case YTS_PROTOCOL_XMPP:
      mgr_name = "gabble";
      break;
    case YTS_PROTOCOL_LOCAL_XMPP:
      mgr_name = "salut";
      break;
  }

  busname = g_strdup_printf ("org.freedesktop.Telepathy.ConnectionManager.%s",
                             mgr_name);
  proxy =
    g_object_new (TP_TYPE_PROXY,
                  "bus-name", busname,
                  "dbus-daemon", dbus,
                  "object-path", "/org/freedesktop/Telepathy/debug",
                  NULL);

  priv->tp_debug_proxy = proxy;

  g_signal_connect (proxy, "interface-added",
                    G_CALLBACK (yts_client_debug_iface_added_cb), self);

  tp_proxy_add_interface_by_id (proxy, TP_IFACE_QUARK_DEBUG);

  /*
   * Connecting to the signals triggers the interface-added signal
   */
  yts_client_connect_debug_signals (self, proxy);

  g_object_unref (dbus);
  g_free (busname);
}

static char const *
extract_remote_service_id (YtsClient   *self,
                           GHashTable  *channel_properties)
{
    GHashTable *metadata;
    char const *remote_service_id = NULL;

    metadata = tp_asv_get_boxed (channel_properties,
                                 TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_METADATA,
                                 TP_HASH_TYPE_METADATA);

    if (metadata) {
      char **values = g_hash_table_lookup (metadata, (gpointer) "FromService");
      if (values && values[0]) {
        remote_service_id = values[0];
      }
    }

  return remote_service_id;
}

static void
_file_handler_handle_channels (TpSimpleHandler          *handler,
                               TpAccount                *account,
                               TpConnection             *connection,
                               GList                    *channels,
                               GList                    *requests_satisfied,
                               gint64                    user_action_time,
                               TpHandleChannelsContext  *context,
                               void                     *data)
{
  YtsClient         *self = YTS_CLIENT (data);
  YtsClientPrivate  *priv = GET_PRIVATE (self);
  GList             *iter;

  for (iter = channels;
       iter && TP_IS_FILE_TRANSFER_CHANNEL (iter->data);
       iter = iter->next) {

    TpFileTransferChannel *channel = iter->data;
    char const *remote_contact_id = tp_channel_get_initiator_identifier (TP_CHANNEL (channel));
    GHashTable *props = tp_channel_borrow_immutable_properties (TP_CHANNEL (channel));
    char const *remote_service_id;
    YtsService *service = NULL;

    remote_service_id = extract_remote_service_id (self, props);
    service = yts_roster_find_service_by_id (priv->roster,
                                             remote_contact_id,
                                             remote_service_id);
    if (service) {

      YtsIncomingFile *incoming = yts_incoming_file_new (channel);
      GError *error = NULL;

      if (g_initable_init (G_INITABLE (incoming), NULL, &error)) {

        g_signal_emit (self, signals[INCOMING_FILE], 0,
                       service, props, incoming);

      } else {

        // TODO if (error) {}
        g_critical ("Handling incoming file failed -- no handler");
      }

      g_clear_error (&error);
      g_object_unref (incoming);

    } else {
      // TODO if (error) {}
      g_critical ("Handling incoming file failed -- no recipient service");
    }
  }

  tp_handle_channels_context_accept (context);
}

static bool
_client_status_foreach_interest_add (YtsClientStatus const  *client_status,
                                     char const             *capability,
                                     YtsClient              *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  tp_yts_client_add_interest (priv->tp_client, capability);

  return true;
}

static bool
_client_status_foreach_capability_add (YtsClientStatus const  *client_status,
                                       char const             *capability,
                                       char const             *status_xml,
                                       YtsClient              *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  tp_yts_client_add_capability (priv->tp_client, capability);

  return true;
}

static void
setup_tp_client (YtsClient  *self,
                 TpAccount  *account)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  GHashTable  *filter;
  GError      *error = NULL;

  g_return_if_fail (TP_IS_ACCOUNT (account));

  priv->tp_account = account;
  priv->tp_client = tp_yts_client_new (priv->service_id, account);

  if (YTS_DEBUG_TELEPATHY & ytstenut_get_debug_flags ()) {
    yts_client_setup_debug (self);
  }

  /* Incoming file machinery. */
  priv->tp_file_handler = tp_simple_handler_new_with_factory (
                            tp_proxy_get_factory (priv->tp_am),
                            TRUE, FALSE, priv->service_id,
                            TRUE, _file_handler_handle_channels, self, NULL);

  filter = tp_asv_new (
                TP_PROP_CHANNEL_CHANNEL_TYPE,
                G_TYPE_STRING,
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER,

                TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
                G_TYPE_UINT,
                TP_HANDLE_TYPE_CONTACT,

                TP_PROP_CHANNEL_REQUESTED,
                G_TYPE_BOOLEAN,
                FALSE,

                TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_SERVICE_NAME,
                G_TYPE_STRING,
                priv->service_id,

                NULL);

  tp_base_client_take_handler_filter (priv->tp_file_handler, filter);
  tp_base_client_register (priv->tp_file_handler, &error);
  if (error) {
    g_critical ("%s", error->message);
    g_clear_error (&error);
  }

  /* Publish capabilities */
  yts_client_status_foreach_capability (
    priv->client_status,
    (YtsClientStatusCapabilityIterator) _client_status_foreach_capability_add,
    self);

  /* Publish interests */
  yts_client_status_foreach_interest (
    priv->client_status,
    (YtsClientStatusInterestIterator) _client_status_foreach_interest_add,
    self);

  /*
   * If connection has been requested already, make one
   */
  if (priv->connect)
    yts_client_make_connection (self);
}

/*
 * Callback from the async tp_proxy_prepare_async() call
 *
 * This function is ready for the New World Order according to Ytstenut ...
 */
static void
yts_client_account_prepared_cb (GObject       *source_object,
                                GAsyncResult  *res,
                                gpointer       self)
{
  TpAccount *account = TP_ACCOUNT (source_object);
  GError    *error   = NULL;

  if (!tp_proxy_prepare_finish (account, res, &error)) {
    g_critical ("Account unprepared: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_message ("Account successfully opened");

  setup_tp_client (YTS_CLIENT (self), account);
}

static void
yts_client_account_cb (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      self)
{
  TpYtsAccountManager *yts_am = TP_YTS_ACCOUNT_MANAGER (source_object);
  TpAccount           *account;
  GError              *error = NULL;
  const GQuark         features[] = { TP_ACCOUNT_FEATURE_CORE, 0 };

  g_return_if_fail (TP_IS_YTS_ACCOUNT_MANAGER (yts_am));

  account = tp_yts_account_manager_get_account_finish (yts_am,
                                                       res,
                                                       &error);
  if (error) {
    g_critical ("Could not access account: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_message ("Got account");

  tp_proxy_prepare_async (account,
                          features,
                          yts_client_account_prepared_cb,
                          self);
}

static void
_roster_send_message (YtsRoster    *roster,
                      YtsContact   *contact,
                      YtsService   *service,
                      YtsMetadata  *message,
                      YtsClient    *self)
{
  char const *service_id;

  service_id = yts_service_get_id (service);

  yts_client_send_message (self, contact, service_id, message);
}

static YtsOutgoingFile *
_roster_send_file (YtsRoster   *roster,
                   YtsContact  *contact,
                   YtsService  *service,
                   GFile       *file,
                   char const  *description,
                   GError     **error_out,
                   YtsClient   *self)
{
  YtsClientPrivate  *priv = GET_PRIVATE (self);
  YtsOutgoingFile   *outgoing;
  char const        *recipient_contact_id;
  char const        *recipient_service_id;
  GError            *error = NULL;

  g_return_val_if_fail (YTS_IS_CLIENT (self), NULL);

  recipient_contact_id = yts_contact_get_id (contact);
  recipient_service_id = yts_service_get_id (service);
  outgoing = yts_outgoing_file_new (priv->tp_account,
                                    file,
                                    priv->service_id,
                                    recipient_contact_id,
                                    recipient_service_id,
                                    description);

  g_initable_init (G_INITABLE (outgoing), NULL, &error);
  if (error) {
    g_object_unref (outgoing);
    g_propagate_error (error_out, error);
    g_clear_error (&error);
    return NULL;
  }

  return outgoing;
}

static void
_roster_contact_removed (YtsRoster  *roster,
                         YtsContact *contact,
                         YtsClient  *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  GHashTableIter   iter;
  bool             start_over;

  /*
   * Clear pending responses.
   */

  // FIXME this would be better solved using g_hash_table_foreach_remove().
  do {
    char const *invocation_id;
    InvocationData *data;
    start_over = false;
    g_hash_table_iter_init (&iter, priv->invocations);
    while (g_hash_table_iter_next (&iter,
                                   (void **) &invocation_id,
                                   (void **) &data)) {

      if (data->contact == contact) {
        g_hash_table_remove (priv->invocations, invocation_id);
        start_over = true;
        break;
      }
    }
  } while (start_over);

  /*
   * Unregister proxies
   */

  // FIXME this would be better solved using g_hash_table_foreach_remove().
  do {
    char const *capability;
    ProxyList *proxy_list;
    start_over = false;
    g_hash_table_iter_init (&iter, priv->proxies);
    while (g_hash_table_iter_next (&iter,
                                   (void **) &capability,
                                   (void **) &proxy_list)) {

      proxy_list_purge_contact (proxy_list, contact);
      if (proxy_list_is_empty (proxy_list)) {
        g_hash_table_remove (priv->proxies, capability);
        start_over = true;
        break;
      }
    }
  } while (start_over);
}

/*
 * C2S Setup
 */

static void
_account_prepared (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  YtsClient *self = YTS_CLIENT (user_data);
  TpAccount *account = TP_ACCOUNT (source_object);
  GError    *error = NULL;

  if (!tp_proxy_prepare_finish (account, result, &error)) {
    g_critical ("Failed to prepare account: %s\n", error->message);
    g_clear_error (&error);
    return;
  }

  setup_tp_client (self, account);
}

/**/

static void
yts_client_constructed (GObject *object)
{
  YtsClientPrivate *priv = GET_PRIVATE (object);
  GError              *error     = NULL;

  if (G_OBJECT_CLASS (yts_client_parent_class)->constructed)
    G_OBJECT_CLASS (yts_client_parent_class)->constructed (object);

  priv->roster   = yts_roster_impl_new ();
  g_signal_connect (priv->roster, "send-message",
                    G_CALLBACK (_roster_send_message), object);
  g_signal_connect (priv->roster, "send-file",
                    G_CALLBACK (_roster_send_file), object);
  g_signal_connect (priv->roster, "contact-removed",
                    G_CALLBACK (_roster_contact_removed), object);

  priv->unwanted = yts_roster_impl_new ();
#if 0 /* TODO */
  g_signal_connect (priv->unwanted, "send-message",
                    G_CALLBACK (_roster_send_message), object);
  g_signal_connect (priv->roster, "send-file",
                    G_CALLBACK (_roster_send_file), object);
  g_signal_connect (priv->unwanted, "contact-removed",
                    G_CALLBACK (_roster_contact_removed), object);
#endif

  if (!priv->service_id || !*priv->service_id) {
    g_critical ("Service-ID must be set at construction time.");
    return;
  }

  priv->client_status = yts_client_status_new (priv->service_id);

  priv->tp_am = tp_yts_account_manager_dup ();
  if (!TP_IS_YTS_ACCOUNT_MANAGER (priv->tp_am)) {
    g_error ("Missing Account Manager");
    return;
  }
  tp_yts_account_manager_hold (priv->tp_am);

  if (priv->protocol == YTS_PROTOCOL_LOCAL_XMPP) {
    tp_yts_account_manager_get_account_async (priv->tp_am, NULL,
                                              yts_client_account_cb,
                                              object);
  } else {

    TpAccount *account;
    char      *escaped_account_id;
    char      *path;

    if (NULL == priv->account_id) {
      g_critical ("Missing account ID");
      return;
    }

    /* TODO iterate account manager to find matching account, rather than
     * relying on escaping and path -- those are not guaranteed to stay
     * compatible. */

    escaped_account_id = tp_escape_as_identifier (priv->account_id);
    path = g_strdup_printf ("%s%s%s",
                            TP_ACCOUNT_OBJECT_PATH_BASE,
                            "gabble/jabber/",
                            escaped_account_id);

    g_message ("account path: %s", path);

    account = tp_yts_account_manager_ensure_account (priv->tp_am, path, &error);
    if (error) {
      g_critical ("Could not access account %s: %s",
                  priv->account_id,
                  error->message);
      g_clear_error (&error);
      return;
    }

    tp_proxy_prepare_async (account, NULL, _account_prepared, object);

    g_free (path);
    g_free (escaped_account_id);
  }
}

static void
yts_client_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  YtsClientPrivate *priv = GET_PRIVATE (object);

  switch (property_id)
    {
    case PROP_ACCOUNT_ID:
      g_value_set_string (value, priv->account_id);
      break;
    case PROP_CONTACT_ID:
      g_value_set_string (value,
                          yts_client_get_contact_id (YTS_CLIENT (object)));
      break;
    case PROP_SERVICE_ID:
      g_value_set_string (value, priv->service_id);
      break;
    case PROP_PROTOCOL:
      g_value_set_enum (value, priv->protocol);
      break;
    case PROP_TP_ACCOUNT:
      g_value_set_object (value, priv->tp_account);
      break;
    case PROP_TP_STATUS:
      g_value_set_object (value, priv->tp_status);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
yts_client_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  YtsClientPrivate *priv = GET_PRIVATE (object);

  switch (property_id)
    {
    case PROP_ACCOUNT_ID:
      /* Construct-only */
      priv->account_id = g_value_dup_string (value);
      break;
    case PROP_SERVICE_ID:
      {
        /* Construct-only */
        g_return_if_fail (NULL == priv->service_id);
        priv->service_id = g_value_dup_string (value);
      }
      break;
    case PROP_PROTOCOL:
      priv->protocol = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
yts_client_dispose (GObject *object)
{
  YtsClientPrivate *priv = GET_PRIVATE (object);

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  if (priv->roster)
    {
      g_object_unref (priv->roster);
      priv->roster = NULL;
    }

  if (priv->unwanted)
    {
      g_object_unref (priv->unwanted);
      priv->unwanted = NULL;
    }

  if (priv->tp_file_handler)
    {
      tp_base_client_unregister (priv->tp_file_handler);
      g_object_unref (priv->tp_file_handler);
      priv->tp_file_handler = NULL;
    }

  if (priv->tp_conn)
    {
      tp_cli_connection_call_disconnect  (priv->tp_conn,
                                          -1,
                                          NULL, NULL, NULL, NULL);
      g_object_unref (priv->tp_conn);
      priv->tp_conn = NULL;
    }

  if (priv->tp_am)
    {
      tp_yts_account_manager_release (priv->tp_am);

      g_object_unref (priv->tp_am);
      priv->tp_am = NULL;
    }

  if (priv->tp_debug_proxy)
    {
      g_object_unref (priv->tp_debug_proxy);
      priv->tp_debug_proxy = NULL;
    }

  if (priv->services)
    {
      g_hash_table_destroy (priv->services);
      priv->services = NULL;
    }

  if (priv->invocations)
    {
      g_hash_table_destroy (priv->invocations);
      priv->invocations = NULL;
    }

  if (priv->proxies)
    {
      g_hash_table_destroy (priv->proxies);
      priv->proxies = NULL;
    }

  G_OBJECT_CLASS (yts_client_parent_class)->dispose (object);
}

static void
yts_client_finalize (GObject *object)
{
  YtsClientPrivate *priv = GET_PRIVATE (object);

  g_free (priv->account_id);
  g_free (priv->service_id);
  g_object_unref (priv->client_status);

  G_OBJECT_CLASS (yts_client_parent_class)->finalize (object);
}

static void
yts_client_class_init (YtsClientClass *klass)
{
  GParamSpec   *pspec;
  GObjectClass *object_class = (GObjectClass *)klass;

  /* Initialize logging. */
  static bool is_initialized = false;
  if (!is_initialized) {
    ytstenut_init ();
    is_initialized = true;
  }

  g_type_class_add_private (klass, sizeof (YtsClientPrivate));

  object_class->dispose      = yts_client_dispose;
  object_class->finalize     = yts_client_finalize;
  object_class->constructed  = yts_client_constructed;
  object_class->get_property = yts_client_get_property;
  object_class->set_property = yts_client_set_property;

  /**
   * YtsClient:account-id:
   *
   * The account ID used by this client instance when running in C2S
   * (client-to-server) mode. This is the non-normalized JID as passed when
   * the client was instantiated. Might be %NULL when in P2P mode.
   *
   * Since: 0.4
   */
  pspec = g_param_spec_string ("account-id", "", "",
                               NULL,
                               G_PARAM_READWRITE |
                               G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_ACCOUNT_ID, pspec);

  /**
   * YtsClient:contact-id:
   *
   * The contact ID of this service.
   *
   * Since: 0.4
   */
  pspec = g_param_spec_string ("contact-id", "", "",
                               NULL,
                               G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_CONTACT_ID, pspec);

  /**
   * YtsClient:service-id:
   *
   * The unique ID of this service.
   *
   * Since: 0.4
   */
  pspec = g_param_spec_string ("service-id", "", "",
                               NULL,
                               G_PARAM_READWRITE |
                               G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_SERVICE_ID, pspec);

  /**
   * YtsClient:protocol:
   *
   * XMPP protocol to use for connection.
   *
   * Since: 0.1
   */
  pspec = g_param_spec_enum ("protocol",
                             "Protocol",
                             "Protocol",
                             YTS_TYPE_PROTOCOL,
                             0,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_PROTOCOL, pspec);

  /**
   * YtsClient:tp-account:
   *
   * Telepathies #TpAccount object used by this instance.
   * <note>There is no API guarantee for this and other fields that expose telepathy.</note>
   *
   * Since: 0.4
   */
  pspec = g_param_spec_object ("tp-account", "", "",
                               TP_TYPE_ACCOUNT,
                               G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_TP_ACCOUNT, pspec);

  /**
   * YtsClient:tp-status:
   *
   * Telepathies #TpYtsStatus object used by this instance.
   * <note>There is no API guarantee for this and other fields that expose telepathy.</note>
   *
   * Since: 0.4
   */
  pspec = g_param_spec_object ("tp-status", "", "",
                               TP_TYPE_YTS_STATUS,
                               G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_TP_STATUS, pspec);

  /**
   * YtsClient::authenticated:
   * @self: object which emitted the signal.
   *
   * The authenticated signal is emited when connection to the Ytstenut server
   * is successfully established.
   *
   * Since: 0.1
   */
  signals[AUTHENTICATED] =
    g_signal_new ("authenticated",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  yts_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  /**
   * YtsClient::ready:
   * @self: object which emitted the signal.
   *
   * The ready signal is emited when the initial Telepathy set up is ready.
   * (In practical terms this means the subscription channels are prepared.)
   *
   * Since: 0.1
   */
  signals[READY] =
    g_signal_new ("ready",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  yts_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  /**
   * YtsClient::disconnected:
   * @self: object which emitted the signal.
   *
   * The disconnected signal is emited when connection to the Ytstenut server
   * is successfully established.
   *
   * Since: 0.1
   */
  signals[DISCONNECTED] =
    g_signal_new ("disconnected",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  yts_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  /**
   * YtsClient::raw-message:
   * @self: object which emitted the signal.
   * @message: the message text.
   *
   * The message signal is emitted when message is received from one of the
   * contacts.
   *
   * Since: 0.3
   */
  signals[RAW_MESSAGE] =
    g_signal_new ("raw-message",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  yts_marshal_VOID__STRING,
                  G_TYPE_NONE, 1,
                  G_TYPE_STRING);

  /**
   * YtsClient::text-message:
   * @self: object which emitted the signal.
   * @text: Message payload.
   *
   * This signal is emitted when a remote service sent a text message.
   *
   * Since: 0.3
   */
  signals[TEXT_MESSAGE] =
    g_signal_new ("text-message",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  yts_marshal_VOID__STRING,
                  G_TYPE_NONE, 1,
                  G_TYPE_STRING);

  /**
   * YtsClient::list-message:
   * @self: object which emitted the signal.
   * @list: %NULL-terminated string vector holding the message content.
   *
   * This signal is emitted when a remote service sent a list of strings.
   *
   * Since: 0.3
   */
  signals[LIST_MESSAGE] =
    g_signal_new ("list-message",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  yts_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1,
                  G_TYPE_STRV);

  /**
   * YtsClient::dictionary-message:
   * @self: object which emitted the signal.
   * @dictionary: %NULL-terminated string vector where even indices are keys and
   *              odd ones are values.
   *
   * This signal is emitted when a remote service sent a dictionary message.
   *
   * Since: 0.3
   */
  signals[DICTIONARY_MESSAGE] =
    g_signal_new ("dictionary-message",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  yts_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1,
                  G_TYPE_STRV);

  /**
   * YtsClient::error:
   * @self: object which emitted the signal.
   * @error: error code
   *
   * The error signal is emitted to indicate an error (or eventual success)
   *
   * Since: 0.1
   */
  signals[ERROR] =
    g_signal_new ("error",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  yts_marshal_VOID__UINT,
                  G_TYPE_NONE, 1,
                  G_TYPE_UINT);

  /**
   * YtsClient::incoming-file:
   * @self: object which emitted the signal.
   * @service: #YtsService sending the file.
   * @properties: an a{sv} #GHashTable containing file properties, see telepathy channel properties.
   * @incoming: the #YtsIncomingFile that is being sent.
   *
   * The #YtsClient::incoming-file signal is emitted when the client receives
   * incoming request for a file transfer. To accept the file, the signal
   * handler needs to call #yts_incoming_file_accept(), otherwise the transfer
   * will be cancelled.
   *
   * Since: 0.1
   */
  signals[INCOMING_FILE] =
    g_signal_new ("incoming-file",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  yts_marshal_VOID__OBJECT_BOXED_OBJECT,
                  G_TYPE_NONE, 3,
                  YTS_TYPE_SERVICE,
                  G_TYPE_HASH_TABLE,
                  YTS_TYPE_INCOMING_FILE);
}

static void
yts_client_init (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  priv->services = g_hash_table_new_full (g_str_hash,
                                          g_str_equal,
                                          g_free,
                                          g_object_unref);

  priv->invocations = g_hash_table_new_full (g_str_hash,
                                             g_str_equal,
                                             g_free,
                                             (GDestroyNotify) invocation_data_destroy);

  priv->proxies = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         (GDestroyNotify) proxy_list_destroy);
}

YtsClient *
yts_client_new_c2s (char const *account_id,
                    char const *service_id)
{
  g_return_val_if_fail (account_id, NULL);
  g_return_val_if_fail (service_id, NULL);

  return g_object_new (YTS_TYPE_CLIENT,
                       "protocol",    YTS_PROTOCOL_XMPP,
                       "account-id",  account_id,
                       "service-id",  service_id,
                       NULL);
}

/**
 * yts_client_new_p2p:
 * @service_id: Unique ID for this service; UIDs must follow the dbus
 *              convention for unique names.
 *
 * Creates a new #YtsClient object.
 *
 * Returns: (transfer full): a #YtsClient object.
 *
 * Since: 0.1
 */
YtsClient *
yts_client_new_p2p (char const *service_id)
{
  g_return_val_if_fail (service_id, NULL);

  return g_object_new (YTS_TYPE_CLIENT,
                       "protocol",    YTS_PROTOCOL_LOCAL_XMPP,
                       "service-id",  service_id,
                       NULL);
}

static GVariant *
variant_new_from_escaped_literal (char const *string)
{
  GVariant  *v;
  char      *unescaped;

  unescaped = g_uri_unescape_string (string, NULL);
  v = g_variant_new_parsed (unescaped);
  g_free (unescaped);

  return v;
}

static gboolean
dispatch_to_service (YtsClient  *self,
                     char const *sender_contact_id,
                     char const *xml)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  RestXmlParser *parser;
  RestXmlNode   *node;
  char const    *proxy_id;
  char const    *capability;
  char const    *type;
  YtsContact   *contact;
  gboolean       dispatched = FALSE;

  parser = rest_xml_parser_new ();
  node = rest_xml_parser_parse_from_data (parser, xml, strlen (xml));
  if (NULL == node) {
    // FIXME report error
    g_critical ("%s : Failed to parse message '%s'", G_STRLOC, xml);
    return false;
  }

  proxy_id = rest_xml_node_get_attr (node, "from-service");
  if (NULL == proxy_id) {
    // FIXME report error
    g_critical ("%s : Malformed message, 'from-service' missing in '%s'",
                G_STRLOC,
                xml);
    return false;
  }

  capability = rest_xml_node_get_attr (node, "capability");
  if (NULL == capability) {
    // FIXME report error
    g_critical ("%s : Malformed message, 'capability' missing in '%s'",
                G_STRLOC,
                xml);
    return false;
  }

  type = rest_xml_node_get_attr (node, "type");
  if (NULL == type) {
    // FIXME report error
    g_critical ("%s : Malformed message, 'type' missing in '%s'",
                G_STRLOC,
                xml);
    return false;
  }

  contact = yts_roster_find_contact_by_id (priv->roster, sender_contact_id);
  if (NULL == contact) {
    // FIXME report error
    g_critical ("%s : Contact for '%s' not found",
                G_STRLOC,
                sender_contact_id);
    return false;
  }

  /*
   * Low-level interface
   */

  if (0 == g_strcmp0 (SERVICE_FQC_ID, capability) &&
      0 == g_strcmp0 ("text", type))
    {
      char const *escaped_payload = rest_xml_node_get_attr (node, "payload");
      GVariant *payload = escaped_payload ?
                            variant_new_from_escaped_literal (escaped_payload) :
                            NULL;
      if (payload)
        {
          char const *text = g_variant_get_string (payload, NULL);
          g_signal_emit (self, signals[TEXT_MESSAGE], 0, text);
          g_variant_unref (payload);
        }
      else
        {
          // FIXME report
          g_warning ("%s : Message empty", G_STRLOC);
        }
    }
  else if (0 == g_strcmp0 (SERVICE_FQC_ID, capability) &&
           0 == g_strcmp0 ("list", type))
    {
      char const *escaped_payload = rest_xml_node_get_attr (node, "payload");
      GVariant *payload = escaped_payload ?
                            variant_new_from_escaped_literal (escaped_payload) :
                            NULL;
      if (payload)
        {
          char const **list = g_variant_get_strv (payload, NULL);
          g_signal_emit (self, signals[LIST_MESSAGE], 0, list);
          g_free (list);
          g_variant_unref (payload);
        }
      else
        {
          // FIXME report
          g_warning ("%s : Message empty", G_STRLOC);
        }
    }
  else if (0 == g_strcmp0 (SERVICE_FQC_ID, capability) &&
           0 == g_strcmp0 ("dictionary", type))
    {
      char const *escaped_payload = rest_xml_node_get_attr (node, "payload");
      GVariant *payload = escaped_payload ?
                            variant_new_from_escaped_literal (escaped_payload) :
                            NULL;
      if (payload)
        {
          GVariantIter iter;
          char const *name;
          char const *value;
          size_t n_entries;
          if (0 < (n_entries = g_variant_iter_init (&iter, payload)))
            {
              char **dictionary = g_new0 (char *, n_entries * 2 + 1);
              unsigned i = 0;
              while (g_variant_iter_loop (&iter, "{ss}", &name, &value))
                {
                  dictionary[i++] = g_strdup (name);
                  dictionary[i++] = g_strdup (value);
                }
              dictionary[i] = NULL;
              g_signal_emit (self, signals[DICTIONARY_MESSAGE], 0, dictionary);
              g_strfreev (dictionary);
            }
          g_variant_unref (payload);
        }
      else
        {
          // FIXME report
          g_warning ("%s : Message empty", G_STRLOC);
        }
    }

  /*
   * High-level interface
   */

  else if (0 == g_strcmp0 ("invocation", type))
    {
      /* Deliver to service */
      YtsServiceAdapter *adapter = g_hash_table_lookup (priv->services,
                                                         capability);
      if (adapter)
        {
          bool keep_sae;
          char const *invocation_id = rest_xml_node_get_attr (node, "invocation");
          char const *aspect = rest_xml_node_get_attr (node, "aspect");
          char const *args = rest_xml_node_get_attr (node, "arguments");
          GVariant *arguments = args ? variant_new_from_escaped_literal (args) : NULL;

          // FIXME check return value
          client_establish_invocation (self,
                                       invocation_id,
                                       contact,
                                       proxy_id);
          keep_sae = yts_service_adapter_invoke (adapter,
                                                  invocation_id,
                                                  aspect,
                                                  arguments);
          if (!keep_sae) {
            client_conclude_invocation (self, invocation_id);
          }

          dispatched = TRUE;
        }
      else
        {
          // FIXME we should probably report back that there's no adapter?
        }
    }
  else if (0 == g_strcmp0 ("event", type))
    {
      char const *aspect = rest_xml_node_get_attr (node, "aspect");
      char const *args = rest_xml_node_get_attr (node, "arguments");
      GVariant *arguments = args ? variant_new_from_escaped_literal (args) : NULL;

      dispatched = yts_contact_dispatch_event (contact,
                                                capability,
                                                aspect,
                                                arguments);
    }
  else if (0 == g_strcmp0 ("response", type))
    {
      char const *invocation_id = rest_xml_node_get_attr (node, "invocation");
      char const *ret = rest_xml_node_get_attr (node, "response");
      GVariant *response = ret ? variant_new_from_escaped_literal (ret) : NULL;

      dispatched = yts_contact_dispatch_response (contact,
                                                   capability,
                                                   invocation_id,
                                                   response);
    }
  else
    {
      // FIXME report error
      g_critical ("%s : Unknown message type '%s'", G_STRLOC, type);
    }

  g_object_unref (parser);
  return dispatched;
}

static void
yts_client_yts_channels_received_cb (TpYtsClient *tp_client,
                                      YtsClient  *client)
{
  TpYtsChannel  *ch;

  while ((ch = tp_yts_client_accept_channel (tp_client)))
    {
      char const      *from;
      GHashTable      *props;
      GHashTableIter   iter;
      gpointer         key, value;

      from = tp_channel_get_initiator_identifier (TP_CHANNEL (ch));

      g_object_get (ch, "channel-properties", &props, NULL);
      g_assert (props);

      g_hash_table_iter_init (&iter, props);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          GValue      *v = value;
          char        *k = key;

          if (!g_strcmp0 (k, "org.freedesktop.ytstenut.xpmn.Channel.RequestBody"))
            {
              char const *xml_payload = g_value_get_string (v);
              gboolean dispatched = dispatch_to_service (client,
                                                         from,
                                                         xml_payload);

              // FIXME this should probably be emitted anyway, for consistency.
              if (!dispatched)
                {
                  g_signal_emit (client, signals[RAW_MESSAGE], 0, xml_payload);
                }
            }
        }
    }
}

/**
 * yts_client_disconnect:
 * @self: object on which to invoke this method.
 *
 * Disconnects @self.
 *
 * Since: 0.1
 */
void
yts_client_disconnect (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  g_return_if_fail (YTS_IS_CLIENT (self));

  /* cancel any pending reconnect timeout */
  if (priv->reconnect_id)
    {
      g_source_remove (priv->reconnect_id);
      priv->reconnect_id = 0;
    }

  /* clear flag indicating pending connect */
  priv->connect = FALSE;

  /*
   * Since this was a disconnect at our end, clear the reconnect flag,
   * to avoid the signal closure from installing a reconnect callback.
   */
  priv->reconnect = FALSE;

  if (priv->tp_conn)
    tp_cli_connection_call_disconnect  (priv->tp_conn,
                                        -1, NULL, NULL, NULL, NULL);
}

static void
yts_client_connected_cb (TpConnection   *proxy,
                          const GError  *error,
                          YtsClient     *self,
                          GObject       *weak_object)
{
  if (error)
    {
      g_warning (G_STRLOC ": %s: %s", __FUNCTION__, error->message);

      yts_client_disconnect (self);
      yts_client_reconnect_after (self, RECONNECT_DELAY);
    }
}

static void
yts_client_error_cb (TpConnection *proxy,
                      char const   *arg_Error,
                      GHashTable   *arg_Details,
                      gpointer      user_data,
                      GObject      *weak_object)
{
  g_message ("Error: %s", arg_Error);
}

static void
yts_client_status_cb (TpConnection  *proxy,
                       guint         arg_Status,
                       guint         arg_Reason,
                       YtsClient    *self,
                       GObject      *weak_object)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  char const *status[] = {"'connected'   ",
                          "'connecting'  ",
                          "'disconnected'"};
  char const *reason[] =
    {
      "NONE_SPECIFIED",
      "REQUESTED",
      "NETWORK_ERROR",
      "AUTHENTICATION_FAILED",
      "ENCRYPTION_ERROR",
      "NAME_IN_USE",
      "CERT_NOT_PROVIDED",
      "CERT_UNTRUSTED",
      "CERT_EXPIRED",
      "CERT_NOT_ACTIVATED",
      "CERT_HOSTNAME_MISMATCH",
      "CERT_FINGERPRINT_MISMATCH",
      "CERT_SELF_SIGNED",
      "CERT_OTHER_ERROR"
    };

  if (priv->disposed)
    return;

  g_message ("Connection: %s: '%s'",
           status[arg_Status], reason[arg_Reason]);

  if (arg_Status == TP_CONNECTION_STATUS_CONNECTED)
    {
      g_signal_emit (self, signals[AUTHENTICATED], 0);
    }
  else if (arg_Status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      yts_client_cleanup_connection_resources (self);

      if (priv->reconnect)
        yts_client_reconnect_after (self, RECONNECT_DELAY);

      g_signal_emit (self, signals[DISCONNECTED], 0);
    }
}

static gboolean
yts_client_process_one_service (YtsClient         *self,
                                char const        *contact_id,
                                char const        *service_id,
                                const GValueArray *service_info)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  char const        *type;
  GHashTable        *names;
  char             **caps;
  GHashTable        *service_statuses;

  if (service_info->n_values != 3)
    {
      g_warning ("Missformed service description (nvalues == %d)",
                 service_info->n_values);
      return FALSE;
    }

  g_message ("Processing service %s:%s", contact_id, service_id);

  type  = g_value_get_string (&service_info->values[0]);
  names = g_value_get_boxed (&service_info->values[1]);
  caps  = g_value_get_boxed (&service_info->values[2]);

  service_statuses = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            g_free,
                                            g_free);
  if (priv->tp_status) {
    GHashTable *discovered_statuses = tp_yts_status_get_discovered_statuses (
                                                              priv->tp_status);
    if (discovered_statuses) {
      GHashTable *contact_statuses = g_hash_table_lookup (discovered_statuses,
                                                          contact_id);
      if (contact_statuses) {
        unsigned i;
        for (i = 0; caps && caps[i]; i++) {
          GHashTable *capability_statuses = g_hash_table_lookup (contact_statuses,
                                                                 caps[i]);
          if (capability_statuses) {
            char const *status_xml = g_hash_table_lookup (capability_statuses,
                                                          service_id);
            if (status_xml) {
              g_hash_table_insert (service_statuses,
                                   g_strdup (caps[i]),
                                   g_strdup (status_xml));
            }
          }
        }
      }
    }
  }

  yts_roster_add_service (priv->roster,
                          priv->tp_conn,
                          contact_id,
                          service_id,
                          type,
                          (char const **)caps,
                          names,
                          service_statuses);

  g_hash_table_unref (service_statuses);

  return TRUE;
}

static void
yts_client_service_added_cb (TpYtsStatus        *tp_status,
                             char const         *contact_id,
                             char const         *service_id,
                             const GValueArray  *service_info,
                             YtsClient          *self)
{
  yts_client_process_one_service (self, contact_id, service_id, service_info);
}

static void
yts_client_service_removed_cb (TpYtsStatus  *tp_status,
                               char const   *contact_id,
                               char const   *service_id,
                               YtsClient    *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  GHashTableIter   iter;
  bool             start_over;

  yts_roster_remove_service_by_id (priv->roster, contact_id, service_id);

  /*
   * Clear pending responses.
   */

  // FIXME this would be better solved using g_hash_table_foreach_remove().
  do {
    char const *invocation_id;
    InvocationData *data;
    start_over = false;
    g_hash_table_iter_init (&iter, priv->invocations);
    while (g_hash_table_iter_next (&iter,
                                   (void **) &invocation_id,
                                   (void **) &data)) {

      if (0 == g_strcmp0 (data->proxy_id, service_id)) {
        g_hash_table_remove (priv->invocations, invocation_id);
        start_over = true;
        break;
      }
    }
  } while (start_over);

  /*
   * Unregister proxies
   */

  // FIXME this would be better solved using g_hash_table_foreach_remove().
  do {
    char const *capability;
    ProxyList *proxy_list;
    start_over = false;
    g_hash_table_iter_init (&iter, priv->proxies);
    while (g_hash_table_iter_next (&iter,
                                   (void **) &capability,
                                   (void **) &proxy_list)) {

      proxy_list_purge_proxy_id (proxy_list, service_id);
      if (proxy_list_is_empty (proxy_list)) {
        g_hash_table_remove (priv->proxies, capability);
        start_over = true;
        break;
      }
    }
  } while (start_over);
}

static void
yts_client_process_status (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  GHashTable        *services;

  if ((services = tp_yts_status_get_discovered_services (priv->tp_status)))
    {
      char           *contact_id;
      GHashTable     *service;
      GHashTableIter  iter;

      if (g_hash_table_size (services) <= 0)
        g_message ("No services discovered so far");

      g_hash_table_iter_init (&iter, services);
      while (g_hash_table_iter_next (&iter,
                                     (void **) &contact_id,
                                     (void **) &service))
        {
          char           *service_id;
          GValueArray    *service_info;
          GHashTableIter  iter2;

          g_hash_table_iter_init (&iter2, service);
          while (g_hash_table_iter_next (&iter2,
                                         (void **) &service_id,
                                         (void **) &service_info))
            {
              yts_client_process_one_service (self,
                                              contact_id,
                                              service_id,
                                              service_info);
            }
        }
    }
  else
    g_message ("No discovered services");
}

static void
_tp_yts_status_changed (TpYtsStatus *tp_status,
                        char const  *contact_id,
                        char const  *fqc_id,
                        char const  *service_id,
                        char const  *status_xml,
                        YtsClient   *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  yts_roster_update_contact_status (priv->roster,
                                    contact_id,
                                    service_id,
                                    fqc_id,
                                    status_xml);
}

static void
yts_client_yts_status_cb (GObject       *obj,
                          GAsyncResult  *res,
                          YtsClient     *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  TpAccount         *acc    = TP_ACCOUNT (obj);
  GError            *error  = NULL;
  TpYtsStatus       *tp_status;

  if (!(tp_status = tp_yts_status_ensure_finish (acc, res,&error)))
    {
      g_error ("Failed to obtain tp_status: %s", error->message);
    }

  g_message ("Processing tp_status");

  priv->tp_status = tp_status;
  g_object_notify (G_OBJECT (self), "tp-status");

  tp_g_signal_connect_object (tp_status, "service-added",
                              G_CALLBACK (yts_client_service_added_cb),
                              self, 0);
  tp_g_signal_connect_object (tp_status, "service-removed",
                              G_CALLBACK (yts_client_service_removed_cb),
                              self, 0);
  tp_g_signal_connect_object (tp_status, "status-changed",
                              G_CALLBACK (_tp_yts_status_changed),
                              self, 0);

  /* Advertise statii that have been set before our TpStatus was ready. */
  yts_client_status_foreach_capability (
    priv->client_status,
    (YtsClientStatusCapabilityIterator) _client_status_foreach_capability_advertise_status,
    self);

  yts_client_process_status (self);

  if (!priv->ready)
    {
      g_message ("Emitting 'ready' signal");
      g_signal_emit (self, signals[READY], 0);
    }
}

static void
yts_client_connection_ready_cb (TpConnection *conn,
                                GParamSpec   *par,
                                YtsClient   *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  GCancellable      *cancellable;

  if (tp_connection_is_ready (conn))
    {
      g_message ("TP Connection entered ready state");

      cancellable = g_cancellable_new ();

      tp_yts_status_ensure_async (priv->tp_account,
                                  cancellable,
                                  (GAsyncReadyCallback) yts_client_yts_status_cb,
                                  self);

      /*
       * TODO -- this should be stored, so we can clean up in dispose any
       * pending op, ???
       *
       * But the TpYtsStatus docs say it's not used ...
       */
      g_object_unref (cancellable);
    }
}

static void
yts_client_connection_prepare_cb (GObject       *connection,
                                  GAsyncResult  *res,
                                  YtsClient     *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  GError            *error  = NULL;

  if (!tp_proxy_prepare_finish (connection, res, &error))
    {
      g_critical ("Failed to prepare info: %s", error->message);
    }
  else
    {
      if (!tp_yts_client_register (priv->tp_client, &error))
        {
          g_error ("Failed to register account: %s", error->message);
        }
      else
        g_message ("Registered TpYtsClient");

      tp_g_signal_connect_object (priv->tp_client, "received-channels",
                              G_CALLBACK (yts_client_yts_channels_received_cb),
                              self, 0);
#if 0
      /* TODO -- */
      /*
       * local-xmpp / salut does not support the ContactCapabilities interface,
       * but file transfer is enabled by default, so it does not matter to us.
       */
      if (priv->protocol != YTS_PROTOCOL_LOCAL_XMPP)
        yts_client_setup_caps (client);
#endif
    }
}

/*
 * Sets up required features on the connection, and connects callbacks to
 * signals that we care about.
 */
static void
yts_client_setup_account_connection (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  GError            *error = NULL;
  GQuark             features[] = { TP_CONNECTION_FEATURE_CONTACT_INFO,
                                    TP_CONNECTION_FEATURE_CAPABILITIES,
                                    TP_CONNECTION_FEATURE_CONNECTED,
                                    0 };

  priv->tp_conn = tp_account_get_connection (priv->tp_account);

  g_assert (priv->tp_conn);

  priv->dialing = FALSE;

  g_message ("Connection ready ?: %d",
             tp_connection_is_ready (priv->tp_conn));

  tp_g_signal_connect_object (priv->tp_conn, "notify::connection-ready",
                              G_CALLBACK (yts_client_connection_ready_cb),
                              self, 0);

  tp_cli_connection_connect_to_connection_error (priv->tp_conn,
                                                 yts_client_error_cb,
                                                 self,
                                                 NULL,
                                                 (GObject*)self,
                                                 &error);

  if (error)
    {
      g_critical (G_STRLOC ": %s: %s; no Ytstenut functionality will be "
                  "available", __FUNCTION__, error->message);
      g_clear_error (&error);
      return;
    }

  tp_cli_connection_connect_to_status_changed (priv->tp_conn,
      (tp_cli_connection_signal_callback_status_changed) yts_client_status_cb,
                                               self,
                                               NULL,
                                               (GObject*) self,
                                               &error);

  if (error)
    {
      g_critical (G_STRLOC ": %s: %s; no Ytstenut functionality will be "
                  "available", __FUNCTION__, error->message);
      g_clear_error (&error);
      return;
    }

  if (error)
    {
      g_critical (G_STRLOC ": %s: %s; no Ytstenut functionality will be "
                  "available", __FUNCTION__, error->message);
      g_clear_error (&error);
      return;
    }

  tp_proxy_prepare_async (priv->tp_conn,
                          features,
                          (GAsyncReadyCallback) yts_client_connection_prepare_cb,
                          self);
}

/*
 * Callback for the async request to change presence ... not that we do
 * do anything with it, except when it fails.
 */
static void
yts_client_account_online_cb (GObject      *acc,
                               GAsyncResult *res,
                               gpointer      data)
{
  GError    *error   = NULL;
  TpAccount *account = (TpAccount*)acc;
  char      *stat;
  char      *msg;
  TpConnectionPresenceType presence;

  if (!tp_account_request_presence_finish (account, res, &error))
    {
      g_error ("Failed to change presence to online");
    }

  presence = tp_account_get_current_presence (account, &stat, &msg);

  g_message ("Request to change presence to 'online' succeeded: %d, %s:%s",
             presence, stat, msg);

  g_free (stat);
  g_free (msg);
}

/*
 * One off handler for connection coming online
 */
static void
yts_client_account_connection_notify_cb (TpAccount  *account,
                                          GParamSpec *pspec,
                                          YtsClient *client)
{
  g_message ("We got connection!");

  g_signal_handlers_disconnect_by_func (account,
                                   yts_client_account_connection_notify_cb,
                                   client);

  yts_client_setup_account_connection (client);
}

static void
yts_client_make_connection (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  /*
   * If we don't have an account yet, we do nothing and will make call to this
   * function when the account is ready.
   */
  if (!priv->tp_account)
    {
      g_message ("Account not yet available");
      return;
    }

  /*
   * At this point the account is prepared, but that does not mean we have a
   * connection (i.e., the current presence could 'off line' -- if we do not
   * have a connection, we request that the presence changes to 'on line' and
   * listen for when the :connection property changes.
   */
  if (!tp_account_get_connection (priv->tp_account))
    {
      g_message ("Currently off line, changing ...");

      g_signal_connect (priv->tp_account, "notify::connection",
                        G_CALLBACK (yts_client_account_connection_notify_cb),
                        self);

      tp_account_request_presence_async (priv->tp_account,
                                         TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,
                                         "online",
                                         "online",
                                         yts_client_account_online_cb,
                                         self);
    }
  else
    yts_client_setup_account_connection (self);
}

/**
 * yts_client_connect:
 * @self: object on which to invoke this method.
 *
 * Initiates connection to the mesh. Once the connection is established,
 * the #YtsClient::authenticated signal will be emitted.
 *
 * Since 0.3
 */
void
yts_client_connect (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  g_message ("Connecting ...");

  g_return_if_fail (YTS_IS_CLIENT (self));

  /* cancel any pending reconnect timeout */
  if (priv->reconnect_id)
    {
      g_source_remove (priv->reconnect_id);
      priv->reconnect_id = 0;
    }

  priv->connect = TRUE;

  if (priv->tp_conn)
    {
      /*
       * We already have the connection, so just connect.
       */
      tp_cli_connection_call_connect (priv->tp_conn,
                                      -1,
(tp_cli_connection_callback_for_connect) yts_client_connected_cb,
                                      self,
                                      NULL,
                                      (GObject*)self);
    }
  else if (!priv->dialing)
    yts_client_make_connection (self);
}

// TODO get rid of this, it invalidates the proxies
static void
yts_client_refresh_roster (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  g_message ("Refreshing roster");

  if (!priv->tp_status)
    return;

  yts_roster_clear (priv->roster);
  yts_roster_clear (priv->unwanted);

  yts_client_process_status (self);
}

/**
 * yts_client_add_capability:
 * @self: object on which to invoke this method.
 * @capability: Name of the capability.
 *
 * Adds a capability to the capability set of this client; multiple capabilities
 * can be added by making mulitiple calls to this function.
 *
 * Since: 0.3
 */
void
yts_client_add_capability (YtsClient          *self,
                           char const         *c,
                           YtsCapabilityMode   mode)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  char *capability;

  /* FIXME check that there's no collision with service owned capabilities. */

  g_return_if_fail (YTS_IS_CLIENT (self));
  g_return_if_fail (c);

  // TODO make sure c doesn't have prefix already
  capability = g_strdup_printf ("urn:ytstenut:capabilities:%s", c);

  if (YTS_CAPABILITY_MODE_PROVIDED == mode) {

    if (yts_client_status_add_capability (priv->client_status, capability)) {
      /* Advertise right away if possible, otherwise the advertising will
       * happen when the tp_client is ready. */
      if (priv->tp_client) {
        tp_yts_client_add_capability (priv->tp_client, capability);
      }
    } else {
      g_message ("Capablity '%s' already set", capability);
      return;
    }

    yts_client_refresh_roster (self);

  } else if (YTS_CAPABILITY_MODE_CONSUMED == mode) {

    if (yts_client_status_add_interest (priv->client_status, capability)) {
      /* Advertise right away if possible, otherwise the advertising will
       * happen when the tp_client is ready. */
      if (priv->tp_client) {
        tp_yts_client_add_interest (priv->tp_client, capability);
      }
    } else {
      g_message ("Interest '%s' already set", capability);
      return;
    }

  } else {
    g_critical ("Invalid capability mode %d", mode);
  }

  g_free (capability);
}

/**
 * yts_client_get_roster:
 * @self: object on which to invoke this method.
 *
 * Gets the #YtsRoster for this client. The object is owned by the client
 * and must not be freed by the caller.
 *
 * Returns: (transfer none): #YtsRoster.
 */
YtsRoster *const
yts_client_get_roster (YtsClient const *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  g_return_val_if_fail (YTS_IS_CLIENT (self), NULL);

  return priv->roster;
}

/**
 * yts_client_emit_error:
 * @self: object on which to invoke this method.
 * @error: #YtsError
 *
 * Emits the #YtsClient::error signal with the suplied error parameter.
 *
 * This function is intened primarily for internal use, but can be also used by
 * toolkit libraries that need to generate asynchronous errors. Any function
 * call that returns the %YTS_ERROR_PENDING code to the caller should
 * eventually lead to emission of the ::error signal with either an appropriate
 * error code or %YTS_ERROR_SUCCESS to indicate the operation successfully
 * completed.
 *
 * Deprecated: This function will be removed in 0.4
 */
void
yts_client_emit_error (YtsClient *self, YtsError error)
{
  g_return_if_fail (YTS_IS_CLIENT (self));

  /*
   * There is no point in throwing an error that has no atom specified.
   */
  g_return_if_fail (yts_error_get_atom (error));

  g_signal_emit (self, signals[ERROR], 0, error);
}

/**
 * yts_client_get_contact_id:
 * @self: object on which to invoke this method.
 *
 * Getter for #YtsClient.#YtsClient:contact-id.
 *
 * Returns: (transfer none): the jabber id.
 */
char const *
yts_client_get_contact_id (YtsClient const *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  g_return_val_if_fail (YTS_IS_CLIENT (self), NULL);
  g_return_val_if_fail (priv->tp_account, NULL);

  return tp_account_get_normalized_name (priv->tp_account);
}

/**
 * yts_client_get_service_id:
 * @self: object on which to invoke this method.
 *
 * Getter for #YtsClient.#YtsClient:service-id.
 *
 * Returns: (transfer none): the service ID.
 */
char const *
yts_client_get_service_id (YtsClient const *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  g_return_val_if_fail (YTS_IS_CLIENT (self), NULL);

  return priv->service_id;
}

TpConnection *const
yts_client_get_tp_connection (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  g_return_val_if_fail (YTS_IS_CLIENT (self), NULL);

  return priv->tp_conn;
}

TpYtsStatus *const
yts_client_get_tp_status (YtsClient *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);

  g_return_val_if_fail (YTS_IS_CLIENT (self), NULL);

  return priv->tp_status;
}

/**
 * yts_client_set_status_by_capability:
 * @self: object on which to invoke this method.
 * @capability: the capability to set status for
 * @activity: the activity to set the status to.
 *
 * Set the status of the service represented by this client to @activity for
 * @capability.
 *
 * FIXME: Maybe this should be named yts_client_set_status_on_capability() or
 *        yts_client_set_status_for_capability() ?
 *        Also maybe the "activity" should not be exposed any more because we're
 *        kinda moving away from it, instead allow setting the xml payload?
 *        Will things work at all without the activity attribut -- to to check
 *        the spec.
 */
void
yts_client_set_status_by_capability (YtsClient    *self,
                                      char const  *cap_value,
                                      char const  *activity,
                                      char const  *status_xml)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  char        *capability;
  char const  *capability_status_xml;
  char const  *attribs[] = {
    "activity", activity,
    NULL
  };

  g_return_if_fail (YTS_IS_CLIENT (self));
  g_return_if_fail (cap_value);
  g_return_if_fail (activity);

  capability = g_strdup_printf ("%s%s",
                                YTS_XML_CAPABILITY_NAMESPACE,
                                cap_value);

  /* Check if the capability is already advertised. */
  if (yts_client_status_add_capability (priv->client_status, capability)) {
    /* Add capability if we already have a tp_client,
     * otherwise that's done when it's ready. */
    if (priv->tp_client) {
      tp_yts_client_add_capability (priv->tp_client, capability);
    }
  }

  capability_status_xml = yts_client_status_set (priv->client_status,
                                                 capability,
                                                 attribs,
                                                 status_xml);

  /* Advertise if we already have a tp_status,
   * otherwise that's done when it's ready. */
  if (priv->client_status) {
    tp_yts_status_advertise_status_async (priv->tp_status,
                                          capability,
                                          priv->service_id,
                                          capability_status_xml,
                                          NULL,
                                          _tp_yts_status_advertise_status_cb,
                                          self);
  }
}

struct YtsCLChannelData
{
  YtsClient  *client;
  YtsContact *contact;
  GHashTable  *attrs;
  char        *xml;
  char        *service_id;
  YtsError    error;
  gboolean     status_done;
  int          ref_count;
};

static void
yts_cl_channel_data_unref (struct YtsCLChannelData *d)
{
  d->ref_count--;

  if (d->ref_count <= 0)
    {
      g_hash_table_unref (d->attrs);
      g_free (d->xml);
      g_free (d->service_id);
      g_free (d);
    }
}

static struct YtsCLChannelData *
yts_cl_channel_data_ref (struct YtsCLChannelData *d)
{
  d->ref_count++;
  return d;
}

static void
yts_client_msg_replied_cb (TpYtsChannel *proxy,
                            GHashTable   *attributes,
                            char const   *body,
                            gpointer      data,
                            GObject      *weak_object)
{
  GHashTableIter            iter;
  gpointer                  key, value;
  struct YtsCLChannelData *d = data;

  g_message ("Got reply with attributes:");

  g_hash_table_iter_init (&iter, attributes);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      g_message ("    %s = %s\n",
                 (char const *) key, (char const  *) value);
    }

  g_message ("    body: %s\n", body);

  if (!d->status_done)
    {
      guint32   a;
      YtsError e;

      a = yts_error_get_atom (d->error);
      e = yts_error_make (a, YTS_ERROR_SUCCESS);

      yts_client_emit_error (d->client, e);

      d->status_done = TRUE;
    }

  yts_cl_channel_data_unref (d);
}

static void
yts_client_msg_failed_cb (TpYtsChannel *proxy,
                           guint         error_type,
                           char const   *stanza_error_name,
                           char const   *ytstenut_error_name,
                           char const   *text,
                           gpointer      data,
                           GObject      *weak_object)
{
  guint32                   a;
  YtsError                 e;
  struct YtsCLChannelData *d = data;

  a = yts_error_get_atom (d->error);

  g_warning ("Sending of message failed: type %u, %s, %s, %s",
             error_type, stanza_error_name, ytstenut_error_name, text);

  e = yts_error_make (a, YTS_ERROR_NO_MSG_CHANNEL);

  yts_client_emit_error (d->client, e);

  d->status_done = TRUE;

  yts_cl_channel_data_unref (d);
}

static void
yts_client_msg_closed_cb (TpChannel *channel,
                           gpointer   data,
                           GObject   *weak_object)
{
  struct YtsCLChannelData *d = data;

  g_message ("Channel closed");

  if (!d->status_done)
    {
      guint32   a;
      YtsError e;

      a = yts_error_get_atom (d->error);
      e = yts_error_make (a, YTS_ERROR_SUCCESS);

      yts_client_emit_error (d->client, e);

      d->status_done = TRUE;
    }

  yts_cl_channel_data_unref (d);
}

static void
yts_client_msg_request_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      data)
{
  GError *error = NULL;

  if (!tp_yts_channel_request_finish (
          TP_YTS_CHANNEL (source_object), result, &error))
    {
      g_warning ("Failed to Request on channel: %s\n", error->message);
    }
  else
    {
      g_message ("Channel requested");
    }

  g_clear_error (&error);
}

static void
yts_client_outgoing_channel_cb (GObject      *obj,
                                 GAsyncResult *res,
                                 gpointer      data)
{
  TpYtsChannel             *ch;
  TpYtsClient              *client = TP_YTS_CLIENT (obj);
  GError                   *error  = NULL;
  struct YtsCLChannelData *d      = data;

  if (!(ch = tp_yts_client_request_channel_finish (client, res, &error)))
    {
      guint32   a;
      YtsError e;

      a = yts_error_get_atom (d->error);

      g_warning ("Failed to open outgoing channel: %s", error->message);
      g_clear_error (&error);

      e = yts_error_make (a, YTS_ERROR_NO_MSG_CHANNEL);

      yts_client_emit_error (d->client, e);
    }
  else
    {
      g_message ("Got message channel, sending request");

      tp_yts_channel_connect_to_replied (ch, yts_client_msg_replied_cb,
                                         yts_cl_channel_data_ref (d),
                                         NULL, NULL, NULL);
      tp_yts_channel_connect_to_failed (ch, yts_client_msg_failed_cb,
                                        yts_cl_channel_data_ref (d),
                                        NULL, NULL, NULL);
      tp_cli_channel_connect_to_closed (TP_CHANNEL (ch),
                                        yts_client_msg_closed_cb,
                                        yts_cl_channel_data_ref (d),
                                        NULL, NULL, NULL);

      tp_yts_channel_request_async (ch, NULL, yts_client_msg_request_cb, NULL);
    }

  yts_cl_channel_data_unref (d);
}

static YtsError
yts_client_dispatch_message (struct YtsCLChannelData *d)
{
  TpContact         *tp_contact;
  YtsClientPrivate *priv = GET_PRIVATE (d->client);

  g_message ("Dispatching delayed message to %s", d->service_id);

  tp_contact = yts_contact_get_tp_contact (d->contact);
  g_assert (tp_contact);

  tp_yts_client_request_channel_async (priv->tp_client,
                                       tp_contact,
                                       d->service_id,
                                       TP_YTS_REQUEST_TYPE_GET,
                                       d->attrs,
                                       d->xml,
                                       NULL,
                                       yts_client_outgoing_channel_cb,
                                       d);

  return d->error;
}

static void
yts_client_notify_tp_contact_cb (YtsContact              *contact,
                                  GParamSpec               *pspec,
                                  struct YtsCLChannelData *d)
{
  g_message ("Contact ready");
  yts_client_dispatch_message (d);
  g_signal_handlers_disconnect_by_func (contact,
                                        yts_client_notify_tp_contact_cb,
                                        d);
}

YtsError
yts_client_send_message (YtsClient   *client,
                           YtsContact  *contact,
                           char const   *service_id,
                           YtsMetadata *message)
{
  GHashTable               *attrs;
  struct YtsCLChannelData *d;
  YtsError                 e;
  char                     *xml = NULL;

  if (!(attrs = yts_metadata_extract (message, &xml)))
    {
      g_warning ("Failed to extract content from YtsMessage object");

      e = yts_error_new (YTS_ERROR_INVALID_PARAMETER);
      g_free (xml);
      return e;
    }

  e = yts_error_new (YTS_ERROR_PENDING);

  d              = g_new (struct YtsCLChannelData, 1);
  d->error       = e;
  d->client      = client;
  d->contact     = contact;
  d->status_done = FALSE;
  d->ref_count   = 1;
  d->attrs       = attrs;
  d->xml         = xml;
  d->service_id  = g_strdup (service_id);

  if (yts_contact_get_tp_contact (contact))
    {
      yts_client_dispatch_message (d);
    }
  else
    {
      g_message ("Contact not ready, postponing message dispatch");

      g_signal_connect (contact, "notify::tp-contact",
                        G_CALLBACK (yts_client_notify_tp_contact_cb),
                        d);
    }

  return e;
}

static void
_adapter_error (YtsServiceAdapter  *adapter,
                char const          *invocation_id,
                GError const        *error,
                YtsClient          *self)
{
  YtsMetadata *message;

  message = yts_error_message_new (g_quark_to_string (error->domain),
                                    error->code,
                                    error->message,
                                    invocation_id);

  // TODO
  g_debug ("%s() not implemented at %s", __FUNCTION__, G_STRLOC);

  g_object_unref (message);
}

static void
_adapter_event (YtsServiceAdapter  *adapter,
                char const          *aspect,
                GVariant            *arguments,
                YtsClient          *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  YtsMetadata *message;
  ProxyList   *proxy_list;
  char        *fqc_id;

  fqc_id = yts_service_adapter_get_fqc_id (adapter);
  message = yts_event_message_new (fqc_id, aspect, arguments);

  /* Dispatch to all registered proxies. */
  proxy_list = g_hash_table_lookup (priv->proxies, fqc_id);
  if (proxy_list) {
    GList const *iter;
    for (iter = proxy_list->list; iter; iter = iter->next) {
      ProxyData const *proxy_data = (ProxyData const *) iter->data;
      yts_client_send_message (self,
                                 YTS_CONTACT (proxy_data->contact),
                                 proxy_data->proxy_id,
                                 message);
    }
  }
  g_free (fqc_id);
  g_object_unref (message);
}

static void
_adapter_response (YtsServiceAdapter *adapter,
                   char const         *invocation_id,
                   GVariant           *return_value,
                   YtsClient         *self)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  InvocationData  *invocation;
  YtsMetadata    *message;
  char            *fqc_id;

  invocation = g_hash_table_lookup (priv->invocations, invocation_id);
  if (NULL == invocation) {
    // FIXME report error
    g_critical ("%s : Data not found to respond to invocation %s",
                G_STRLOC,
                invocation_id);
  }

  fqc_id = yts_service_adapter_get_fqc_id (adapter);
  message = yts_response_message_new (fqc_id,
                                       invocation_id,
                                       return_value);
  yts_client_send_message (self,
                             invocation->contact,
                             invocation->proxy_id,
                             message);
  g_object_unref (message);
  g_free (fqc_id);

  client_conclude_invocation (self, invocation_id);
}

static void
_service_destroyed (ServiceData *data,
                    void        *stale_service_ptr)
{
  YtsClientPrivate *priv = GET_PRIVATE (data->client);

  g_hash_table_remove (priv->services, data->capability);
  service_data_destroy (data);
}

/**
 * yts_client_publish_service:
 * @self: object on which to invoke this method.
 * @service: Service implementation.
 *
 * Publish a service to the Ytstenut network.
 *
 * Returns: <literal>true</literal> if publishing succeeded.
 *
 * Since 0.3
 */
bool
yts_client_publish_service (YtsClient     *self,
                            YtsCapability *service)
{
/*
 * TODO add GError reporting
 * The client does not take ownership of the service, it will be
 * unregistered upon destruction.
 */
  YtsClientPrivate *priv = GET_PRIVATE (self);
  YtsServiceAdapter   *adapter;
  YtsProfileImpl      *profile_impl;
  ServiceData          *service_data;
  char                **fqc_ids;
  unsigned              i;
  YtsAdapterFactory   *const factory = yts_adapter_factory_get_default ();

  g_return_val_if_fail (YTS_IS_CLIENT (self), FALSE);
  g_return_val_if_fail (YTS_IS_CAPABILITY (service), FALSE);

  fqc_ids = yts_capability_get_fqc_ids (service);

  /* Check that capabilities are not implemented yet. */
  for (i = 0; fqc_ids[i] != NULL; i++) {

    adapter = g_hash_table_lookup (priv->services, fqc_ids[i]);
    if (adapter)
      {
        g_critical ("%s : Service for capability %s already registered",
                    G_STRLOC,
                    fqc_ids[i]);
        g_strfreev (fqc_ids);
        return FALSE;
      }
  }

  /* Hook up the service */
  for (i = 0; fqc_ids[i] != NULL; i++) {

    adapter = yts_adapter_factory_create_adapter (factory, service, fqc_ids[i]);
    g_return_val_if_fail (adapter, FALSE);

    service_data = service_data_create (self, fqc_ids[i]);
    g_object_weak_ref (G_OBJECT (service),
                       (GWeakNotify) _service_destroyed,
                       service_data);

    g_signal_connect (adapter, "error",
                      G_CALLBACK (_adapter_error), self);
    g_signal_connect (adapter, "event",
                      G_CALLBACK (_adapter_event), self);
    g_signal_connect (adapter, "response",
                      G_CALLBACK (_adapter_response), self);

    /* Hash table takes adapter reference */
    g_hash_table_insert (priv->services,
                         g_strdup (fqc_ids[i]),
                         adapter);
    yts_client_add_capability (self, fqc_ids[i], YTS_CAPABILITY_MODE_PROVIDED);

    /* Keep the proxy management service up to date. */
    adapter = g_hash_table_lookup (priv->services, YTS_PROFILE_FQC_ID);
    if (NULL == adapter) {
      profile_impl = yts_profile_impl_new (self);
      adapter = g_object_new (YTS_TYPE_PROFILE_ADAPTER,
                              "service", profile_impl,
                              NULL);
      g_hash_table_insert (priv->services,
                           g_strdup (YTS_PROFILE_FQC_ID),
                           adapter);

      g_signal_connect (adapter, "error",
                        G_CALLBACK (_adapter_error), self);
      g_signal_connect (adapter, "event",
                        G_CALLBACK (_adapter_event), self);
      g_signal_connect (adapter, "response",
                        G_CALLBACK (_adapter_response), self);

    } else {
      profile_impl = YTS_PROFILE_IMPL (
                        yts_service_adapter_get_service (adapter));
      /* Not nice, but it's still referenced by the adapter. */
      g_object_unref (profile_impl);
    }

    yts_profile_impl_add_capability (profile_impl, fqc_ids[i]);
  }

  g_strfreev (fqc_ids);

  return TRUE;
}

bool
yts_client_get_invocation_proxy (YtsClient   *self,
                                  char const   *invocation_id,
                                  YtsContact **contact,
                                  char const  **proxy_id)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  InvocationData const *invocation;

  g_return_val_if_fail (YTS_IS_CLIENT (self), false);
  g_return_val_if_fail (contact, false);
  g_return_val_if_fail (proxy_id, false);

  invocation = g_hash_table_lookup (priv->invocations, invocation_id);
  g_return_val_if_fail (invocation, false);

  *contact = invocation->contact;
  *proxy_id = invocation->proxy_id;

  return true;
}

GVariant *
yts_client_register_proxy (YtsClient  *self,
                            char const  *capability,
                            YtsContact *contact,
                            char const  *proxy_id)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  ProxyList           *proxy_list;
  YtsServiceAdapter  *adapter = NULL;
  GVariant            *properties = NULL;

  g_return_val_if_fail (YTS_IS_CLIENT (self), false);

  proxy_list = g_hash_table_lookup (priv->proxies, capability);
  if (NULL == proxy_list) {
    proxy_list = proxy_list_create_with_proxy (contact, proxy_id);
    g_hash_table_insert (priv->proxies,
                         g_strdup (capability),
                         proxy_list);
  } else {
    proxy_list_ensure_proxy (proxy_list, contact, proxy_id);
  }


  /* This is a bit of a hack but we're returning the collected
   * object properties as response to the register-proxy invocation. */
  adapter = g_hash_table_lookup (priv->services, capability);
  if (adapter) {
    properties = yts_service_adapter_collect_properties (adapter);

  } else {

    g_critical ("%s : Could not find adapter for capability %s",
                G_STRLOC,
                capability);
  }

  return properties;
}

bool
yts_client_unregister_proxy (YtsClient  *self,
                              char const  *capability,
                              char const  *proxy_id)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  ProxyList *proxy_list;

  g_return_val_if_fail (YTS_IS_CLIENT (self), false);

  proxy_list = g_hash_table_lookup (priv->proxies, capability);
  if (NULL == proxy_list) {
    g_warning ("%s : No proxy for %s:%s",
               G_STRLOC,
               proxy_id,
               capability);
    return false;
  }

  proxy_list_purge_proxy_id (proxy_list, proxy_id);
  if (proxy_list_is_empty (proxy_list)) {
    g_hash_table_remove (priv->proxies, capability);
  }

  return true;
}

/**
 * yts_client_foreach_service:
 * @self: object on which to invoke this method.
 * @iterator: iterator function.
 * @user_data: context to pass to the iterator function.
 *
 * Iterate over @self's published services.
 *
 * Returns: <literal>true</literal> if all the services have been iterated.
 *
 * Since: 0.3
 */
bool
yts_client_foreach_service (YtsClient                 *self,
                            YtsClientServiceIterator   iterator,
                            void                      *user_data)
{
  YtsClientPrivate *priv = GET_PRIVATE (self);
  GHashTableIter     iter;
  char const        *fqc_id;
  YtsServiceAdapter *adapter;
  bool               ret = true;

  g_return_val_if_fail (YTS_IS_CLIENT (self), false);
  g_return_val_if_fail (iterator, false);

  g_hash_table_iter_init (&iter, priv->services);
  while (ret &&
         g_hash_table_iter_next (&iter,
                                 (void **) &fqc_id,
                                 (void **) &adapter)) {
    YtsCapability *capability = yts_service_adapter_get_service (adapter);
    ret = iterator (self, fqc_id, capability, user_data);
  }

  return ret;
}

