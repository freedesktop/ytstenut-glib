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

#ifndef YTSG_VS_TRANSFER_H
#define YTSG_VS_TRANSFER_H

#include <stdbool.h>
#include <glib-object.h>
#include <ytstenut-glib/video-service/ytsg-vs-transfer.h>
#include <ytstenut-glib/video-service/ytsg-vs-transmission.h>

G_BEGIN_DECLS

#define YTSG_VS_TYPE_TRANSFER \
  (ytsg_vs_transfer_get_type ())

#define YTSG_VS_TRANSFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), YTSG_VS_TYPE_TRANSFER, YtsgVSTransfer))

#define YTSG_VS_IS_TRANSFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), YTSG_VS_TYPE_TRANSFER))

#define YTSG_VS_TRANSFER_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), YTSG_VS_TYPE_TRANSFER, YtsgVSTransferInterface))

typedef struct YtsgVSTransfer YtsgVSTransfer;
typedef struct YtsgVSTransferInterface YtsgVSTransferInterface;

struct YtsgVSTransferInterface {

  /*< private >*/
  GTypeInterface parent;

  YtsgVSTransmission * (*download) (YtsgVSTransfer  *self,
                                    char const      *uri);

  YtsgVSTransmission * (*upload) (YtsgVSTransfer  *self,
                                  char const      *uri);
};

GType
ytsg_vs_transfer_get_type (void) G_GNUC_CONST;

YtsgVSTransmission *
ytsg_vs_transfer_download (YtsgVSTransfer *self,
                           char const     *uri);

YtsgVSTransmission *
ytsg_vs_transfer_upload (YtsgVSTransfer *self,
                         char const     *uri);

G_END_DECLS

#endif /* YTSG_VS_TRANSFER_H */
