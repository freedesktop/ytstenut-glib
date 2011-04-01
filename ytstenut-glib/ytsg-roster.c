/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2011 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
 *
 * This library is free software; you can redistribute it and/or
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
 */

#include "ytsg-client.h"
#include "ytsg-contact.h"
#include "ytsg-marshal.h"
#include "ytsg-private.h"
#include "ytsg-roster.h"

#include <string.h>
#include <telepathy-ytstenut-glib/telepathy-ytstenut-glib.h>

static void ytsg_roster_dispose (GObject *object);
static void ytsg_roster_finalize (GObject *object);
static void ytsg_roster_constructed (GObject *object);
static void ytsg_roster_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec);
static void ytsg_roster_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec);

G_DEFINE_TYPE (YtsgRoster, ytsg_roster, G_TYPE_OBJECT);

#define YTSG_ROSTER_GET_PRIVATE(o) \
(G_TYPE_INSTANCE_GET_PRIVATE ((o), YTSG_TYPE_ROSTER, YtsgRosterPrivate))

struct _YtsgRosterPrivate
{
  GHashTable *contacts; /* hash of YtsgContact this roster holds */
  YtsgClient *client;   /* back-reference to the client object that owns us */

  guint disposed : 1;
};

enum
{
  CONTACT_ADDED,
  CONTACT_REMOVED,

  N_SIGNALS,
};

enum
{
  PROP_0,
  PROP_CLIENT,
};

static guint signals[N_SIGNALS] = {0};

