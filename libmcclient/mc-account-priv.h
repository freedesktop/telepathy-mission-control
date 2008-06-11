/*
 * mc-account-priv.h - the Telepathy Account D-Bus interface
 * (client side)
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __LIBMCCLIENT_ACCOUNT_PRIV_H__
#define __LIBMCCLIENT_ACCOUNT_PRIV_H__

#include <telepathy-glib/proxy.h>
#include <telepathy-glib/enums.h>

G_BEGIN_DECLS

typedef struct _McAccountProps McAccountProps;
typedef struct _McAccountAvatarProps McAccountAvatarProps;

struct _McAccountPrivate {
    McAccountProps *props;
    McAccountAvatarProps *avatar_props;
};

typedef struct _CallWhenReadyContext CallWhenReadyContext;
typedef struct _McAccountIfaceData McAccountIfaceData;

typedef void (*McAccountCreateProps) (McAccount *account, GHashTable *props);

struct _CallWhenReadyContext {
    McAccountWhenReadyCb callback;
    gpointer user_data;
    McAccountCreateProps create_props;
};

struct _McAccountIfaceData {
    gchar *name;
    gpointer *props_data_ptr;
    McAccountCreateProps create_props;
};

void _mc_account_call_when_ready_int (McAccount *account,
				      McAccountWhenReadyCb callback,
				      gpointer user_data,
				      McAccountIfaceData *iface_data);

G_END_DECLS

#endif