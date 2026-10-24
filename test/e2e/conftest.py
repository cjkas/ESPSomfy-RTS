"""Options and fixtures for the e2e suite.  See README.md."""
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from proto_client import ProtoClient, Schema, http_json, run_sync  # noqa: E402


def pytest_addoption(parser):
    parser.addoption("--device", required=True, help="the device's address, e.g. 192.168.1.120")
    parser.addoption("--password", default=None, help="the device's password, when it is Secured with one")
    parser.addoption("--username", default="", help="the username that goes with --password")
    parser.addoption("--pin", default=None, help="the device's PIN, when it is Secured with one")
    parser.addoption("--record", action="store_true", help="save what the device sends to test/fixtures/proto2")


@pytest.fixture(scope="session")
def host(request):
    return request.config.getoption("--device")


@pytest.fixture(scope="session")
def login_context(host):
    return http_json(host, "/loginContext")


@pytest.fixture(scope="session")
def secured(login_context):
    return login_context.get("type", 0) != 0


@pytest.fixture(scope="session")
def token(request, host, secured):
    """The token for this PC, or None when the device isn't Secured."""
    if not secured:
        return None
    password = request.config.getoption("--password")
    pin = request.config.getoption("--pin")
    if password is None and pin is None:
        pytest.skip("the device is Secured: pass --password or --pin")
    body = {"username": request.config.getoption("--username"), "password": password or "", "pin": pin or ""}
    return http_json(host, "/login", method="PUT", body=body)["apiKey"]


@pytest.fixture
def connect(host, token):
    """Opens protocol 2 clients and closes them after the test."""
    clients = []

    def open_client(**params):
        if token and "apikey" not in params:
            params["apikey"] = token
        c = ProtoClient(host, params=params or None)
        clients.append(c)
        return c

    yield open_client
    for c in clients:
        c.close()
    # The device frees a slot when it sees the disconnect; the next test may
    # need every slot.
    time.sleep(0.5)


@pytest.fixture(scope="session")
def schema(host, token):
    """The device's schema, from one full sync."""
    s = Schema()
    c = ProtoClient(host, params={"apikey": token} if token else None)
    try:
        assert c.status == 101, "the device has no /ws: is it running protocol 2 firmware?"
        run_sync(c, s)
    finally:
        c.close()
    assert s.complete() and s.hash
    return s


@pytest.fixture(scope="session")
def recording(request):
    return request.config.getoption("--record")
