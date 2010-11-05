# Copyright (C) 2010 Nokia Corporation
# Copyright (C) 2010 Collabora Ltd.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
# 02110-1301 USA

import dbus
import dbus
import dbus.service

from servicetest import unwrap, assertContains, assertEquals, assertSameSets
from mctest import exec_test, create_fakecm_account
import constants as cs

def get_schemes(props):
    return unwrap (props.Get (cs.ACCOUNT_IFACE_ADDRESSING, 'URISchemes'))

def test(q, bus, mc):
    params = dbus.Dictionary ({"account": "jc.denton@example.com",
                               "password": "ionstorm"},
                              signature='sv')
    (cm_name_ref, account) = create_fakecm_account (q, bus, mc, params)

    account_iface = dbus.Interface (account, cs.ACCOUNT)
    account_props = dbus.Interface (account, cs.PROPERTIES_IFACE)
    address_iface = dbus.Interface (account, cs.ACCOUNT_IFACE_ADDRESSING)

    uri_schemes = get_schemes (account_props)

    # initial URI scheme list is empty
    assertEquals (uri_schemes, [])

    # remove URI from empty list:
    assert (not address_iface.SetURISchemeAssociation ('email', False))
    uri_schemes = get_schemes (account_props)
    assertEquals (uri_schemes, [])

    # add association to empty list
    assert (not address_iface.SetURISchemeAssociation ('email', True))
    uri_schemes = get_schemes (account_props)
    assertEquals (uri_schemes, ['email'])

    # add association to list where it already resides
    assert (address_iface.SetURISchemeAssociation ('email', True))
    uri_schemes = get_schemes (account_props)
    assertEquals (uri_schemes, ['email'])

    # remove association to produce empty list 
    assert (address_iface.SetURISchemeAssociation ('email', False))
    uri_schemes = get_schemes (account_props)
    assertEquals (uri_schemes, [])

    # extend list to 3 schemes, with some redundant additions:
    assert (not address_iface.SetURISchemeAssociation ('scheme-a', True))
    assert (not address_iface.SetURISchemeAssociation ('scheme-b', True))
    assert (not address_iface.SetURISchemeAssociation ('scheme-c', True))
    assert (address_iface.SetURISchemeAssociation ('scheme-a', True))
    assert (address_iface.SetURISchemeAssociation ('scheme-c', True))
    uri_schemes = get_schemes (account_props)
    assertSameSets (set (['scheme-a','scheme-b','scheme-c']), set (uri_schemes))

    # remove a scheme that's not there from a non-empty list
    assert (not address_iface.SetURISchemeAssociation ('scheme-d', False))
    uri_schemes = get_schemes (account_props)
    assertSameSets (set (['scheme-a','scheme-b','scheme-c']), set (uri_schemes))


    # remove one that is there:
    assert (address_iface.SetURISchemeAssociation ('scheme-b', False))
    uri_schemes = get_schemes (account_props)
    assertSameSets (set (['scheme-a','scheme-c']), set (uri_schemes))

if __name__ == '__main__':
    exec_test(test, {})
