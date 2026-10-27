"""The HTTP routes and the socket's Origin check under the device's security
mode (docs/adr/0006-two-security-modes-eleven-http-routes.md).

Like test_auth, it tests the mode it finds: pass --password or --pin for a
Secured device.
"""
import time
import urllib.error
import urllib.request

import pytest

from proto_client import ProtoClient, heap

# Every route that needs the token under Secured, sent without a body or
# parameters, so a route that let one through would still change nothing.
TOKEN_ROUTES = [
    ("GET", "/backup"),
    ("GET", "/shadeCommand"),
    ("GET", "/groupCommand"),
    ("GET", "/tiltCommand"),
    ("PUT", "/shadeCommand"),
    ("PUT", "/groupCommand"),
    ("PUT", "/tiltCommand"),
]
PUBLIC_ROUTES = ["/", "/index.html", "/index.js", "/loginContext", "/upnp.xml"]
SHADE_FILES = ["/shades.cfg", "/shades.tmp"]


def http(host, path, method="GET", body=None, headers=None):
    """The status and headers of a request, refusals included."""
    req = urllib.request.Request(f"http://{host}{path}", data=body, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            r.read()
            return r.status, r.headers
    except urllib.error.HTTPError as e:
        e.read()
        return e.code, e.headers


def upload(host, path, headers=None):
    """Posts a small file that isn't a backup or an image."""
    boundary = "e2e-upload"
    body = (
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"e2e.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n").encode() + b"\0" * 256 + f"\r\n--{boundary}--\r\n".encode()
    return http(host, path, "POST", body, {"Content-Type": f"multipart/form-data; boundary={boundary}", **(headers or {})})


@pytest.mark.parametrize("method,path", TOKEN_ROUTES)
def test_secured_refuses_a_route_without_a_token(host, secured, method, path):
    if not secured:
        pytest.skip("the device's security is None")
    body = b"{}" if method == "PUT" else None
    status, _ = http(host, path, method, body, {"Content-Type": "application/json"} if body else None)
    assert status == 401


@pytest.mark.parametrize("method,path", TOKEN_ROUTES)
def test_secured_refuses_a_route_with_a_wrong_token(host, secured, method, path):
    if not secured:
        pytest.skip("the device's security is None")
    body = b"{}" if method == "PUT" else None
    headers = {"apikey": "0" * 64}
    if body:
        headers["Content-Type"] = "application/json"
    status, _ = http(host, path, method, body, headers)
    assert status == 401


def test_secured_accepts_the_token_on_a_route(host, secured, token):
    if not secured:
        pytest.skip("the device's security is None")
    status, _ = http(host, "/backup", headers={"apikey": token})
    assert status == 200


@pytest.mark.parametrize("path", ["/restore", "/updateFirmware"])
def test_secured_drops_an_upload_without_a_token(host, secured, token, path):
    """The token check runs once the body is in, so the uploads check it on their first chunk.

    An /updateFirmware that got through would stop the radio and reboot the
    device once the connection closed.  /updateApplication isn't sent: one
    that got through would write over the file system.
    """
    if not secured:
        pytest.skip("the device's security is None")
    before = heap(host, token)
    status, _ = upload(host, path)
    assert status == 401
    after = heap(host, token)
    assert after["uptime"] >= before["uptime"], "the device rebooted"


@pytest.mark.parametrize("path", PUBLIC_ROUTES)
def test_public_routes_need_no_token(host, path):
    status, _ = http(host, path)
    assert status == 200


@pytest.mark.parametrize("path", SHADE_FILES)
def test_the_shade_files_are_not_served(host, path):
    """Their rolling codes leave the device only through /backup."""
    status, _ = http(host, path)
    assert status in (401, 404)


def test_replies_carry_no_cors_headers(host):
    _, headers = http(host, "/loginContext", headers={"Origin": "http://example.com"})
    assert headers.get("Access-Control-Allow-Origin") is None


def test_a_socket_from_another_site_is_refused(host, token):
    params = {"apikey": token} if token else None
    c = ProtoClient(host, params=params, headers={"Origin": "http://example.com"})
    assert c.status == 401


def test_a_socket_from_the_page_the_device_served_is_accepted(host, token):
    # ProtoClient's Host names the port, and so does an Origin that matches it.
    params = {"apikey": token} if token else None
    c = ProtoClient(host, params=params, headers={"Origin": f"http://{host}:80"})
    assert c.status == 101
    c.close()
    # The device frees the slot when it sees the disconnect.
    time.sleep(0.5)
