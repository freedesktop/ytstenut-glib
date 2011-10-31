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

#ifndef YTS_SERVICE_INTERNAL_H
#define YTS_SERVICE_INTERNAL_H

#include <ytstenut/yts-metadata.h>
#include <ytstenut/yts-service.h>

#define YTS_SERVICE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), YTS_TYPE_SERVICE, YtsServiceClass))

#define YTS_IS_SERVICE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), YTS_TYPE_SERVICE))

#define YTS_SERVICE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), YTS_TYPE_SERVICE, YtsServiceClass))

struct YtsService {
  GObject parent;
};

typedef struct {
  GObjectClass parent;
} YtsServiceClass;

void
yts_service_send_message (YtsService  *self,
                          YtsMetadata *message);

void
yts_service_update_status (YtsService *self,
                           char const *fqc_id,
                           char const *status_xml);

G_END_DECLS

#endif /* YTS_SERVICE_INTERNAL_H */

