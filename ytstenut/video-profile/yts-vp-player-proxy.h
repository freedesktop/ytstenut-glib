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

#ifndef YTS_VP_PLAYER_PROXY_H
#define YTS_VP_PLAYER_PROXY_H

#include <glib-object.h>
#include <ytstenut/yts-proxy.h>

G_BEGIN_DECLS

#define YTS_VP_TYPE_PLAYER_PROXY yts_vp_player_proxy_get_type()

#define YTS_VP_PLAYER_PROXY(obj)                         \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                     \
                               YTS_VP_TYPE_PLAYER_PROXY, \
                               YtsVPPlayerProxy))

#define YTS_VP_PLAYER_PROXY_CLASS(klass)               \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                    \
                             YTS_VP_TYPE_PLAYER_PROXY, \
                             YtsVPPlayerProxyClass))

#define YTS_VP_IS_PLAYER_PROXY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), YTS_VP_TYPE_PLAYER_PROXY))

#define YTS_VP_IS_PLAYER_PROXY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), YTS_VP_TYPE_PLAYER_PROXY))

#define YTS_VP_PLAYER_PROXY_GET_CLASS(obj)               \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                      \
                              YTS_VP_TYPE_PLAYER_PROXY,  \
                              YtsVPPlayerProxyClass))

typedef struct {
  YtsProxy parent;
} YtsVPPlayerProxy;

typedef struct {
  YtsProxyClass parent;
} YtsVPPlayerProxyClass;

GType
yts_vp_player_proxy_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* YTS_VP_PLAYER_PROXY_H */

