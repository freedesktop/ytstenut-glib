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

#ifndef YTSG_VS_PLAYER_PROXY_H
#define YTSG_VS_PLAYER_PROXY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define YTSG_VS_TYPE_PLAYER_PROXY ytsg_vs_player_proxy_get_type()

#define YTSG_VS_PLAYER_PROXY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), YTSG_VS_TYPE_PLAYER_PROXY, YtsgVSPlayerProxy))

#define YTSG_VS_PLAYER_PROXY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), YTSG_VS_TYPE_PLAYER_PROXY, YtsgVSPlayerProxyClass))

#define YTSG_VS_IS_PLAYER_PROXY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), YTSG_VS_TYPE_PLAYER_PROXY))

#define YTSG_VS_IS_PLAYER_PROXY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), YTSG_VS_TYPE_PLAYER_PROXY))

#define YTSG_VS_PLAYER_PROXY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), YTSG_VS_TYPE_PLAYER_PROXY, YtsgVSPlayerProxyClass))

typedef struct {
  GObject parent;
} YtsgVSPlayerProxy;

typedef struct {
  GObjectClass parent;
} YtsgVSPlayerProxyClass;

GType
ytsg_vs_player_proxy_get_type (void);

G_END_DECLS

#endif /* YTSG_VS_PLAYER_PROXY_H */
