/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include <string.h>
#include <telepathy-glib/util.h>

#include "ytsg-client.h"
#include "ytsg-debug.h"
#include "ytsg-marshal.h"
#include "ytsg-message.h"
#include "ytsg-metadata-service.h"
#include "ytsg-private.h"
#include "ytsg-status.h"

static void ytsg_metadata_service_dispose (GObject *object);
static void ytsg_metadata_service_finalize (GObject *object);
static void ytsg_metadata_service_constructed (GObject *object);
static void ytsg_metadata_service_get_property (GObject    *object,
                                                guint       property_id,
                                                GValue     *value,
                                                GParamSpec *pspec);
static void ytsg_metadata_service_set_property (GObject      *object,
                                                guint         property_id,
                                                const GValue *value,
                                                GParamSpec   *pspec);

G_DEFINE_TYPE (YtsgMetadataService, ytsg_metadata_service, YTSG_TYPE_SERVICE);

#define YTSG_METADATA_SERVICE_GET_PRIVATE(o) \
(G_TYPE_INSTANCE_GET_PRIVATE ((o), YTSG_TYPE_METADATA_SERVICE, YtsgMetadataServicePrivate))

struct _YtsgMetadataServicePrivate
{
  const char  *type;
  char       **caps;
  GHashTable  *names;

  guint disposed : 1;
};

enum
{
  STATUS,
  MESSAGE,

  N_SIGNALS,
};

enum
{
  PROP_0,
  PROP_TYPE,
  PROP_NAMES,
  PROP_CAPS,
};

static guint signals[N_SIGNALS] = {0};

