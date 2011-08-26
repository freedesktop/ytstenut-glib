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

#include "ytsg-profile.h"
#include "ytsg-profile-impl.h"

static void
_profile_interface_init (YtsgProfileInterface *interface);

G_DEFINE_TYPE_WITH_CODE (YtsgProfileImpl,
                         ytsg_profile_impl,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (YTSG_TYPE_PROFILE,
                                                _profile_interface_init))

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), YTSG_TYPE_PROFILE_IMPL, YtsgProfileImplPrivate))

enum {
  PROP_0,

  PROP_PROFILE_CAPABILITY,
  PROP_PROFILE_CAPABILITIES,

  PROP_CLIENT
};

typedef struct {
  GStrv        capabilities;
  YtsgClient  *client;        /* free pointer */
} YtsgProfileImplPrivate;

/*
 * YtsgProfile
 */

static void
_register_proxy (YtsgProfile  *self,
                 char const   *invocation_id,
                 char const   *capability)
{
  YtsgProfileImplPrivate *priv = GET_PRIVATE (self);
  YtsgContact const *contact;
  char const        *proxy_id;
  bool               found;
  bool               ret;
  int                i;

  found = false;
  for (i = 0; priv->capabilities[i]; i++) {
    if (0 == g_strcmp0 (priv->capabilities[i], capability)) {
      found = true;
      break;
    }
  }

  if (!found) {
    g_critical ("%s : Capability %s not available in profile",
                G_STRLOC,
                capability);
    ytsg_profile_register_proxy_return (self, invocation_id, false);
    return;
  }

  ret = ytsg_client_get_invocation_proxy (priv->client,
                                          invocation_id,
                                          &contact,
                                          &proxy_id);
  if (!ret) {
    g_critical ("%s : Failed to get proxy info for %s",
                G_STRLOC,
                capability);
    ytsg_profile_register_proxy_return (self, invocation_id, false);
    return;
  }

  ret = ytsg_client_register_proxy (priv->client,
                                    capability,
                                    contact,
                                    proxy_id);
  if (!ret) {
    g_critical ("%s : Failed to register proxy %s:%s for %s",
                G_STRLOC,
                ytsg_contact_get_jid (contact),
                proxy_id,
                capability);
    ytsg_profile_register_proxy_return (self, invocation_id, false);
    return;
  }

  ytsg_profile_register_proxy_return (self, invocation_id, true);
}

static void
_unregister_proxy (YtsgProfile  *self,
                   char const   *invocation_id,
                   char const   *capability)
{
  YtsgProfileImplPrivate *priv = GET_PRIVATE (self);
  YtsgContact const *contact;
  char const        *proxy_id;
  bool               found;
  bool               ret;
  int                i;

  found = false;
  for (i = 0; priv->capabilities[i]; i++) {
    if (0 == g_strcmp0 (priv->capabilities[i], capability)) {
      found = true;
      break;
    }
  }

  if (!found) {
    g_critical ("%s : Capability %s not available in profile",
                G_STRLOC,
                capability);
    ytsg_profile_unregister_proxy_return (self, invocation_id, false);
    return;
  }

  ret = ytsg_client_get_invocation_proxy (priv->client,
                                          invocation_id,
                                          &contact,
                                          &proxy_id);
  if (!ret) {
    g_critical ("%s : Failed to get proxy info for %s",
                G_STRLOC,
                capability);
    ytsg_profile_unregister_proxy_return (self, invocation_id, false);
    return;
  }

  ret = ytsg_client_unregister_proxy (priv->client,
                                      capability,
                                      contact,
                                      proxy_id);
  if (!ret) {
    g_critical ("%s : Failed to unregister proxy %s:%s for %s",
                G_STRLOC,
                ytsg_contact_get_jid (contact),
                proxy_id,
                capability);
    ytsg_profile_unregister_proxy_return (self, invocation_id, false);
    return;
  }

  ytsg_profile_unregister_proxy_return (self, invocation_id, true);
}

static void
_profile_interface_init (YtsgProfileInterface *interface)
{
  interface->register_proxy = _register_proxy;
  interface->unregister_proxy = _unregister_proxy;
}

/*
 * YtsgProfileImpl
 */

static void
_get_property (GObject      *object,
               unsigned int  property_id,
               GValue       *value,
               GParamSpec   *pspec)
{
  YtsgProfileImplPrivate *priv = GET_PRIVATE (object);

  switch (property_id) {
    case PROP_PROFILE_CAPABILITIES:
      g_value_set_boxed (value, priv->capabilities);
      break;
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
  YtsgProfileImplPrivate *priv = GET_PRIVATE (object);

  switch (property_id) {
    case PROP_PROFILE_CAPABILITIES:
      /* Construct-only */
      g_return_if_fail (priv->capabilities == NULL);
      priv->capabilities = g_value_dup_boxed (value);
      break;
    case PROP_CLIENT:
      /* Construct-only */
      priv->client = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
_dispose (GObject *object)
{
  YtsgProfileImplPrivate *priv = GET_PRIVATE (object);

  if (priv->capabilities) {
    g_boxed_free (G_TYPE_STRV, priv->capabilities);
    priv->capabilities = NULL;
  }

  G_OBJECT_CLASS (ytsg_profile_impl_parent_class)->dispose (object);
}

static void
ytsg_profile_impl_class_init (YtsgProfileImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (YtsgProfileImplPrivate));

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;
  object_class->dispose = _dispose;

  /* YtsgProfile interface */

  /* Just for default value, no need to handle get/set. */
  g_object_class_override_property (object_class,
                                    PROP_PROFILE_CAPABILITY,
                                    "capability");

  g_object_class_override_property (object_class,
                                    PROP_PROFILE_CAPABILITIES,
                                    "capabilities");

  /* Properties */

  pspec = g_param_spec_object ("client", "", "",
                               YTSG_TYPE_CLIENT,
                               G_PARAM_READWRITE |
                               G_PARAM_CONSTRUCT_ONLY |
                               G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
                                   PROP_CLIENT,
                                   pspec);
}

static void
ytsg_profile_impl_init (YtsgProfileImpl *self)
{
}

YtsgProfileImpl *
ytsg_profile_impl_new (GStrv const   capabilities,
                       YtsgClient   *client)
{
  return g_object_new (YTSG_TYPE_PROFILE_IMPL,
                       "capabilities",  capabilities,
                       "client",        client,
                       NULL);
}
