"""/restore and the options the web UI sends with the backup file.

The round trip restores a backup and reboots the device: pass --reboot.
"""
import json

import pytest

from proto_client import run_sync
from test_commands import is_ok, op, synced_model
from test_routes import auth, http, restore_without_a_file, upload
from test_settings import desc, settings, uptime, wait_for_restart

# Saved for the round trip and put back by the restore.  .invalid never resolves.
CHANGED_NTP_SERVER = "restore.e2e.invalid"


def test_restore_reads_the_options_sent_as_a_form_field(host, token):
    """The options come as the form field "data", next to the file.

    They are read before the file is checked, so options that aren't JSON
    are refused, and this file, which isn't a backup, restores nothing
    either way.  /restore once looked up only query parameters, missed the
    field and restored the shades alone, whatever was ticked.
    """
    status, _, body = upload(host, "/restore", auth(token), fields={"data": "{"})
    assert status == 500, body
    assert json.loads(body)["desc"] == "JSON parse error"


def test_a_restore_with_no_part_chosen_is_refused(host, token):
    """Checked before the file, so this file, which isn't a backup, restores nothing either way."""
    options = json.dumps({"shades": False})
    status, _, body = upload(host, "/restore", auth(token), fields={"data": options})
    assert status == 400, body
    assert json.loads(body)["desc"] == "No part of the backup was chosen, so nothing was restored."


def test_a_refused_restore_leaves_no_upload_behind(host, token):
    """A later /restore without a file must not find the refused one's upload."""
    status, _, _ = upload(host, "/restore", auth(token), fields={"data": "{"})
    assert status == 500
    status, reason = restore_without_a_file(host, token)
    assert status == 400
    assert "didn't complete" in reason, reason


def test_a_restore_brings_back_the_parts_that_were_ticked(request, host, token, connect, schema):
    if not request.config.getoption("--reboot"):
        pytest.skip("restores a backup and reboots the device: pass --reboot")
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    uptime_before = uptime(schema, frames)
    general = settings(c, schema, "general")
    ntp = {name: general.get(name, schema.default(name)) for name in ("ntpServer", "posixZone")}
    assert ntp["ntpServer"] != CHANGED_NTP_SERVER
    status, _, backup = http(host, "/backup", headers=auth(token))
    assert status == 200

    reply = op(c, schema, "saveSettings", dict(ntp, ntpServer=CHANGED_NTP_SERVER, section="general"))
    assert is_ok(schema, reply), desc(schema, reply)
    try:
        options = json.dumps({"shades": False, "settings": True})
        status, _, body = upload(host, "/restore", auth(token), file=backup, fields={"data": options})
        assert status == 200, body
        wait_for_restart(host, token, schema, uptime_before)
    finally:
        c, _, _ = synced_model(connect, schema)
        restored = settings(c, schema, "general").get("ntpServer", schema.default("ntpServer"))
        if restored != ntp["ntpServer"]:
            op(c, schema, "saveSettings", dict(ntp, section="general"))
    assert restored == ntp["ntpServer"]