static void
ytsg_metadata_service_class_init (YtsgMetadataServiceClass *klass)
{
  GParamSpec   *pspec;
  GObjectClass *object_class = (GObjectClass *)klass;

  g_type_class_add_private (klass, sizeof (YtsgMetadataServicePrivate));

  object_class->dispose      = ytsg_metadata_service_dispose;
  object_class->finalize     = ytsg_metadata_service_finalize;
  object_class->constructed  = ytsg_metadata_service_constructed;
  object_class->get_property = ytsg_metadata_service_get_property;
  object_class->set_property = ytsg_metadata_service_set_property;

  /**
   * YtsgService:type:
   *
   * The type of this service
   */
  pspec = g_param_spec_string ("type",
                               "type",
                               "type",
                               NULL,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_TYPE, pspec);

  /**
   * YtsgService:names:
   *
   * The names of this service
   */
  pspec = g_param_spec_boxed ("names",
                              "names",
                              "names",
                              G_TYPE_HASH_TABLE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_NAMES, pspec);

  /**
   * YtsgService:caps:
   *
   * The caps of this service
   */
  pspec = g_param_spec_boxed ("caps",
                              "caps",
                              "caps",
                              G_TYPE_STRV,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_CAPS, pspec);

  /**
   * YtsgMetadataService::status:
   * @service: the service which received the signal
   * @status: the status
   *
   * The ::status signal is emitted when the status of a given service changes
   *
   * Since: 0.1
   */
  signals[STATUS] =
    g_signal_new (I_("status"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (YtsgMetadataServiceClass, received_status),
                  NULL, NULL,
                  ytsg_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  YTSG_TYPE_STATUS);


  /**
   * YtsgMetadataService::message:
   * @service: the service which received the signal
   * @message: the message
   *
   * The ::message signal is emitted when message is received on given service
   *
   * Since: 0.1
   */
  signals[MESSAGE] =
    g_signal_new (I_("message"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (YtsgMetadataServiceClass, received_message),
                  NULL, NULL,
                  ytsg_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  YTSG_TYPE_MESSAGE);
}

static void
ytsg_metadata_service_status_changed_cb (TpYtsStatus *status,
                                         const gchar *contact_id,
                                         const gchar *capability,
                                         const gchar *service_name,
                                         const gchar *xml,
                                         gpointer     data)
{
  YtsgMetadataService        *self = data;
  YtsgMetadataServicePrivate *priv = self->priv;

  const char *jid = ytsg_service_get_jid (data);
  const char *uid = ytsg_service_get_uid (data);

  g_return_if_fail (contact_id && service_name && jid && uid);

  if (strcmp (contact_id, jid) || strcmp (service_name, uid))
    return;

  YTSG_NOTE (STATUS, "Status changed for %s/%s:%s",
             contact_id, service_name, capability);

  /* FIXME -- update status */
  g_warning ("NOT IMPLEMENTED");
}

static void
ytsg_metadata_service_constructed (GObject *object)
{
  YtsgMetadataService        *self = (YtsgMetadataService*) object;
  YtsgClient                 *client;
  TpYtsStatus                *status;

  if (G_OBJECT_CLASS (ytsg_metadata_service_parent_class)->constructed)
    G_OBJECT_CLASS (ytsg_metadata_service_parent_class)->constructed (object);

  client = ytsg_service_get_client ((YtsgService *)object);
  g_assert (client);

  status = _ytsg_client_get_status (client);
  g_assert (status);

  tp_g_signal_connect_object (status, "status-changed",
                          G_CALLBACK (ytsg_metadata_service_status_changed_cb),
                          self, 0);
}

static void
ytsg_metadata_service_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  YtsgMetadataService        *self = (YtsgMetadataService*) object;
  YtsgMetadataServicePrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_TYPE:
      g_value_set_string (value, priv->type);
      break;
    case PROP_NAMES:
      g_value_set_boxed (value, priv->names);
      break;
    case PROP_CAPS:
      g_value_set_boxed (value, priv->caps);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
ytsg_metadata_service_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  YtsgMetadataService        *self = (YtsgMetadataService*) object;
  YtsgMetadataServicePrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_TYPE:
      priv->type = g_intern_string (g_value_get_string (value));
      break;
    case PROP_NAMES:
      priv->names = g_value_dup_boxed (value);
      break;
    case PROP_CAPS:
      priv->caps = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
ytsg_metadata_service_init (YtsgMetadataService *self)
{
  self->priv = YTSG_METADATA_SERVICE_GET_PRIVATE (self);
}

static void
ytsg_metadata_service_dispose (GObject *object)
{
  YtsgMetadataService        *self = (YtsgMetadataService*) object;
  YtsgMetadataServicePrivate *priv = self->priv;

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  if (priv->names)
    {
      g_hash_table_unref (priv->names);
      priv->names = NULL;
    }

  G_OBJECT_CLASS (ytsg_metadata_service_parent_class)->dispose (object);
}

static void
ytsg_metadata_service_finalize (GObject *object)
{
  YtsgMetadataService        *self = (YtsgMetadataService*) object;
  YtsgMetadataServicePrivate *priv = self->priv;

  if (priv->caps)
    g_strfreev (priv->caps);

  G_OBJECT_CLASS (ytsg_metadata_service_parent_class)->finalize (object);
}

void
_ytsg_metadata_service_received_status (YtsgMetadataService *service,
                                        const char          *xml)
{
  YtsgStatus *status;

  status = (YtsgStatus*) _ytsg_metadata_new_from_xml (xml);

  g_return_if_fail (YTSG_IS_STATUS (status));

  g_signal_emit (service, signals[STATUS], 0, status);
}

void
_ytsg_metadata_service_received_message (YtsgMetadataService *service,
                                         const char          *xml)
{
  YtsgMessage *message;

  message = (YtsgMessage*) _ytsg_metadata_new_from_xml (xml);

  g_return_if_fail (YTSG_IS_MESSAGE (message));

  g_signal_emit (service, signals[MESSAGE], 0, message);
}

static void
ytsg_service_metadata_send_status (YtsgMetadataService *service,
                                   YtsgStatus          *status)
{
  g_critical (G_STRLOC ": NOT IMPLEMENTED !!!");
}

static void
ytsg_service_metadata_send_message (YtsgMetadataService *service,
                                    YtsgMessage         *message)
{
  g_critical (G_STRLOC ": NOT IMPLEMENTED !!!");
}

/**
 * ytsg_metadata_service_send_metadata:
 * @service: #YtsgMetadataService
 * @metadata: #YtsgMetadata that was received
 *
 *  Sends the provided metadata via the service.
 */
void
ytsg_metadata_service_send_metadata (YtsgMetadataService *service,
                                     YtsgMetadata        *metadata)
{
  g_return_if_fail (YTSG_IS_METADATA_SERVICE (service));
  g_return_if_fail (YTSG_IS_METADATA (metadata));

  if (YTSG_IS_STATUS (metadata))
    {
      ytsg_service_metadata_send_status (service, (YtsgStatus*)metadata);
    }
  else if (YTSG_IS_MESSAGE (metadata))
    {
      ytsg_service_metadata_send_message (service, (YtsgMessage*)metadata);
    }
  else
    g_warning ("Unknown metadata type %s",  G_OBJECT_TYPE_NAME (metadata));
}

YtsgService *
_ytsg_metadata_service_new (YtsgClient  *client,
                            const char  *jid,
                            const char  *uid,
                            const char  *type,
                            const char **caps,
                            GHashTable  *names)
{
  g_return_val_if_fail (uid && *uid, NULL);

  return g_object_new (YTSG_TYPE_METADATA_SERVICE,
                       "client", client,
                       "jid",    jid,
                       "uid",    uid,
                       "type",   type,
                       "caps",   caps,
                       "names",  names,
                       NULL);
}

