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
 * Authored by: Rob Staudinger <robsta@linux.intel.com>
 */

#include <stdbool.h>
#include "yts-capability.h"
#include "yts-marshal.h"
#include "yts-proxy.h"

static void
_capability_interface_init (YtsCapability *interface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (YtsProxy,
                                  yts_proxy,
                                  G_TYPE_OBJECT,
                                  G_IMPLEMENT_INTERFACE (YTS_TYPE_CAPABILITY,
                                                         _capability_interface_init))

enum {
  PROP_0,
  PROP_CAPABILITY_FQC_IDS
};

enum {
  INVOKE_SERVICE_SIGNAL,
  SERVICE_EVENT_SIGNAL,
  SERVICE_RESPONSE_SIGNAL,
  N_SIGNALS
};

static guint _signals[N_SIGNALS] = { 0, };

/*
 * YtsCapability implementation
 */

static void
_capability_interface_init (YtsCapability *interface)
{
  /* Nothing to do, it's just about overriding the "fqc-ids" property,
   * which has to be done in the concrete subclass of the Proxy. */
}

/*
 * YtsProxy
 */

static void
_get_property (GObject      *object,
               unsigned int  property_id,
               GValue       *value,
               GParamSpec   *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
_set_property (GObject      *object,
               unsigned int  property_id,
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
  G_OBJECT_CLASS (yts_proxy_parent_class)->dispose (object);
}

static void
yts_proxy_class_init (YtsProxyClass *klass)
{
  GObjectClass  *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;
  object_class->dispose = _dispose;

  /* YtsCapability, needs to be overridden. */

  g_object_class_override_property (object_class,
                                    PROP_CAPABILITY_FQC_IDS,
                                    "fqc-ids");

  /* Signals */

  _signals[INVOKE_SERVICE_SIGNAL] =
                  g_signal_new ("invoke-service",
                                YTS_TYPE_PROXY,
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                yts_marshal_VOID__STRING_STRING_BOXED,
                                G_TYPE_NONE, 3,
                                G_TYPE_STRING, G_TYPE_STRING, G_TYPE_VARIANT);

  _signals[SERVICE_EVENT_SIGNAL] =
                                g_signal_new ("service-event",
                                              YTS_TYPE_PROXY,
                                              G_SIGNAL_RUN_LAST,
                                              G_STRUCT_OFFSET (YtsProxyClass,
                                                               service_event),
                                              NULL, NULL,
                                              yts_marshal_VOID__STRING_BOXED,
                                              G_TYPE_NONE, 2,
                                              G_TYPE_STRING, G_TYPE_VARIANT);

  _signals[SERVICE_RESPONSE_SIGNAL] =
                              g_signal_new ("service-response",
                                            YTS_TYPE_PROXY,
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (YtsProxyClass,
                                                             service_response),
                                            NULL, NULL,
                                            yts_marshal_VOID__STRING_BOXED,
                                            G_TYPE_NONE, 2,
                                            G_TYPE_STRING, G_TYPE_VARIANT);
}

static void
yts_proxy_init (YtsProxy *self)
{
}

char *
yts_proxy_get_fqc_id (YtsProxy *self)
{
  char **fqc_ids;
  char  *fqc_id;

  g_return_val_if_fail (YTS_IS_PROXY (self), NULL);

  /* Get it from the subclass. */
  fqc_ids = yts_capability_get_fqc_ids (YTS_CAPABILITY (self));

  /* A Proxy can only ever have a single fqc-id. */
  g_return_val_if_fail (fqc_ids, NULL);
  g_return_val_if_fail (fqc_ids[0], NULL);
  g_return_val_if_fail (fqc_ids[1] == NULL, NULL);

  /* PONDERING, maybe just g_free the array and return the first element. */
  fqc_id = g_strdup (fqc_ids[0]);
  g_strfreev (fqc_ids);

  return fqc_id;
}

char *
yts_proxy_create_invocation_id (YtsProxy *self)
{
  static GRand  *_rand = NULL;
  char          *invocation_id;

  /* PONDERING: introduce a typedef for the invocation-id and GSlice it. */

  if (_rand == NULL) {
    _rand = g_rand_new ();
  }

  invocation_id = g_strdup_printf ("%x", g_rand_int (_rand));

  return invocation_id;
}

void
yts_proxy_invoke (YtsProxy  *self,
                   char const *invocation_id,
                   char const *aspect,
                   GVariant   *arguments)
{
  g_return_if_fail (YTS_IS_PROXY (self));
  g_return_if_fail (invocation_id);
  g_return_if_fail (aspect);

  g_signal_emit (self, _signals[INVOKE_SERVICE_SIGNAL], 0,
                 invocation_id,
                 aspect,
                 arguments);

  /* This is a bit hackish, ok, but it allows for creating the variant
   * in the invocation of this function. */
  if (arguments &&
      g_variant_is_floating (arguments)) {
    g_variant_unref (arguments);
  }
}

void
yts_proxy_handle_service_event (YtsProxy  *self,
                                 char const *aspect,
                                 GVariant   *arguments)
{
  g_return_if_fail (YTS_IS_PROXY (self));
  g_return_if_fail (aspect);

  g_signal_emit (self, _signals[SERVICE_EVENT_SIGNAL], 0,
                 aspect,
                 arguments);

  /* This is a bit hackish, ok, but it allows for creating the variant
   * in the invocation of this function. */
  if (arguments &&
      g_variant_is_floating (arguments)) {
    g_variant_unref (arguments);
  }
}

void
yts_proxy_handle_service_response (YtsProxy   *self,
                                    char const  *invocation_id,
                                    GVariant    *response)
{
  g_return_if_fail (YTS_IS_PROXY (self));

  g_signal_emit (self, _signals[SERVICE_RESPONSE_SIGNAL], 0,
                 invocation_id,
                 response);

  /* This is a bit hackish, ok, but it allows for creating the variant
   * in the invocation of this function. */
  if (response &&
      g_variant_is_floating (response)) {
    g_variant_unref (response);
  }
}
