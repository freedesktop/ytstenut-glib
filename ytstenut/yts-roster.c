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
 */

/**
 * SECTION:yts-roster
 * @short_description: Represents a roster of devices and services
 * connected to the Ytstenut application mesh.
 *
 * #YtsRoster represents all known devices and services in the Ytstenut
 * application mesh.
 */

#include "yts-client.h"
#include "yts-contact.h"
#include "yts-debug.h"
#include "yts-marshal.h"
#include "yts-private.h"
#include "yts-proxy-service.h"
#include "yts-roster.h"

#include <string.h>
#include <telepathy-ytstenut-glib/telepathy-ytstenut-glib.h>

static void yts_roster_dispose (GObject *object);
static void yts_roster_finalize (GObject *object);
static void yts_roster_constructed (GObject *object);
static void yts_roster_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec);
static void yts_roster_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec);

G_DEFINE_TYPE (YtsRoster, yts_roster, G_TYPE_OBJECT);

#define YTS_ROSTER_GET_PRIVATE(o) \
(G_TYPE_INSTANCE_GET_PRIVATE ((o), YTS_TYPE_ROSTER, YtsRosterPrivate))

struct _YtsRosterPrivate
{
  GHashTable *contacts; /* hash of YtsContact this roster holds */
  YtsClient *client;   /* back-reference to the client object that owns us */

  guint disposed : 1;
};

enum
{
  CONTACT_ADDED,
  CONTACT_REMOVED,

  SERVICE_ADDED,
  SERVICE_REMOVED,

  N_SIGNALS,
};

enum
{
  PROP_0,
  PROP_CLIENT,
};

static guint signals[N_SIGNALS] = {0};

