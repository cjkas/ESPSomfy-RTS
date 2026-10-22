# Socket protocol 2

**Status:** outline. The type numbers, field keys and payloads are filled in with the firmware protocol core. Why the protocol looks like this: [ADR 0005](adr/0005-one-websocket-protocol.md). Terms: [GLOSSARY.md](../GLOSSARY.md).

## Connection

- `ws://host/ws` on port 80. Under Secured the upgrade needs `?apikey=<token>` ([ADR 0006](adr/0006-two-security-modes-eleven-http-routes.md)).
- Optional `?schema=<hash>`: the client already has that schema and doesn't need it again.
- Text frames only. A binary encoding, if one comes, would be asked for at the upgrade (for example `?enc=bin`) and use binary frames.

## Envelope

Every frame, in both directions, is one JSON object:

```json
{"v":2,"t":20,"id":17,"d":{}}
```

| Key | Meaning |
|---|---|
| `v` | protocol version |
| `t` | message type, a number from the table below |
| `id` | only on an op and its reply: the client's number for the op |
| `d` | the payload, with short keys from the schema |

Within `d`, booleans are `1`/`0` and fields at their default are left out. Keys are one or two characters from one dictionary shared by every type.

## Message types

| Range | Sender | Kinds |
|---|---|---|
| 1–15 | either | control: `hello`, `reply`, `next`, `sync`, `syncEnd`, `subscribe`, `unsubscribe` |
| 16–127 | client | ops: the commands, reads, model edits and `getSettings`/`saveSettings` |
| 128–255 | device | events: `shadeState`, `shade`, `group`, `room`, `*Removed`, `settingsChanged`, status, feeds |

A number is never reused. A retired type stays reserved.

## Connect sequence

1. The device sends `hello`: protocol version, firmware version, schema hash.
2. Unless the client passed the matching `?schema=`, the sync starts with the schema: types, fields (key, name, kind, default) and error codes.
3. Sync: header, rooms, shades, groups, repeaters, status. Each message is at most 4 KB, items are never split, and the client sends `next` after each one.
4. `syncEnd`. From then on, events keep the client current. Events also arrive during the sync, so the client upserts by id.

## Replies

Every op gets exactly one reply, to its sender only: `{"ok":1, ...}`, or `{"ok":0,"code":<n>,"desc":"..."}`. The error codes are bad request, unknown op, unauthorized, not found, busy, radio unavailable and internal; their numbers are in the schema.

## Feeds

`subscribe`/`unsubscribe` with a feed: `frames` (received remote frames) or `frequencyScan`. Feed events go only to subscribed clients.
