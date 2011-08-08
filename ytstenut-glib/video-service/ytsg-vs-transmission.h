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

#ifndef YTSG_VS_TRANSMISSION_H
#define YTSG_VS_TRANSMISSION_H

#include <stdbool.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define YTSG_VS_TYPE_TRANSMISSION \
  (ytsg_vs_transmission_get_type ())

#define YTSG_VS_TRANSMISSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), YTSG_VS_TYPE_TRANSMISSION, YtsgVSTransmission))

#define YTSG_VS_IS_TRANSMISSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), YTSG_VS_TYPE_TRANSMISSION))

#define YTSG_VS_TRANSMISSION_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), YTSG_VS_TYPE_TRANSMISSION, YtsgVSTransmissionInterface))

typedef struct YtsgVSTransmission YtsgVSTransmission;
typedef struct YtsgVSTransmissionInterface YtsgVSTransmissionInterface;

struct YtsgVSTransmissionInterface {

  /*< private >*/
  GTypeInterface parent;
};

GType
ytsg_vs_transmission_get_type (void) G_GNUC_CONST;

char *
ytsg_vs_transmission_get_local_uri (YtsgVSTransmission *self);

unsigned int
ytsg_vs_transmission_get_progress (YtsgVSTransmission *self);

char *
ytsg_vs_transmission_get_remote_uri (YtsgVSTransmission *self);

G_END_DECLS

#endif /* YTSG_VS_TRANSMISSION_H */