static void
yts_roster_class_init (YtsRosterClass *klass)
{
  GParamSpec   *pspec;
  GObjectClass *object_class = (GObjectClass *)klass;

  g_type_class_add_private (klass, sizeof (YtsRosterPrivate));

  object_class->dispose      = yts_roster_dispose;
  object_class->finalize     = yts_roster_finalize;
  object_class->constructed  = yts_roster_constructed;
  object_class->get_property = yts_roster_get_property;
  object_class->set_property = yts_roster_set_property;

  /**
   * YtsRoster:client:
   *
   * #YtsClient this roster represents
   */
  pspec = g_param_spec_object ("client",
                               "YtsClient",
                               "YtsClient",
                               YTS_TYPE_CLIENT,
                               G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_CLIENT, pspec);

  /**
   * YtsRoster::contact-added
   * @roster: #YtsRoster object which emitted the signal,
   * @contact: #YtsContact that was added.
   *
   * Emitted when contact is added to the roster.
   *
   * Since: 0.1
   */
  signals[CONTACT_ADDED] =
    g_signal_new (I_("contact-added"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  yts_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  YTS_TYPE_CONTACT);

  /**
   * YtsRoster::contact-removed
   * @roster: #YtsRoster object which emitted the signal,
   * @contact: #YtsContact that was removed.
   *
   * Emitted when contact is removed from the roster. Applications that
   * connected signal handlers to the contact, should disconnect them when this
   * signal is emitted.
   *
   * Since: 0.1
   */
  signals[CONTACT_REMOVED] =
    g_signal_new (I_("contact-removed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  yts_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  YTS_TYPE_CONTACT);

  /**
   * YtsRoster::service-added
   * @roster: #YtsRoster object which emitted the signal,
   * @service: #YtsService that was added.
   *
   * Emitted when service is added to the roster.
   *
   * Since: 0.1
   */
  signals[SERVICE_ADDED] =
    g_signal_new (I_("service-added"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  yts_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  YTS_TYPE_SERVICE);

  /**
   * YtsRoster::service-removed
   * @roster: #YtsRoster object which emitted the signal,
   * @service: #YtsService that was removed.
   *
   * Emitted when service is removed from the roster. Applications that
   * connected signal handlers to the service, should disconnect them when this
   * signal is emitted.
   *
   * Since: 0.1
   */
  signals[SERVICE_REMOVED] =
    g_signal_new (I_("service-removed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  yts_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  YTS_TYPE_SERVICE);
}

static void
yts_roster_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (yts_roster_parent_class)->constructed)
    G_OBJECT_CLASS (yts_roster_parent_class)->constructed (object);
}

static void
yts_roster_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
yts_roster_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  YtsRoster        *self = (YtsRoster*) object;
  YtsRosterPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_CLIENT:
      priv->client = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
yts_roster_init (YtsRoster *self)
{
  self->priv = YTS_ROSTER_GET_PRIVATE (self);

  self->priv->contacts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
}

static void
yts_roster_dispose (GObject *object)
{
  YtsRoster        *self = (YtsRoster*) object;
  YtsRosterPrivate *priv = self->priv;

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  priv->client = NULL;

  g_hash_table_destroy (priv->contacts);

  G_OBJECT_CLASS (yts_roster_parent_class)->dispose (object);
}

static void
yts_roster_finalize (GObject *object)
{
  G_OBJECT_CLASS (yts_roster_parent_class)->finalize (object);
}


/**
 * yts_roster_get_contacts:
 * @roster: #YtsRoster
 *
 * Returns contacts in this #YtsRoster.
 *
 * Return value: (transfer none): #GHashTable of #YtsContact; the hash table is
 * owned by the roster and must not be modified or freed by the caller.
 */
GHashTable *
yts_roster_get_contacts (YtsRoster *roster)
{
  YtsRosterPrivate *priv;

  g_return_val_if_fail (YTS_IS_ROSTER (roster), NULL);

  priv = roster->priv;

  return priv->contacts;
}

/*
 * yts_roster_remove_service_by_id:
 * @roster: #YtsRoster
 * @jid: JID of the contact that the service is running on
 * @uid: the service UID.
 *
 * Removes service from a roster and emits YtsRoster::service-removed signal.
 *
 * For use by #YtsClient.
 */
void
_yts_roster_remove_service_by_id (YtsRoster *roster,
                                   const char *jid,
                                   const char *uid)
{
  YtsRosterPrivate *priv;
  YtsContact       *contact;
  gboolean           emit = FALSE;

  g_return_if_fail (YTS_IS_ROSTER (roster));

  priv = roster->priv;

  if (!(contact = (YtsContact*)yts_roster_find_contact_by_jid (roster, jid)))
    {
      g_warning ("Contact for service not found");
      return;
    }

  _yts_contact_remove_service_by_uid (contact, uid);

  emit = _yts_contact_is_empty (contact);

  if (emit)
    {
      g_object_ref (contact);
      g_hash_table_remove (priv->contacts, jid);
      g_signal_emit (roster, signals[CONTACT_REMOVED], 0, contact);
      g_object_unref (contact);
    }
}

/*
 * yts_roster_find_contact_by_handle:
 * @roster: #YtsRoster
 * @handle: handle of this contact
 *
 * Finds contact in a roster.
 *
 * Return value: (transfer none): #YtsContact if found, or %NULL.
 */
YtsContact *
_yts_roster_find_contact_by_handle (YtsRoster *roster, guint handle)
{
  YtsRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_val_if_fail (YTS_IS_ROSTER (roster), NULL);

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsContact *contact    = value;
      TpContact   *tp_contact = yts_contact_get_tp_contact (contact);
      guint        h          = tp_contact_get_handle (tp_contact);

      if (h == handle)
        {
          return contact;
        }
    }

  return NULL;
}

/**
 * yts_roster_find_contact_by_jid:
 * @roster: #YtsRoster
 * @jid: jid of this contact
 *
 * Finds contact in a roster.
 *
 * Return value: (transfer none): #YtsContact if found, or %NULL.
 */
YtsContact *
yts_roster_find_contact_by_jid (YtsRoster *roster, const char *jid)
{
  YtsRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_val_if_fail (YTS_IS_ROSTER (roster) && jid, NULL);

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsContact *contact = value;
      const gchar *j       = key;

      if (j && !strcmp (j, jid))
        {
          return contact;
        }
    }

  return NULL;
}

/*
 * yts_roster_clear:
 * @roster: #YtsRoster
 *
 * Removes all contacts from the roster; for each contact removed, the
 * contact-removed signal is emitted and the contact's dispose method is
 * forecefully run.
 */
void
_yts_roster_clear (YtsRoster *roster)
{
  YtsRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_if_fail (YTS_IS_ROSTER (roster));

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsContact *contact = value;

      g_object_ref (contact);

      g_hash_table_iter_remove (&iter);

      g_signal_emit (roster, signals[CONTACT_REMOVED], 0, contact);
      g_object_run_dispose ((GObject*)contact);

      g_object_unref (contact);
    }
}

/**
 * yts_roster_find_contact_by_capability:
 * @roster: #YtsRoster
 * @capability: capability of this contact
 *
 * Finds first contact in roster that advertises the specified capablity.
 *
 * Return value: (transfer none): #YtsContact if found, or %NULL.
 */
YtsContact *
yts_roster_find_contact_by_capability (YtsRoster *roster,
                                        YtsCaps    capability)
{
  YtsRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_val_if_fail (YTS_IS_ROSTER (roster), NULL);
  g_return_val_if_fail (capability, NULL);

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsContact *contact = value;

      if (yts_contact_has_capability (contact, capability))
        {
          return contact;
        }
    }

  return NULL;
}

