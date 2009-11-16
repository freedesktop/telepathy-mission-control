/* A Telepathy ChannelRequest object
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef MCD_REQUEST_H
#define MCD_REQUEST_H

#include "mcd-account.h"

G_BEGIN_DECLS

typedef struct _McdRequest McdRequest;
typedef struct _McdRequestClass McdRequestClass;
typedef struct _McdRequestPrivate McdRequestPrivate;

G_GNUC_INTERNAL GType _mcd_request_get_type (void);

#define MCD_TYPE_REQUEST \
  (_mcd_request_get_type ())
#define MCD_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_REQUEST, \
                               McdRequest))
#define MCD_REQUEST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_REQUEST, \
                            McdRequestClass))
#define MCD_IS_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_REQUEST))
#define MCD_IS_REQUEST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_REQUEST))
#define MCD_REQUEST_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_REQUEST, \
                              McdRequestClass))

G_GNUC_INTERNAL McdRequest *_mcd_request_new (McdAccount *account);

G_END_DECLS

#endif