static void
ytsg_roster_class_init (YtsgRosterClass *klass)
{
  GParamSpec   *pspec;
  GObjectClass *object_class = (GObjectClass *)klass;

  g_type_class_add_private (klass, sizeof (YtsgRosterPrivate));

  object_class->dispose      = ytsg_roster_dispose;
  object_class->finalize     = ytsg_roster_finalize;
  object_class->constructed  = ytsg_roster_constructed;
  object_class->get_property = ytsg_roster_get_property;
  object_class->set_property = ytsg_roster_set_property;

  /**
   * YtsgRoster:client:
   *
   * #YtsgClient this roster represents
   */
  pspec = g_param_spec_object ("client",
                               "YtsgClient",
                               "YtsgClient",
                               YTSG_TYPE_CLIENT,
                               G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_CLIENT, pspec);

  /**
   * YtsgRoster::contact-added
   * @roster: #YtsgRoster object which emitted the signal,
   * @contact: #YtsgContact that was added.
   *
   * Since: 0.1
   */
  signals[CONTACT_ADDED] =
    g_signal_new (I_("contact-added"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  ytsg_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  YTSG_TYPE_CONTACT);

  /**
   * YtsgRoster::contact-removed
   * @roster: #YtsgRoster object which emitted the signal,
   * @contact: #YtsgContact that was removed.
   *
   * Applications that connected signal handlers to the contact, should disconnect
   * them when this signal is emitted.
   *
   * Since: 0.1
   */
  signals[CONTACT_REMOVED] =
    g_signal_new (I_("contact-removed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  ytsg_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  YTSG_TYPE_CONTACT);
}

static void
ytsg_roster_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (ytsg_roster_parent_class)->constructed)
    G_OBJECT_CLASS (ytsg_roster_parent_class)->constructed (object);
}

static void
ytsg_roster_get_property (GObject    *object,
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
ytsg_roster_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  YtsgRoster        *self = (YtsgRoster*) object;
  YtsgRosterPrivate *priv = self->priv;

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
ytsg_roster_init (YtsgRoster *self)
{
  self->priv = YTSG_ROSTER_GET_PRIVATE (self);

  self->priv->contacts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                NULL, g_object_unref);
}

static void
ytsg_roster_dispose (GObject *object)
{
  YtsgRoster        *self = (YtsgRoster*) object;
  YtsgRosterPrivate *priv = self->priv;

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  priv->client = NULL;

  g_hash_table_destroy (priv->contacts);

  G_OBJECT_CLASS (ytsg_roster_parent_class)->dispose (object);
}

static void
ytsg_roster_finalize (GObject *object)
{
  G_OBJECT_CLASS (ytsg_roster_parent_class)->finalize (object);
}


/**
 * ytsg_roster_get_contacts:
 * @roster: #YtsgRoster
 *
 * Returns contacts in this #YtsgRoster.
 *
 * Return value: (transfer none): #GHashTable of #YtsgContact; the hash table is
 * owned by the roster and must not be modified or freed by the caller.
 */
GHashTable *
ytsg_roster_get_contacts (YtsgRoster *roster)
{
  YtsgRosterPrivate *priv;

  g_return_val_if_fail (YTSG_IS_ROSTER (roster), NULL);

  priv = roster->priv;

  return priv->contacts;
}

/*
 * ytsg_roster_add_service:
 * @roster: #YtsgRoster
 * @service: #YtsgService
 *
 * Adds service to a roster and emits YtsgRoster::service-added signal, assuming
 * reference on the #YtsgService object.
 *
 * For use by #YtsgClient.
 */
void
_ytsg_roster_add_service (YtsgRoster *roster, YtsgService *service)
{
  YtsgRosterPrivate *priv;
  const char        *jid;
  const char        *uid;
  YtsgContact       *contact;
  gboolean           emit = FALSE;

  g_return_if_fail (YTSG_IS_ROSTER (roster));
  g_return_if_fail (YTSG_IS_SERVICE (service));

  priv = roster->priv;

  jid = ytsg_service_get_jid (service);
  uid = ytsg_service_get_uid (service);

  if (!(contact = (YtsgContact*)ytsg_roster_find_contact_by_jid (roster, jid)))
    {
      /*
        FIXME
        contact = _ytsg_contact_new (YtsgClient *client, TpContact *tp_contact);
        */

      g_hash_table_insert (priv->contacts, (char*)jid, contact);

      emit = TRUE;
    }

  _ytsg_contact_add_service (contact, service);

  if (emit)
    g_signal_emit (roster, signals[CONTACT_ADDED], 0, contact);
}

/*
 * ytsg_roster_remove_service:
 * @roster: #YtsgRoster
 * @service: #YtsgService
 * @dispose: if %TRUE, forecefully runs dispose on the #YtsgService object
 *
 * Removes service from a roster and emits YtsgRoster::service-removed signal;
 * if dispose is %TRUE, runs the service's dispose method.
 *
 * For use by #YtsgClient.
 */
void
_ytsg_roster_remove_service (YtsgRoster  *roster,
                             YtsgService *service,
                             gboolean     dispose)
{
  YtsgRosterPrivate *priv;
  const char        *jid;
  const char        *uid;
  YtsgContact       *contact;
  gboolean           emit = FALSE;

  g_return_if_fail (YTSG_IS_ROSTER (roster));
  g_return_if_fail (YTSG_IS_SERVICE (service));

  priv = roster->priv;

  g_object_ref (service);

  jid = ytsg_service_get_jid (service);
  uid = ytsg_service_get_uid (service);

  if (!(contact = (YtsgContact*)ytsg_roster_find_contact_by_jid (roster, jid)))
    {
      g_warning ("Contact for service not found");
      return;
    }

  _ytsg_contact_remove_service (contact, service);

  emit = _ytsg_contact_is_empty (contact);

  g_object_unref (service);

  if (emit)
    {
      g_object_ref (contact);
      g_hash_table_remove (priv->contacts, jid);
      g_signal_emit (roster, signals[CONTACT_REMOVED], 0, contact);
      g_object_unref (contact);
    }
}

/*
 * ytsg_roster_remove_contact_by_handle:
 * @roster: #YtsgRoster
 * @handle: handle of this contact
 *
 * Removes contact from a roster and emits YtsgRoster::contact-removed signal;
 * once the the signal emission is finished, this function will forecefully run
 * the the contacts dispose method to ensure that that all TP resources are
 * released.
 *
 * For use by #YtsgClient.
 */
void
_ytsg_roster_remove_contact_by_handle (YtsgRoster *roster, guint handle)
{
  YtsgRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_if_fail (YTSG_IS_ROSTER (roster));

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsgContact *contact    = value;
      TpContact   *tp_contact = ytsg_contact_get_tp_contact (contact);
      guint        h          = tp_contact_get_handle (tp_contact);

      if (h == handle)
        {
          const char *jid = ytsg_contact_get_jid (contact);

          g_object_ref (contact);

          g_hash_table_remove (priv->contacts, jid);

          g_signal_emit (roster, signals[CONTACT_REMOVED], 0, contact);

          g_object_run_dispose ((GObject*)contact);

          g_object_unref (contact);
          return;
        }

    }

  g_warning (G_STRLOC " contact with handle %d not in roster %p",
             handle, roster);
}

