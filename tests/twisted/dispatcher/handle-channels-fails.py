# Copyright (C) 2009 Nokia Corporation
# Copyright (C) 2009 Collabora Ltd.
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
"""Regression test for dispatching an incoming Text channel.
"""

import dbus
import dbus.bus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    simulated_cm, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')
    vague_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    empathy_bus = dbus.bus.BusConnection()
    q.attach_to_bus(empathy_bus)
    empathy = SimulatedClient(q, empathy_bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # Kopete's filter is less specific than Empathy's, so we'll prefer Empathy
    kopete_bus = dbus.bus.BusConnection()
    q.attach_to_bus(kopete_bus)
    kopete = SimulatedClient(q, kopete_bus, 'Kopete',
            observe=[], approve=[],
            handle=[vague_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [empathy, kopete])

    # subscribe to the OperationList interface (MC assumes that until this
    # property has been retrieved once, nobody cares)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

    channel_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = 'juliet'
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel_properties[cs.CHANNEL + '.InitiatorID'] = 'juliet'
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel_properties[cs.CHANNEL + '.Requested'] = False
    channel_properties[cs.CHANNEL + '.Interfaces'] = dbus.Array(signature='s')

    chan = SimulatedChannel(conn, channel_properties)
    chan.announce()

    # A channel dispatch operation is created

    e = q.expect('dbus-signal',
            path=cs.CD_PATH,
            interface=cs.CD_IFACE_OP_LIST,
            signal='NewDispatchOperation')

    cdo_path = e.args[0]
    cdo_properties = e.args[1]

    assert cdo_properties[cs.CDO + '.Account'] == account.object_path
    assert cdo_properties[cs.CDO + '.Connection'] == conn.object_path
    assert cs.CDO + '.Interfaces' in cdo_properties

    # In this test Empathy's filter has more things in it than Kopete's, so
    # MC will prefer Empathy
    handlers = cdo_properties[cs.CDO + '.PossibleHandlers'][:]
    assert handlers == [cs.tp_name_prefix + '.Client.Empathy',
            cs.tp_name_prefix + '.Client.Kopete'], handlers

    assert cs.CD_IFACE_OP_LIST in cd_props.Get(cs.CD, 'Interfaces')
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') ==\
            [(cdo_path, cdo_properties)]

    cdo = bus.get_object(cs.CD, cdo_path)
    cdo_iface = dbus.Interface(cdo, cs.CDO)
    cdo_props_iface = dbus.Interface(cdo, cs.PROPERTIES_IFACE)

    assert cdo_props_iface.Get(cs.CDO, 'Interfaces') == \
            cdo_properties[cs.CDO + '.Interfaces']
    assert cdo_props_iface.Get(cs.CDO, 'Connection') == conn.object_path
    assert cdo_props_iface.Get(cs.CDO, 'Account') == account.object_path
    assert cdo_props_iface.Get(cs.CDO, 'Channels') == [(chan.object_path,
        channel_properties)]
    assert cdo_props_iface.Get(cs.CDO, 'PossibleHandlers') == \
            cdo_properties[cs.CDO + '.PossibleHandlers']

    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.OBSERVER, method='ObserveChannels',
            handled=False)
    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    assert e.args[3] == cdo_path, e.args
    assert e.args[4] == [], e.args      # no requests satisfied
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == chan.object_path, channels
    assert channels[0][1] == channel_properties, channels

    q.dbus_return(e.message, bus=empathy_bus, signature='')

    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.APPROVER, method='AddDispatchOperation',
            handled=False)

    assert e.args == [[(chan.object_path, channel_properties)],
            cdo_path, cdo_properties]

    q.dbus_return(e.message, bus=empathy_bus, signature='')

    call_async(q, cdo_iface, 'HandleWith',
            cs.tp_name_prefix + '.Client.Empathy')

    # Empathy is asked to handle the channels
    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    # Empathy rejects the channels
    q.dbus_raise(e.message, cs.NOT_AVAILABLE, 'Blind drunk', bus=empathy_bus)

    e = q.expect('dbus-error', method='HandleWith')
    assert e.error.get_dbus_name() == cs.NOT_AVAILABLE
    assert e.error.get_dbus_message() == 'Blind drunk'

    # The channels no longer count as having been approved. Check that MC
    # doesn't carry on regardless
    forbidden = [EventPattern('dbus-method-call', method='HandleChannels')]
    q.forbid_events(forbidden)
    sync_dbus(bus, q, mc)
    q.unforbid_events(forbidden)

    # I'm Feeling Lucky. It might work if I try again? Maybe?
    call_async(q, cdo_iface, 'HandleWith',
            cs.tp_name_prefix + '.Client.Empathy')

    # Empathy is asked to handle the channels, again
    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    # Empathy rejects the channels, again
    q.dbus_raise(e.message, cs.NOT_CAPABLE, 'Still drunk', bus=empathy_bus)

    e = q.expect('dbus-error', method='HandleWith')
    assert e.error.get_dbus_name() == cs.NOT_CAPABLE
    assert e.error.get_dbus_message() == 'Still drunk'

    # OK, OK, is anyone else competent enough to handle them?
    # (Also, assert that MC doesn't offer them back to Empathy, knowing that
    # it already tried and failed)
    forbidden = [EventPattern('dbus-method-call', method='HandleChannels',
        path=empathy.object_path)]
    q.forbid_events(forbidden)
    call_async(q, cdo_iface, 'HandleWith', '')

    # Kopete is asked to handle the channels
    k = q.expect('dbus-method-call',
                path=kopete.object_path,
                interface=cs.HANDLER, method='HandleChannels',
                handled=False)

    # Kopete rejects the channels too
    q.dbus_raise(k.message, cs.NOT_AVAILABLE, 'Also blind drunk',
            bus=kopete_bus)

    e = q.expect('dbus-error', method='HandleWith')

    assert e.error.get_dbus_name() == cs.NOT_AVAILABLE
    assert e.error.get_dbus_message() == 'Also blind drunk'

    # MC gives up and closes the channel. This is the end of the CDO.
    q.expect_many(
            EventPattern('dbus-method-call', path=chan.object_path,
                interface=cs.CHANNEL, method='Close', args=[]),
            EventPattern('dbus-signal', interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished'),
            )

    # Now there are no more active channel dispatch operations
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

if __name__ == '__main__':
    exec_test(test, {})
