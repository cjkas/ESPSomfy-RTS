// The shade model as socket protocol 2 keeps a client up to date with it
// (docs/PROTOCOL.md): rooms, shades, groups and repeaters from the sync, then
// changed by the device's events.  Items are inserted or updated by id, so an
// event during the sync does no harm.  It takes frames already expanded to
// names (ProtoSchema.expand) and fills in what a frame leaves out at its
// default.  The minify step joins this file into index.js; Node loads it on
// its own for the tests (test/js).
class ProtoModel {
    constructor(schema) {
        this.schema = schema;
        this.rooms = new Map();
        this.shades = new Map();
        this.groups = new Map();
        this.repeaters = [];
        this.header = {};
        this.status = {};
        this.listeners = [];
        this.seen = null;
    }
    // cb(kind, id, item): kind is room, shade, group, roomRemoved,
    // shadeRemoved, groupRemoved, order, repeaters, header or status.
    onChange(cb) { this.listeners.push(cb); }
    changed(kind, id, item) { for (const cb of this.listeners) cb(kind, id, item); }

    // A sync: the items it doesn't list are gone once it ends.
    beginSync() { this.seen = { rooms: new Set(), shades: new Set(), groups: new Set() }; }
    applySync(d) {
        if (d.header) {
            this.header = this.schema.fill(Object.assign({}, d.header), ProtoModel.FIELDS.header);
            this.changed('header', 0, this.header);
        }
        for (const r of d.rooms || []) this.putRoom(r);
        for (const s of d.shades || []) this.putShade(s);
        for (const g of d.groups || []) this.putGroup(g);
        if (d.repeaters) this.putRepeaters(d.repeaters);
        if (d.status) {
            this.putStatus(d.status, ProtoModel.FIELDS.memory);
            this.putStatus(d.status, this.header.connType === 'Ethernet' ? ProtoModel.FIELDS.ethernet : ProtoModel.FIELDS.wifi);
        }
    }
    endSync() {
        if (!this.seen) return;
        const lists = [['rooms', 'roomRemoved'], ['shades', 'shadeRemoved'], ['groups', 'groupRemoved']];
        for (const [list, removed] of lists) {
            for (const id of [...this[list].keys()]) {
                if (this.seen[list].has(id)) continue;
                this[list].delete(id);
                this.changed(removed, id);
            }
        }
        this.seen = null;
    }
    // An event by its type's name; false for one that isn't about the model.
    applyEvent(type, d) {
        switch (type) {
            case 'room': this.putRoom(d); return true;
            case 'shade': this.putShade(d); return true;
            case 'group': this.putGroup(d); return true;
            case 'shadeState': return this.mergeState(this.shades, 'shade', d.shadeId, d, ProtoModel.FIELDS.shadeState);
            case 'groupState': return this.mergeState(this.groups, 'group', d.groupId, d, ProtoModel.FIELDS.groupState);
            case 'roomRemoved': return this.remove(this.rooms, type, d.roomId || 0);
            case 'shadeRemoved': return this.remove(this.shades, type, d.shadeId || 0);
            case 'groupRemoved': return this.remove(this.groups, type, d.groupId || 0);
            case 'roomOrder': return this.order(this.rooms, d.ids || []);
            case 'shadeOrder': return this.order(this.shades, d.ids || []);
            case 'groupOrder': return this.order(this.groups, d.ids || []);
            case 'repeaters': this.putRepeaters(d.repeaters || []); return true;
            case 'memory': this.putStatus(d, ProtoModel.FIELDS.memory); return true;
            case 'wifi': this.putStatus(d, ProtoModel.FIELDS.wifi); return true;
            case 'ethernet': this.putStatus(d, ProtoModel.FIELDS.ethernet); return true;
            default: return false;
        }
    }

    // The lists in their sort order.
    roomList() { return ProtoModel.sorted(this.rooms); }
    shadeList() { return ProtoModel.sorted(this.shades); }
    groupList() { return ProtoModel.sorted(this.groups); }
    static sorted(map) { return [...map.values()].sort((a, b) => a.sortOrder - b.sortOrder); }

