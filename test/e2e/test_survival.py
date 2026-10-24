"""The device must survive the loads that ran its heap out
(docs/adr/0005-one-websocket-protocol.md): every client reconnecting at once,
and a browser loading the page."""
import threading
import urllib.request

import pytest

from proto_client import ProtoClient, heap, run_sync

# What the heap may drop to under load.  Below it the next allocation in the
# web server is close to failing, which aborts.
HEAP_FLOOR = 20 * 1024
MAX_CLIENTS = 5


@pytest.fixture
def watch_heap(host):
    """Fails the test when the device rebooted or its heap fell under the floor.

    The device reports its lowest heap since it booted, so a dip under the
    floor only shows while that low is still above it."""
    before = heap(host)
    if before["min"] < HEAP_FLOOR:
        pytest.skip("the heap has been down to %u bytes since boot: reboot the device to check the floor" % before["min"])
    yield before
    after = heap(host)
    assert after["uptime"] > before["uptime"], "the device rebooted"
    assert after["min"] >= HEAP_FLOOR, "the heap fell to %u bytes (floor %u)" % (after["min"], HEAP_FLOOR)


def test_every_client_reconnecting_at_once(connect, schema, watch_heap):
    clients = [connect() for _ in range(MAX_CLIENTS)]
    errors = []

    def sync(c):
        try:
            run_sync(c, timeout=60)
        except Exception as e:  # reported below
            errors.append(e)
    threads = [threading.Thread(target=sync, args=(c,)) for c in clients]
    [t.start() for t in threads]
    [t.join() for t in threads]
    assert not errors, errors


def test_one_client_too_many_is_closed(connect, schema):
    clients = [connect(schema=schema.hash) for _ in range(MAX_CLIENTS)]
    for c in clients:
        run_sync(c)
    extra = connect(schema=schema.hash)
    assert extra.closed.wait(5), "a sixth client stayed connected"


def test_page_load(host, token, connect, watch_heap):
    """The static files in parallel, as a browser fetches them, then the socket."""
    files = ["/", "/main.css", "/widgets.css", "/icons.css", "/index.js", "/icon.png"]
    errors = []

    def get(path):
        try:
            req = urllib.request.Request(f"http://{host}{path}", headers={"Accept-Encoding": "gzip"})
            with urllib.request.urlopen(req, timeout=60) as r:
                r.read()
        except Exception as e:  # reported below
            errors.append((path, e))
    threads = [threading.Thread(target=get, args=(p,)) for p in files]
    [t.start() for t in threads]
    c = connect()
    run_sync(c, timeout=60)
    [t.join() for t in threads]
    assert not errors, errors
