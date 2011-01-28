/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2011 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _YTSG_ROSTER_H
#define _YTSG_ROSTER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define YTSG_TYPE_ROSTER                                                \
   (ytsg_roster_get_type())
#define YTSG_ROSTER(obj)                                                \
   (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                  \
                                YTSG_TYPE_ROSTER,                       \
                                YtsgRoster))
#define YTSG_ROSTER_CLASS(klass)                                        \
   (G_TYPE_CHECK_CLASS_CAST ((klass),                                   \
                             YTSG_TYPE_ROSTER,                          \
                             YtsgRosterClass))
#define YTSG_IS_ROSTER(obj)                                             \
   (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                  \
                                YTSG_TYPE_ROSTER))
#define YTSG_IS_ROSTER_CLASS(klass)                                     \
   (G_TYPE_CHECK_CLASS_TYPE ((klass),                                   \
                             YTSG_TYPE_ROSTER))
#define YTSG_ROSTER_GET_CLASS(obj)                                      \
   (G_TYPE_INSTANCE_GET_CLASS ((obj),                                   \
                               YTSG_TYPE_ROSTER,                        \
                               YtsgRosterClass))

typedef struct _YtsgRoster        YtsgRoster;
typedef struct _YtsgRosterClass   YtsgRosterClass;
typedef struct _YtsgRosterPrivate YtsgRosterPrivate;

struct _YtsgRosterClass
{
  GObjectClass parent_class;
};

struct _YtsgRoster
{
  GObject parent;

  /*<private>*/
  YtsgRosterPrivate *priv;
};

GType ytsg_roster_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* _YTSG_ROSTER_H */