    putRoom(r) { this.put(this.rooms, 'room', r.roomId, r, ProtoModel.FIELDS.room, 'rooms'); }
    putShade(s) {
        // Its linked remotes leave out their defaults too.
        const linked = (s.linkedRemotes || []).map((r) => this.schema.fill(Object.assign({}, r), ProtoModel.FIELDS.linkedRemote));
        this.put(this.shades, 'shade', s.shadeId, Object.assign({}, s, { linkedRemotes: linked }), ProtoModel.FIELDS.shade, 'shades');
    }
    putGroup(g) { this.put(this.groups, 'group', g.groupId, g, ProtoModel.FIELDS.group, 'groups'); }
    // A whole item: what it had before is replaced.
    put(map, kind, id, item, fields, list) {
        id = id || 0;
        const whole = this.schema.fill(Object.assign({}, item), fields);
        map.set(id, whole);
        if (this.seen) this.seen[list].add(id);
        this.changed(kind, id, whole);
    }
    // Part of an item: only its state.  A state of an item not synced yet
    // waits for the item.
    mergeState(map, kind, id, d, fields) {
        const item = map.get(id || 0);
        if (!item) return false;
        Object.assign(item, this.schema.fill(Object.assign({}, d), fields));
        this.changed(kind, item[`${kind}Id`], item);
        return true;
    }
    remove(map, kind, id) {
        if (!map.delete(id)) return false;
        this.changed(kind, id);
        return true;
    }
    order(map, ids) {
        ids.forEach((id, i) => {
            const item = map.get(id);
            if (item) item.sortOrder = i;
        });
        this.changed('order', 0, ids);
        return true;
    }
    putRepeaters(list) {
        this.repeaters = list.slice();
        this.changed('repeaters', 0, this.repeaters);
    }
    putStatus(d, fields) {
        const part = {};
        for (const name of fields) part[name] = typeof d[name] === 'undefined' ? this.schema.defaultOf(name) : d[name];
        Object.assign(this.status, part);
        this.changed('status', 0, this.status);
    }
}
// The fields of each kind of item, as the device writes them
// (src/ProtoSync.cpp, src/outputs/ProtoSocket.cpp): what a frame that leaves
// one out means by it.
ProtoModel.FIELDS = {
    header: ['maxRooms', 'maxShades', 'maxGroups', 'maxGroupedShades', 'maxLinkedRemotes', 'startingAddress', 'bitLength',
        'proto', 'serverId', 'hostname', 'model', 'chipModel', 'fw', 'connType'],
    room: ['roomId', 'name', 'sortOrder'],
    shadeState: ['shadeId', 'position', 'target', 'direction', 'myPos', 'tiltPosition', 'tiltTarget', 'tiltDirection',
        'myTiltPos', 'flags', 'lastRollingCode'],
    groupState: ['groupId', 'flags', 'direction'],
    linkedRemote: ['remoteAddress', 'lastRollingCode'],
    group: ['groupId', 'roomId', 'name', 'remoteAddress', 'lastRollingCode', 'bitLength', 'proto', 'sunSensor',
        'flipCommands', 'flags', 'repeats', 'sortOrder', 'linkedShades'],
    memory: ['freeHeap', 'maxHeap', 'minHeap', 'totalHeap', 'uptime', 'loopRate', 'loopMaxUs'],
    wifi: ['ssid', 'strength', 'channel'],
    ethernet: ['connected', 'speed', 'fullDuplex']
};
ProtoModel.FIELDS.shade = ProtoModel.FIELDS.shadeState.concat(['roomId', 'name', 'remoteAddress', 'upTime', 'downTime',
    'tiltTime', 'stepSize', 'paired', 'tiltType', 'shadeType', 'bitLength', 'proto', 'flipCommands', 'flipPosition',
    'inGroup', 'sunSensor', 'light', 'simMy', 'repeats', 'sortOrder', 'gpioUp', 'gpioDown', 'gpioMy', 'gpioLLTrigger',
    'linkedRemotes']);
if (typeof module !== 'undefined' && module.exports) module.exports = { ProtoModel };
