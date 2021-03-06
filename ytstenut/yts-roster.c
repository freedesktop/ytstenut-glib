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

#include <gio/gio.h>
#include <telepathy-ytstenut-glib/telepathy-ytstenut-glib.h>

#include "ytstenut-internal.h"
#include "yts-contact-impl.h"
#include "yts-contact-internal.h"
#include "yts-marshal.h"
#include "yts-metadata.h"
#include "yts-outgoing-file.h"
#include "yts-roster-impl.h"
#include "yts-roster-internal.h"
#include "yts-service-factory.h"

G_DEFINE_ABSTRACT_TYPE (YtsRoster, yts_roster, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), YTS_TYPE_ROSTER, YtsRosterPrivate))

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN PACKAGE"\0roster\0"G_STRLOC

/**
 * SECTION: yts-roster
 * @short_description: Represents a roster of devices and services
 * connected to the Ytstenut application mesh.
 *
 * #YtsRoster represents all known devices and services in the Ytstenut
 * application mesh.
 */

enum {
  PROP_0
};

enum {
  SIG_CONTACT_ADDED,
  SIG_CONTACT_REMOVED,

  SIG_SERVICE_ADDED,
  SIG_SERVICE_REMOVED,

  N_SIGNALS
};

typedef struct {
    gchar *contact_id;
    gchar *service_id;
    gchar *fqc_id;
} StatusTuple;

static StatusTuple *
status_tuple_new (const gchar *contact_id,
    const gchar *service_id,
    const gchar *fqc_id)
{
  StatusTuple *st = g_slice_new0 (StatusTuple);

  st->contact_id = g_strdup (contact_id);
  st->service_id = g_strdup (service_id);
  st->fqc_id = g_strdup (fqc_id);
  return st;
}

static void
status_tuple_free (gpointer p)
{
  StatusTuple *st = p;

  g_free (st->contact_id);
  g_free (st->service_id);
  g_free (st->fqc_id);
  g_slice_free (StatusTuple, st);
}

static guint
status_tuple_hash (gconstpointer v)
{
  const StatusTuple *st = v;

  return g_str_hash (st->contact_id) +
    g_str_hash (st->service_id) * 2 +
    g_str_hash (st->fqc_id) * 3;
}

static gboolean
status_tuple_equal (gconstpointer a,
    gconstpointer b)
{
  const StatusTuple *a_ = a;
  const StatusTuple *b_ = b;

  return g_str_equal (a_->contact_id, b_->contact_id) &&
    g_str_equal (a_->service_id, b_->service_id) &&
    g_str_equal (a_->fqc_id, b_->fqc_id);
}

typedef struct {

  GHashTable *contacts; /* hash of YtsContact this roster holds */

  /* StatusTuple => status XML */
  GHashTable *deferred_statuses;

} YtsRosterPrivate;

static unsigned _signals[N_SIGNALS] = { 0, };

static void
_contact_send_message (YtsContact   *contact,
                       YtsService   *service,
                       YtsMetadata  *message,
                       YtsRoster    *self)
{
  /* This is a bit of a hack, we require the non-abstract subclass to
   * implement this interface. */
  yts_roster_impl_send_message (YTS_ROSTER_IMPL (self),
                                contact,
                                service,
                                message);
}

static YtsOutgoingFile *
_contact_send_file (YtsContact   *contact,
                    YtsService   *service,
                    GFile        *file,
                    char const   *description,
                    GError      **error_out,
                    YtsRoster    *self)
{
  /* This is a bit of a hack, we require the non-abstract subclass to
   * implement this interface. */
  return yts_roster_impl_send_file (YTS_ROSTER_IMPL (self),
                                    contact,
                                    service,
                                    file,
                                    description,
                                    error_out);
}

