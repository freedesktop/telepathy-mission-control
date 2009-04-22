"""Regression test for the unofficial Account.Interface.Requests API when
a channel can be created successfully.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    client = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # No Approver should be invoked at any point during this test, because the
    # Channel was Requested
    def fail_on_approval(e):
        raise AssertionError('Approver should not be invoked')
    q.add_dbus_method_impl(fail_on_approval, path=client.object_path,
            interface=cs.APPROVER, method='AddDispatchOperation')

    # wait for MC to download the properties
    q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.CLIENT, 'Interfaces'],
                path=client.object_path),
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.APPROVER, 'ApproverChannelFilter'],
                path=client.object_path),
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.HANDLER, 'HandlerChannelFilter'],
                path=client.object_path),
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.OBSERVER, 'ObserverChannelFilter'],
                path=client.object_path),
            )

    user_action_time = dbus.Int64(1238582606)

    # chat UI calls ChannelDispatcher.CreateChannel
    # (or in this case, an equivalent non-standard method on the Account)
    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    account_requests = dbus.Interface(account,
            cs.ACCOUNT_IFACE_NOKIA_REQUESTS)
    call_async(q, account_requests, 'Create',
            request, user_action_time, client.bus_name)

    # chat UI connects to signals and calls ChannelRequest.Proceed() - but not
    # in this non-standard API, which fires off the request instantly
    ret, cm_request_call = q.expect_many(
            EventPattern('dbus-return', method='Create'),
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_REQUESTS, method='CreateChannel',
                path=conn.object_path, args=[request], handled=False),
            )

    request_path = ret.value[0]

    cr = bus.get_object(cs.AM, request_path)
    request_props = cr.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time

    # ChannelDispatcher calls AddRequest on chat UI; chat UI ignores it as the
    # request is already known to it.
    # FIXME: it is not, strictly speaking, an API guarantee that the Requests
    # call precedes this

    e = q.expect('dbus-method-call', handled=False,
        interface=cs.CLIENT_IFACE_REQUESTS, method='AddRequest',
        path=client.object_path)
    assert e.args[0] == request_path
    q.dbus_return(e.message, signature='')

    # Actually, never mind.
    account_requests.Cancel(request_path)

    # Time passes. A channel is returned.

    channel_immutable = dbus.Dictionary(request)
    channel_immutable[cs.CHANNEL + '.InitiatorID'] = conn.self_ident
    channel_immutable[cs.CHANNEL + '.InitiatorHandle'] = conn.self_handle
    channel_immutable[cs.CHANNEL + '.Requested'] = True
    channel_immutable[cs.CHANNEL + '.Interfaces'] = \
        dbus.Array([], signature='s')
    channel_immutable[cs.CHANNEL + '.TargetHandle'] = \
        conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel = SimulatedChannel(conn, channel_immutable)

    # this order of events is guaranteed by telepathy-spec (since 0.17.14)
    q.dbus_return(cm_request_call.message,
            channel.object_path, channel.immutable, signature='oa{sv}')
    channel.announce()

    # Channel is unwanted now, MC stabs it in the face
    accsig, stdsig, _ = q.expect_many(
            EventPattern('dbus-signal', path=account.object_path,
                interface=cs.ACCOUNT_IFACE_NOKIA_REQUESTS, signal='Failed'),
            EventPattern('dbus-signal', path=request_path,
                interface=cs.CR, signal='Failed'),
            EventPattern('dbus-method-call', path=channel.object_path,
                interface=cs.CHANNEL, method='Close', handled=True),
            )

if __name__ == '__main__':
    exec_test(test, {})