/*
 * ytsg_roster_find_contact_by_handle:
 * @roster: #YtsgRoster
 * @handle: handle of this contact
 *
 * Finds contact in a roster.
 *
 * Return value: (transfer none): #YtsgContact if found, or %NULL.
 */
YtsgContact *
_ytsg_roster_find_contact_by_handle (YtsgRoster *roster, guint handle)
{
  YtsgRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_val_if_fail (YTSG_IS_ROSTER (roster), NULL);

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsgContact *contact    = value;
      TpContact   *tp_contact = ytsg_contact_get_tp_contact (contact);
      guint        h          = tp_contact_get_handle (tp_contact);

      if (h == handle)
        {
          return contact;
        }
    }

  return NULL;
}

/**
 * ytsg_roster_find_contact_by_jid:
 * @roster: #YtsgRoster
 * @jid: jid of this contact
 *
 * Finds contact in a roster.
 *
 * Return value: (transfer none): #YtsgContact if found, or %NULL.
 */
const YtsgContact *
ytsg_roster_find_contact_by_jid (YtsgRoster *roster, const char *jid)
{
  YtsgRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_val_if_fail (YTSG_IS_ROSTER (roster) && jid, NULL);

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsgContact *contact = value;
      const gchar *j       = ytsg_contact_get_jid (contact);

      if (j && !strcmp (j, jid))
        {
          return contact;
        }
    }

  return NULL;
}

/*
 * ytsg_roster_contains_contact:
 * @roster: #YtsgRoster
 * @contact: #YtsgContact
 *
 * Checks if roster contains given contact.
 *
 * Return value: (transfer none): %TRUE if the contact is in the roster.
 */
gboolean
_ytsg_roster_contains_contact (YtsgRoster *roster, const YtsgContact *contact)
{
  YtsgRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_val_if_fail (YTSG_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (YTSG_IS_CONTACT (contact), FALSE);

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsgContact *c = value;

      if (c == contact)
        {
          return TRUE;
        }
    }

  return FALSE;
}

/*
 * ytsg_roster_clear:
 * @roster: #YtsgRoster
 *
 * Removes all contacts from the roster; for each contact removed, the
 * contact-removed signal is emitted and the contact's dispose method is
 * forecefully run.
 */
void
_ytsg_roster_clear (YtsgRoster *roster)
{
  YtsgRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_if_fail (YTSG_IS_ROSTER (roster));

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsgContact *contact = value;

      g_object_ref (contact);

      g_hash_table_iter_remove (&iter);

      g_signal_emit (roster, signals[CONTACT_REMOVED], 0, contact);
      g_object_run_dispose ((GObject*)contact);

      g_object_unref (contact);
    }
}

/**
 * ytsg_roster_find_contact_by_capability:
 * @roster: #YtsgRoster
 * @capability: capability of this contact
 *
 * Finds first contact in roster that advertises the specified capablity.
 *
 * Return value: (transfer none): #YtsgContact if found, or %NULL.
 */
const YtsgContact *
ytsg_roster_find_contact_by_capability (YtsgRoster *roster,
                                        YtsgCaps    capability)
{
  YtsgRosterPrivate *priv;
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_val_if_fail (YTSG_IS_ROSTER (roster), NULL);
  g_return_val_if_fail (capability, NULL);

  priv = roster->priv;

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsgContact *contact = value;

      if (ytsg_contact_has_capability (contact, capability))
        {
          return contact;
        }
    }

  return NULL;
}

YtsgRoster*
_ytsg_roster_new (YtsgClient *client)
{
  return g_object_new (YTSG_TYPE_ROSTER,
                       "client", client,
                       NULL);
}

YtsgClient*
ytsg_roster_get_client (YtsgRoster *roster)
{
  YtsgRosterPrivate *priv;

  g_return_val_if_fail (YTSG_IS_ROSTER (roster), NULL);

  priv = roster->priv;

  return priv->client;
}
