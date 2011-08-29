/*
 * Copyright (c) 2011 Intel Corp.
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

#include "ytsg-marshal.h"
#include "ytsg-service-adapter.h"

G_DEFINE_TYPE (YtsgServiceAdapter, ytsg_service_adapter, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_CAPABILITY,
  PROP_SERVICE
};

enum {
  ERROR_SIGNAL,
  EVENT_SIGNAL,
  RESPONSE_SIGNAL,

  N_SIGNALS
};

static unsigned int _signals[N_SIGNALS] = { 0, };

static GHashTable *
_collect_properties (YtsgServiceAdapter *self)
{
  g_critical ("%s : Method YtsgServiceAdapter.collect_properties() not implemented by %s",
              G_STRLOC,
              G_OBJECT_TYPE_NAME (self));

  return NULL;
}

static bool
_invoke (YtsgServiceAdapter *self,
         char const         *invocation_id,
         char const         *aspect,
         GVariant           *arguments)
{
  g_critical ("%s : Method YtsgServiceAdapter.invoke() not implemented by %s",
              G_STRLOC,
              G_OBJECT_TYPE_NAME (self));

  return false;
}

static void
_get_property (GObject      *object,
               unsigned int  property_id,
               GValue       *value,
               GParamSpec   *pspec)
{
  switch (property_id) {
    case PROP_CAPABILITY:
      g_value_set_string (value,
                          ytsg_service_adapter_get_capability (
                            YTSG_SERVICE_ADAPTER (object)));
      break;
    /* Other properties need to be implemented by the subclass. */
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

/* Stub, the properties need to be implemented by the subclass. */
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
ytsg_service_adapter_class_init (YtsgServiceAdapterClass *klass)
{
  GObjectClass  *object_class = G_OBJECT_CLASS (klass);
  GParamSpec    *pspec;

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;

  klass->collect_properties = _collect_properties;
  klass->invoke = _invoke;

  /* Properties */

  pspec = g_param_spec_string ("capability", "", "",
                               NULL,
                               G_PARAM_READABLE |
                               G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CAPABILITY, pspec);

  pspec = g_param_spec_object ("service", "", "",
                               G_TYPE_OBJECT,
                               G_PARAM_READWRITE |
                               G_PARAM_CONSTRUCT_ONLY |
                               G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SERVICE, pspec);

  /* Signals */

  _signals[ERROR_SIGNAL] = g_signal_new ("error",
                                         YTSG_TYPE_SERVICE_ADAPTER,
                                         G_SIGNAL_RUN_LAST,
                                         G_STRUCT_OFFSET (YtsgServiceAdapterClass,
                                                          error),
                                         NULL, NULL,
                                         ytsg_marshal_VOID__STRING_BOXED,
                                         G_TYPE_NONE, 2,
                                         G_TYPE_STRING, G_TYPE_ERROR);

  _signals[EVENT_SIGNAL] = g_signal_new ("event",
                                         YTSG_TYPE_SERVICE_ADAPTER,
                                         G_SIGNAL_RUN_LAST,
                                         G_STRUCT_OFFSET (YtsgServiceAdapterClass,
                                                          event),
                                         NULL, NULL,
                                         ytsg_marshal_VOID__STRING_BOXED,
                                         G_TYPE_NONE, 2,
                                         G_TYPE_STRING, G_TYPE_VARIANT);

  _signals[RESPONSE_SIGNAL] = g_signal_new ("response",
                                            YTSG_TYPE_SERVICE_ADAPTER,
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (YtsgServiceAdapterClass,
                                                             response),
                                            NULL, NULL,
                                            ytsg_marshal_VOID__STRING_BOXED,
                                            G_TYPE_NONE, 2,
                                            G_TYPE_STRING, G_TYPE_VARIANT);
}

static void
ytsg_service_adapter_init (YtsgServiceAdapter *self)
{
}

char const *
ytsg_service_adapter_get_capability (YtsgServiceAdapter *self)
{
  GObject     *service;
  GParamSpec  *pspec;

  g_return_val_if_fail (YTSG_IS_SERVICE_ADAPTER (self), NULL);

  /* Get service object from our subclass. */
  service = NULL;
  g_object_get (self, "service", &service, NULL);
  g_return_val_if_fail (service, NULL);

  /* The service object implements a capability property,
   * holding the capability as default value. */
  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (service),
                                        "capability");
  g_return_val_if_fail (G_IS_PARAM_SPEC_STRING (pspec), NULL);

  return G_PARAM_SPEC_STRING (pspec)->default_value;
}

GHashTable *
ytsg_service_adapter_collect_properties (YtsgServiceAdapter *self)
{
  g_return_val_if_fail (YTSG_IS_SERVICE_ADAPTER (self), NULL);

  return YTSG_SERVICE_ADAPTER_GET_CLASS (self)->collect_properties (self);
}

bool
ytsg_service_adapter_invoke (YtsgServiceAdapter *self,
                             char const         *invocation_id,
                             char const         *aspect,
                             GVariant           *arguments)
{
  bool keep_sae;

  g_return_val_if_fail (YTSG_IS_SERVICE_ADAPTER (self), false);

  keep_sae = YTSG_SERVICE_ADAPTER_GET_CLASS (self)->invoke (self,
                                                            invocation_id,
                                                            aspect,
                                                            arguments);

  /* This is a bit hackish, ok, but it allows for creating the variant
   * in the invocation of this function. */
  if (arguments &&
      g_variant_is_floating (arguments)) {
    g_variant_unref (arguments);
  }

  return keep_sae;
}

void
ytsg_service_adapter_send_error (YtsgServiceAdapter *self,
                                 char const         *invocation_id,
                                 GError const       *error)
{
  g_return_if_fail (YTSG_IS_SERVICE_ADAPTER (self));

  g_signal_emit (self, _signals[ERROR_SIGNAL], 0,
                 invocation_id, error);
}

void
ytsg_service_adapter_send_event (YtsgServiceAdapter *self,
                                 char const         *aspect,
                                 GVariant           *arguments)
{
  g_return_if_fail (YTSG_IS_SERVICE_ADAPTER (self));

  /* This is a bit hackish, ok, but it allows for creating the variant
   * in the invocation of this function. */
  g_signal_emit (self, _signals[EVENT_SIGNAL], 0,
                 aspect, arguments);

  if (arguments &&
      g_variant_is_floating (arguments)) {
    g_variant_unref (arguments);
  }
}

void
ytsg_service_adapter_send_response (YtsgServiceAdapter  *self,
                                    char const          *invocation_id,
                                    GVariant            *return_value)
{
  g_return_if_fail (YTSG_IS_SERVICE_ADAPTER (self));

  /* This is a bit hackish, ok, but it allows for creating the variant
   * in the invocation of this function. */
  g_signal_emit (self, _signals[RESPONSE_SIGNAL], 0,
                 invocation_id, return_value);

  if (return_value &&
      g_variant_is_floating (return_value)) {
    g_variant_unref (return_value);
  }
}