YtsRoster*
_yts_roster_new (YtsClient *client)
{
  return g_object_new (YTS_TYPE_ROSTER,
                       "client", client,
                       NULL);
}

/**
 * yts_roster_get_client:
 * @roster: #YtsRoster
 *
 * Retrieves the #YtsClient associated with this roster; the client object
 * must not be freed by the caller.
 *
 * Return value (transfer none): #YtsClient.
 */
YtsClient*
yts_roster_get_client (YtsRoster *roster)
{
  YtsRosterPrivate *priv;

  g_return_val_if_fail (YTS_IS_ROSTER (roster), NULL);

  priv = roster->priv;

  return priv->client;
}

static void
yts_roster_contact_service_removed_cb (YtsContact *contact,
                                        YtsService *service,
                                        YtsRoster  *roster)
{
  g_signal_emit (roster, signals[SERVICE_REMOVED], 0, service);
}

static void
yts_roster_contact_service_added_cb (YtsContact *contact,
                                      YtsService *service,
                                      YtsRoster  *roster)
{
  g_signal_emit (roster, signals[SERVICE_ADDED], 0, service);
}

/* FIXME this should probably go into some sort of factory.
 * Then we'll probably also use some smarter lookup algorithm. */
static YtsService *
create_service (YtsRoster   *self,
                YtsContact  *contact,
                char const   *sid,
                char const   *type,
                char const  **caps,
                GHashTable   *names)
{
  static char const *known_caps[] = {
    "org.freedesktop.ytstenut.VideoProfile.Content",
    "org.freedesktop.ytstenut.VideoProfile.Player",
    "org.freedesktop.ytstenut.VideoProfile.Transcript",
    "org.freedesktop.ytstenut.VideoProfile.Transfer",
    NULL
  };

  // TODO: publish implemented profiles in "names"?
  if (caps && *caps)
    {
      char const *capability = *caps;
      unsigned int i;

      for (i = 0; known_caps[i] != NULL; i++)
        {
          if (0 == g_strcmp0 (capability, known_caps[i]))
            {
              return yts_proxy_service_new (contact, sid, type, caps, names);
            }
        }
    }

  return _yts_metadata_service_new (contact, sid, type, caps, names);
}

void
_yts_roster_add_service (YtsRoster  *roster,
                          const char  *jid,
                          const char  *sid,
                          const char  *type,
                          const char **caps,
                          GHashTable  *names)
{
  YtsRosterPrivate *priv;
  YtsContact       *contact;
  YtsService       *service;

  g_return_if_fail (YTS_IS_ROSTER (roster));

  priv = roster->priv;

  if (!(contact = (YtsContact*)yts_roster_find_contact_by_jid (roster, jid)))
    {
      YTS_NOTE (ROSTER, "Creating new contact for %s", jid);

      contact = _yts_contact_new (priv->client, jid);

      g_signal_connect (contact, "service-added",
                        G_CALLBACK (yts_roster_contact_service_added_cb),
                        roster);
      g_signal_connect (contact, "service-removed",
                        G_CALLBACK (yts_roster_contact_service_removed_cb),
                        roster);

      g_hash_table_insert (priv->contacts, g_strdup (jid), contact);

      YTS_NOTE (ROSTER, "Emitting contact-added for new contact %s", jid);
      g_signal_emit (roster, signals[CONTACT_ADDED], 0, contact);
    }

  YTS_NOTE (ROSTER, "Adding service %s:%s", jid, sid);

  service = create_service (roster, contact, sid, type, caps, names);

  _yts_contact_add_service (contact, service);

  g_object_unref (service);
}
