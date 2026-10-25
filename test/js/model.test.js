// The web UI's ProtoModel (data-src/js/model.js): the recorded sync, then
// events.
const test = require('node:test');
const assert = require('node:assert/strict');
const { ProtoModel } = require('../../data-src/js/model.js');
const { recordedSchema, deviceSections, recordedItems } = require('./helpers.js');

function synced() {
    const schema = recordedSchema();
    const model = new ProtoModel(schema);
    model.beginSync();
    for (const d of deviceSections(schema)) model.applySync(d);
    model.endSync();
    return model;
}

test('the sync fills every room, shade and group', () => {
    const m = synced();
    assert.equal(m.rooms.size, recordedItems('R').length);
    assert.equal(m.shades.size, recordedItems('S').length);
    assert.equal(m.groups.size, recordedItems('G').length);
    assert.equal(m.header.model, 'ESPSomfyRTS');
    assert.equal(m.header.maxShades, 32);
    assert.ok(m.status.freeHeap > 0);
});

test('a field the sync left out is its default', () => {
    const m = synced();
    const raw = recordedItems('S').find((s) => typeof s.m === 'undefined');
    const shade = m.shades.get(raw.s);
    // Left out because they were at their default.
    assert.equal(shade.myPos, -1);
    assert.equal(shade.stepSize, raw.z === undefined ? 100 : raw.z);
    assert.equal(typeof shade.paired, 'boolean');
    assert.ok(Array.isArray(shade.linkedRemotes));
    for (const r of shade.linkedRemotes) assert.equal(typeof r.lastRollingCode, 'number');
    for (const g of m.groupList()) assert.ok(Array.isArray(g.linkedShades));
});

test('the lists come in their sort order', () => {
    const m = synced();
    const orders = m.shadeList().map((s) => s.sortOrder);
    assert.deepEqual(orders, [...orders].sort((a, b) => a - b));
});

test('a state event changes only the state, and a left-out field is its default', () => {
    const m = synced();
    const shade = m.shadeList()[0];
    const name = shade.name;
    // A shade at rest at the top: position 0 is left out.
    assert.ok(m.applyEvent('shadeState', { shadeId: shade.shadeId, target: 40, direction: 1 }));
    assert.equal(m.shades.get(shade.shadeId).position, 0);
    assert.equal(m.shades.get(shade.shadeId).target, 40);
    assert.equal(m.shades.get(shade.shadeId).name, name);
    // Of a shade the client doesn't have yet: nothing to change.
    assert.equal(m.applyEvent('shadeState', { shadeId: 250, position: 5 }), false);
});

test('a whole item replaces what was there', () => {
    const m = synced();
    const shade = m.shadeList()[0];
    m.applyEvent('shade', { shadeId: shade.shadeId, name: 'Renamed' });
    assert.equal(m.shades.get(shade.shadeId).name, 'Renamed');
    assert.equal(m.shades.get(shade.shadeId).upTime, 0);
    m.applyEvent('room', { roomId: 9, name: 'New' });
    assert.equal(m.rooms.get(9).sortOrder, 0);
});

test('removals, orders and repeaters', () => {
    const m = synced();
    const changes = [];
    m.onChange((kind, id) => changes.push([kind, id]));
    const [first, second] = m.shadeList();
    assert.ok(m.applyEvent('shadeRemoved', { shadeId: first.shadeId }));
    assert.equal(m.shades.has(first.shadeId), false);
    // An id it doesn't have is ignored.
    assert.equal(m.applyEvent('roomRemoved', { roomId: 250 }), false);
    m.applyEvent('shadeOrder', { ids: [second.shadeId] });
    assert.equal(m.shades.get(second.shadeId).sortOrder, 0);
    m.applyEvent('repeaters', { repeaters: [7, 8] });
    assert.deepEqual(m.repeaters, [7, 8]);
    assert.deepEqual(changes.map((c) => c[0]), ['shadeRemoved', 'order', 'repeaters']);
});

test('a sync drops what it no longer lists, and keeps what an event added during it', () => {
    const m = synced();
    const schema = m.schema;
    m.applyEvent('room', { roomId: 99, name: 'Gone' });
    m.beginSync();
    m.applyEvent('room', { roomId: 98, name: 'Added during the sync' });
    for (const d of deviceSections(schema)) m.applySync(d);
    m.endSync();
    assert.equal(m.rooms.has(99), false);
    assert.equal(m.rooms.has(98), true);
});

test('status events', () => {
    const m = synced();
    m.applyEvent('memory', { freeHeap: 1000, uptime: 5 });
    assert.equal(m.status.freeHeap, 1000);
    assert.equal(m.status.loopRate, 0);
    m.applyEvent('wifi', { strength: -60, channel: 6 });
    assert.equal(m.status.ssid, '');
    assert.equal(m.status.strength, -60);
    assert.equal(m.applyEvent('frame', { remoteAddress: 1 }), false);
});