static void
_get_property (GObject    *object,
               unsigned    property_id,
               GValue     *value,
               GParamSpec *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
_set_property (GObject      *object,
               unsigned      property_id,
               const GValue *value,
               GParamSpec   *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
_dispose (GObject *object)
{
  YtsRosterPrivate *priv = GET_PRIVATE (object);

  if (priv->contacts) {

    GHashTableIter iter;
    char const *contact_id;
    YtsContact *contact;

    g_hash_table_iter_init (&iter, priv->contacts);
    while (g_hash_table_iter_next (&iter,
                                   (void **) &contact_id,
                                   (void **) &contact)) {

      g_signal_handlers_disconnect_by_func (contact,
                                            _contact_send_message,
                                            object);
      g_signal_handlers_disconnect_by_func (contact,
                                            _contact_send_file,
                                            object);
    }

    g_hash_table_destroy (priv->contacts);
    priv->contacts = NULL;
  }

  if (priv->deferred_statuses)
    {
      g_hash_table_unref (priv->deferred_statuses);
      priv->deferred_statuses = NULL;
    }

  G_OBJECT_CLASS (yts_roster_parent_class)->dispose (object);
}

static void
yts_roster_class_init (YtsRosterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (YtsRosterPrivate));

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;
  object_class->dispose = _dispose;

  /**
   * YtsRoster::contact-added:
   * @self: object which emitted the signal.
   * @contact: #YtsContact that was added.
   *
   * Emitted when contact is added to the roster.
   *
   * Since: 0.1
   */
  _signals[SIG_CONTACT_ADDED] = g_signal_new ("contact-added",
                                              G_TYPE_FROM_CLASS (object_class),
                                              G_SIGNAL_RUN_LAST,
                                              0, NULL, NULL,
                                              yts_marshal_VOID__OBJECT,
                                              G_TYPE_NONE, 1,
                                              YTS_TYPE_CONTACT);

  /**
   * YtsRoster::contact-removed:
   * @self: object which emitted the signal.
   * @contact: #YtsContact that was removed.
   *
   * Emitted when contact is removed from the roster. Applications that
   * connected signal handlers to the contact, should disconnect them when this
   * signal is emitted.
   *
   * Since: 0.1
   */
  _signals[SIG_CONTACT_REMOVED] = g_signal_new ("contact-removed",
                                                G_TYPE_FROM_CLASS (object_class),
                                                G_SIGNAL_RUN_LAST,
                                                0, NULL, NULL,
                                                yts_marshal_VOID__OBJECT,
                                                G_TYPE_NONE, 1,
                                                YTS_TYPE_CONTACT);

  /**
   * YtsRoster::service-added:
   * @self: object which emitted the signal.
   * @service: #YtsService that was added.
   *
   * Emitted when service is added to the roster.
   *
   * Since: 0.1
   */
  _signals[SIG_SERVICE_ADDED] = g_signal_new ("service-added",
                                              G_TYPE_FROM_CLASS (object_class),
                                              G_SIGNAL_RUN_LAST,
                                              0, NULL, NULL,
                                              yts_marshal_VOID__OBJECT,
                                              G_TYPE_NONE, 1,
                                              YTS_TYPE_SERVICE);

  /**
   * YtsRoster::service-removed:
   * @self: object which emitted the signal.
   * @service: #YtsService that was removed.
   *
   * Emitted when service is removed from the roster. Applications that
   * connected signal handlers to the service, should disconnect them when this
   * signal is emitted.
   *
   * Since: 0.1
   */
  _signals[SIG_SERVICE_REMOVED] = g_signal_new ("service-removed",
                                                G_TYPE_FROM_CLASS (object_class),
                                                G_SIGNAL_RUN_LAST,
                                                0, NULL, NULL,
                                                yts_marshal_VOID__OBJECT,
                                                G_TYPE_NONE, 1,
                                                YTS_TYPE_SERVICE);
}

static void
yts_roster_init (YtsRoster *self)
{
  YtsRosterPrivate *priv = GET_PRIVATE (self);

  priv->contacts = g_hash_table_new_full (g_str_hash,
                                          g_str_equal,
                                          g_free,
                                          g_object_unref);

  priv->deferred_statuses = g_hash_table_new_full (status_tuple_hash,
      status_tuple_equal, status_tuple_free, g_free);
}

YtsService *const
yts_roster_find_service_by_id (YtsRoster  *self,
                               char const *contact_id,
                               char const *service_id)
{
  YtsContact  *contact;
  YtsService  *service = NULL;

  contact = yts_roster_find_contact_by_id (self, contact_id);
  if (contact) {
    service = yts_contact_find_service_by_id (contact, service_id);
  }

  return service;
}

/*
 * yts_roster_remove_service_by_id:
 * @self: object on which to invoke this method.
 * @contact_id: JID of the contact that the service is running on
 * @service_id: the service UID.
 *
 * Removes service from a roster and emits YtsRoster::service-removed signal.
 */
void
yts_roster_remove_service_by_id (YtsRoster  *self,
                                 char const *contact_id,
                                 char const *service_id)
{
  YtsRosterPrivate *priv = GET_PRIVATE (self);
  YtsContact       *contact;
  gboolean           emit = FALSE;

  g_return_if_fail (YTS_IS_ROSTER (self));

  contact = yts_roster_find_contact_by_id (self, contact_id);
  if (!contact) {
    g_critical ("Contact for service not found");
    return;
  }

  yts_contact_remove_service_by_id (contact, service_id);

  emit = yts_contact_is_empty (contact);
  if (emit) {
    g_signal_handlers_disconnect_by_func (contact,
                                          _contact_send_message,
                                          self);
    g_signal_handlers_disconnect_by_func (contact,
                                          _contact_send_file,
                                          self);
    g_object_ref (contact);
    g_hash_table_remove (priv->contacts, contact_id);
    g_signal_emit (self, _signals[SIG_CONTACT_REMOVED], 0, contact);
    g_object_unref (contact);
  }
}

/**
 * yts_roster_find_contact_by_id:
 * @self: object on which to invoke this method.
 * @contact_id: JID of this contact
 *
 * Finds contact in a roster.
 *
 * Returns: (transfer none): #YtsContact if found, or %NULL.
 */
YtsContact *const
yts_roster_find_contact_by_id (YtsRoster const  *self,
                               char const       *contact_id)
{
  YtsRosterPrivate *priv = GET_PRIVATE (self);

  g_return_val_if_fail (YTS_IS_ROSTER (self), NULL);
  g_return_val_if_fail (contact_id, NULL);

  return g_hash_table_lookup (priv->contacts, contact_id);
}

/*
 * yts_roster_clear:
 * @self: object on which to invoke this method.
 *
 * Removes all contacts from the roster; for each contact removed, the
 * contact-removed signal is emitted and the contact's dispose method is
 * forecefully run.
 */
void
yts_roster_clear (YtsRoster *self)
{
  YtsRosterPrivate *priv = GET_PRIVATE (self);
  GHashTableIter     iter;
  gpointer           key, value;

  g_return_if_fail (YTS_IS_ROSTER (self));

  // FIXME changing the hash while iterating seems not safe?!
  // also we can probably get rid of that run_dispose() once the
  // client referencing frenzy is no more.

  g_hash_table_iter_init (&iter, priv->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      YtsContact *contact = value;

      g_signal_handlers_disconnect_by_func (contact,
                                            _contact_send_message,
                                            self);
      g_signal_handlers_disconnect_by_func (contact,
                                            _contact_send_file,
                                            self);

      g_object_ref (contact);

      g_hash_table_iter_remove (&iter);

      g_signal_emit (self, _signals[SIG_CONTACT_REMOVED], 0, contact);
      g_object_run_dispose ((GObject*)contact);

      g_object_unref (contact);
    }
}

static void
yts_roster_contact_service_removed_cb (YtsContact *contact,
                                        YtsService *service,
                                        YtsRoster  *roster)
{
  g_signal_emit (roster, _signals[SIG_SERVICE_REMOVED], 0, service);
}

static void
yts_roster_contact_service_added_cb (YtsContact *contact,
                                      YtsService *service,
                                      YtsRoster  *roster)
{
  g_signal_emit (roster, _signals[SIG_SERVICE_ADDED], 0, service);
}

static void
_connection_get_contacts (TpConnection        *connection,
                            guint              n_contacts,
                            TpContact *const  *contacts,
                            const char *const *requested_ids,
                            GHashTable        *failed_id_errors,
                            const GError      *error,
                            gpointer           self_,
                            GObject           *service_)
{
  YtsRoster *self = YTS_ROSTER (self_);
  YtsRosterPrivate *priv = GET_PRIVATE (self);
  YtsService *service = YTS_SERVICE (service_);

  if (n_contacts == 0) {

    GError const *error;

    g_return_if_fail (requested_ids && requested_ids[0]);
    g_return_if_fail (failed_id_errors);

    error = g_hash_table_lookup (failed_id_errors, requested_ids[0]);
    g_critical ("%s : %s", G_STRLOC, error->message);

  } else {

    YtsContact *contact;
    char const *contact_id = requested_ids[0];
    TpContact *tp_contact = TP_CONTACT (contacts[0]);
    GHashTableIter iter;
    gpointer k, v;

    g_message ("Creating new contact for %s", contact_id);

    contact = yts_contact_impl_new (tp_contact);

    g_signal_connect (contact, "service-added",
                      G_CALLBACK (yts_roster_contact_service_added_cb),
                      self);
    g_signal_connect (contact, "service-removed",
                      G_CALLBACK (yts_roster_contact_service_removed_cb),
                      self);

    g_hash_table_insert (priv->contacts, g_strdup (contact_id), contact);

    g_message ("Emitting contact-added for new contact %s", contact_id);
    g_signal_emit (self, _signals[SIG_CONTACT_ADDED], 0, contact);

    g_signal_connect (contact, "send-message",
                      G_CALLBACK (_contact_send_message), self);
    g_signal_connect (contact, "send-file",
                      G_CALLBACK (_contact_send_file), self);

    yts_contact_add_service (contact, service);
    g_object_unref (service);

    /* Apply deferred status updates */

    g_hash_table_iter_init (&iter, priv->deferred_statuses);

    while (g_hash_table_iter_next (&iter, &k, &v))
      {
        StatusTuple *st = k;

        if (g_str_equal (st->contact_id, contact_id))
          {
            yts_contact_update_service_status (contact, st->service_id,
                st->fqc_id, v);
            g_hash_table_iter_remove (&iter);
          }
      }
  }
}

void
yts_roster_add_service (YtsRoster         *self,
                        TpConnection      *tp_connection,
                        char const        *contact_id,
                        char const        *service_id,
                        char const        *type,
                        char const *const *caps,
                        GHashTable        *names,
                        GHashTable        *statuses)
{
  YtsContact        *contact;
  YtsService        *service;
  YtsServiceFactory *factory = yts_service_factory_get_default ();

  DEBUG ("contact=%s, service=%s, type=%s", contact_id, service_id, type);

  service = yts_service_factory_create_service (factory,
                                                caps,
                                                service_id,
                                                type,
                                                names,
                                                statuses);

  contact = yts_roster_find_contact_by_id (self, contact_id);
  if (contact) {

    DEBUG ("we already have that contact");
    yts_contact_add_service (contact, service);

  } else {

    TpContactFeature const features[] = { TP_CONTACT_FEATURE_PRESENCE,
                                          TP_CONTACT_FEATURE_CONTACT_INFO,
                                          TP_CONTACT_FEATURE_AVATAR_DATA,
                                          TP_CONTACT_FEATURE_CAPABILITIES };

    DEBUG ("adding that contact later, when we get a TpContact");
    tp_connection_get_contacts_by_id (tp_connection,
                                      1,
                                      &contact_id,
                                      G_N_ELEMENTS (features),
                                      features,
                                      _connection_get_contacts,
                                      self,
                                      NULL,
                                      G_OBJECT (service));
  }
}

void
yts_roster_update_contact_status (YtsRoster   *self,
                                  char const  *contact_id,
                                  char const  *service_id,
                                  char const  *fqc_id,
                                  char const  *status_xml)
{
  YtsRosterPrivate *priv = GET_PRIVATE (self);
  YtsContact *contact;

  DEBUG ("contact=%s service=%s fqc=%s", contact_id, service_id, fqc_id);
  contact = g_hash_table_lookup (priv->contacts, contact_id);

  if (contact != NULL)
    {
      DEBUG ("updating service status straight away");
      yts_contact_update_service_status (contact, service_id, fqc_id,
          status_xml);
    }
  else
    {
      /* We've hit a race condition between the contact's status being
       * discovered, and the TpContact being set up.
       * Save the status and apply it when we get the contact.
       */
      DEBUG ("no contact yet, will update status when we have one");
      g_hash_table_insert (priv->deferred_statuses,
          status_tuple_new (contact_id, service_id, fqc_id),
          g_strdup (status_xml));
    }
}

/**
 * yts_roster_foreach_contact:
 * @self: object on which to invoke this method.
 * @iterator: iterator function.
 * @user_data: context to pass to the iterator function.
 *
 * Iterate over @self's contacts.
 *
 * Returns: <literal>true</literal> if all the contacts have been iterated.
 *
 * Since: 0.4
 */
bool
yts_roster_foreach_contact (YtsRoster                 *self,
                            YtsRosterContactIterator   iterator,
                            void                      *user_data)
{
  YtsRosterPrivate *priv = GET_PRIVATE (self);
  GHashTableIter   iter;
  char const      *contact_id;
  YtsContact      *contact;
  bool             ret = true;

  g_return_val_if_fail (YTS_IS_ROSTER (self), false);
  g_return_val_if_fail (iterator, false);

  g_hash_table_iter_init (&iter, priv->contacts);
  while (ret &&
         g_hash_table_iter_next (&iter,
                                 (void **) &contact_id,
                                 (void **) &contact)) {
    ret = iterator (self, contact_id, contact, user_data);
  }

  return ret;
}

