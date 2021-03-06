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

#ifndef YTS_CONTACT_IMPL_H
#define YTS_CONTACT_IMPL_H

#include <glib-object.h>
#include <gio/gio.h>
#include <telepathy-glib/contact.h>
#include <ytstenut/yts-contact-internal.h>
#include <ytstenut/yts-metadata.h>
#include <ytstenut/yts-outgoing-file.h>

G_BEGIN_DECLS

#define YTS_TYPE_CONTACT_IMPL  (yts_contact_impl_get_type ())

#define YTS_CONTACT_IMPL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), YTS_TYPE_CONTACT_IMPL, YtsContactImpl))

#define YTS_CONTACT_IMPL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), YTS_TYPE_CONTACT_IMPL, YtsContactImplClass))

#define YTS_IS_CONTACT_IMPL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), YTS_TYPE_CONTACT_IMPL))

#define YTS_IS_CONTACT_IMPL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), YTS_TYPE_CONTACT_IMPL))

#define YTS_CONTACT_IMPL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), YTS_TYPE_CONTACT_IMPL, YtsContactImplClass))

typedef struct {
  YtsContact parent;
} YtsContactImpl;

typedef struct {
  YtsContactClass parent;
} YtsContactImplClass;

GType
yts_contact_impl_get_type (void) G_GNUC_CONST;

YtsContact *
yts_contact_impl_new (TpContact *tp_contact);

void
yts_contact_impl_send_message (YtsContactImpl *self,
                               YtsService     *service,
                               YtsMetadata    *message);

YtsOutgoingFile *
yts_contact_impl_send_file (YtsContactImpl   *self,
                            YtsService       *service,
                            GFile            *file,
                            char const       *description,
                            GError          **error_out);

G_END_DECLS

#endif /* YTS_CONTACT_IMPL_H */

