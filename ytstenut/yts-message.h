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
 */

#ifndef YTS_MESSAGE_H
#define YTS_MESSAGE_H

#include <ytstenut/yts-metadata.h>

G_BEGIN_DECLS

#define YTS_TYPE_MESSAGE                                               \
   (yts_message_get_type())
#define YTS_MESSAGE(obj)                                               \
   (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                  \
                                YTS_TYPE_MESSAGE,                      \
                                YtsMessage))
#define YTS_MESSAGE_CLASS(klass)                                       \
   (G_TYPE_CHECK_CLASS_CAST ((klass),                                   \
                             YTS_TYPE_MESSAGE,                         \
                             YtsMessageClass))
#define YTS_IS_MESSAGE(obj)                                            \
   (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                  \
                                YTS_TYPE_MESSAGE))
#define YTS_IS_MESSAGE_CLASS(klass)                                    \
   (G_TYPE_CHECK_CLASS_TYPE ((klass),                                   \
                             YTS_TYPE_MESSAGE))
#define YTS_MESSAGE_GET_CLASS(obj)                                     \
   (G_TYPE_INSTANCE_GET_CLASS ((obj),                                   \
                               YTS_TYPE_MESSAGE,                       \
                               YtsMessageClass))

typedef struct _YtsMessage        YtsMessage;
typedef struct _YtsMessageClass   YtsMessageClass;
typedef struct _YtsMessagePrivate YtsMessagePrivate;

/**
 * YtsMessageClass:
 *
 * #YtsMessage class.
 */
struct _YtsMessageClass
{
  /*< private >*/
  YtsMetadataClass parent_class;
};

/**
 * YtsMessage:
 *
 * Encapsulates a Ytstenut message, either being sent to a given #YtsService or
 * received by #YtsClient.
 */
struct _YtsMessage
{
  /*< private >*/
  YtsMetadata parent;

  /*< private >*/
  YtsMessagePrivate *priv;
};

GType yts_message_get_type (void) G_GNUC_CONST;

YtsMessage *yts_message_new (const char ** attributes);

G_END_DECLS

#endif /* YTS_MESSAGE_H */
