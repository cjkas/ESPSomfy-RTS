"""The socket under the device's security mode (docs/adr/0006-two-security-modes-eleven-http-routes.md).

The suite never changes the device's security itself, so it tests the mode it
finds: pass --password or --pin for a Secured device.
"""
import pytest

from proto_client import ProtoClient


def test_none_lets_anyone_connect(host, secured):
    if secured:
        pytest.skip("the device is Secured")
    c = ProtoClient(host)
    assert c.status == 101
    c.close()


def test_secured_refuses_a_socket_without_a_token(host, secured):
    if not secured:
        pytest.skip("the device's security is None")
    c = ProtoClient(host)
    assert c.status == 401


def test_secured_refuses_a_wrong_token(host, secured):
    if not secured:
        pytest.skip("the device's security is None")
    c = ProtoClient(host, params={"apikey": "0" * 64})
    assert c.status == 401


def test_secured_accepts_the_token(host, secured, token):
    if not secured:
        pytest.skip("the device's security is None")
    c = ProtoClient(host, params={"apikey": token})
    assert c.status == 101
    c.close()
