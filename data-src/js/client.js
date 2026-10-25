// A socket protocol 2 client (docs/PROTOCOL.md): ws://host/ws, hello, the sync
// with next after each frame, ops answered by their reply, events and feeds.
// It keeps the shade model (ProtoModel) up to date and connects again when the
// socket closes.  The WebSocket, the timers and the schema's storage can be
// handed in, so the tests (test/js) run it without a device.  The minify step
// joins this file into index.js after schema.js and model.js.
class ProtoError extends Error {
    // code is an error name from the schema (badRequest, busy ...), or
    // notConnected, closed or timeout from the client itself.
    constructor(code, desc, data) {
        super(desc || code);
        this.code = code;
        this.desc = desc || code;
        this.data = data || {};
    }
}
class ProtoClient {
    // opts: host, token, WebSocket, storage (getItem/setItem, for the schema),
    // setTimeout/clearTimeout, opTimeoutMs.
    constructor(opts) {
        const deps = ProtoClient.deps();
        this.Schema = deps.ProtoSchema;
        this.host = opts.host;
        this.token = opts.token || '';
        this.WebSocket = opts.WebSocket || (typeof WebSocket !== 'undefined' ? WebSocket : null);
        this.storage = opts.storage || null;
        this.setTimeout = opts.setTimeout || ((fn, ms) => setTimeout(fn, ms));
        this.clearTimeout = opts.clearTimeout || ((t) => clearTimeout(t));
        this.opTimeoutMs = opts.opTimeoutMs || 10000;
        this.schema = this.loadSchema() || new this.Schema();
        this.model = new deps.ProtoModel(this.schema);
        this.model.onChange((kind, id, item) => this.emit('change', { kind: kind, id: id, item: item }));
        this.listeners = {};
        this.pending = new Map();
        this.nextId = 1;
        this.ws = null;
        this.ready = false;
        this.opened = false;
        this.stopped = true;
        this.retries = 0;
        this.reconnectTimer = null;
        this.held = [];
        this.fw = null;
    }
    // In the browser the classes are the ones index.js was joined with; in
    // Node they are loaded.
    static deps() {
        if (typeof require === 'function' && typeof module !== 'undefined' && module.exports)
            return { ProtoSchema: require('./schema.js').ProtoSchema, ProtoModel: require('./model.js').ProtoModel };
        return { ProtoSchema: ProtoSchema, ProtoModel: ProtoModel };
    }

    // cb(d) for a device event by its name (shadeState, frame ...), and for
    // the client's own: open, hello, synced, change, refused (closed before it
    // opened: the token may have been refused), closed, versionMismatch and
    // error (a refusal the device couldn't tie to an op).
    on(name, cb) { (this.listeners[name] = this.listeners[name] || []).push(cb); }
    emit(name, d) { for (const cb of this.listeners[name] || []) cb(d); }

    connect() {
        this.stopped = false;
        this.open();
    }
    close() {
        this.stopped = true;
        if (this.reconnectTimer) this.clearTimeout(this.reconnectTimer);
        this.reconnectTimer = null;
        if (this.ws) this.ws.close();
    }
    // A new token, from a login: the device checks it as the socket opens.
    setToken(token) {
        this.token = token || '';
        if (this.ws) this.ws.close();
        else if (!this.stopped) this.open();
    }
    url() {
        const params = [];
        if (this.token) params.push(`apikey=${encodeURIComponent(this.token)}`);
        if (this.schema.hash) params.push(`schema=${encodeURIComponent(this.schema.hash)}`);
        return `ws://${this.host}/ws${params.length ? '?' + params.join('&') : ''}`;
    }
    open() {
        this.reconnectTimer = null;
        this.ready = false;
        this.opened = false;
        this.held = [];
        const ws = new this.WebSocket(this.url());
        this.ws = ws;
        ws.onopen = () => {
            this.opened = true;
            this.retries = 0;
            this.emit('open');
        };
        ws.onmessage = (evt) => this.receive(evt.data);
        ws.onclose = () => {
            if (this.ws !== ws) return;
            this.ws = null;
            this.ready = false;
            for (const [, p] of this.pending) {
                this.clearTimeout(p.timer);
                p.reject(new ProtoError('closed', 'The connection closed'));
            }
            this.pending.clear();
            this.emit(this.opened ? 'closed' : 'refused');
            if (this.stopped) return;
            // Soon after a close, slower after each failure, at most 10 s.
            const ms = Math.min(500 * Math.pow(2, this.retries++), 10000);
            this.reconnectTimer = this.setTimeout(() => this.open(), ms);
        };
    }
    send(type, d, id) {
        const frame = { v: ProtoClient.VERSION, t: this.schema.typeNumbers[type] };
        if (typeof frame.t === 'undefined') throw new ProtoError('unknownOp', `The device has no message ${type}`);
        if (id) frame.id = id;
        if (d) frame.d = d;
        this.ws.send(JSON.stringify(frame));
    }

    // Sends an op, or a control message that wants a reply, by its name with
    // its fields by name.  Resolves with the reply's fields, rejects with a
    // ProtoError.
    op(name, fields) {
        return new Promise((resolve, reject) => {
            if (!this.ready || !this.ws) {
                reject(new ProtoError('notConnected', 'Not connected to the device'));
                return;
            }
            const id = this.nextId++;
            try {
                this.send(name, this.schema.pack(fields || {}), id);
            } catch (err) {
                reject(err instanceof ProtoError ? err : new ProtoError('badRequest', err.message));
                return;
            }
            const timer = this.setTimeout(() => {
                this.pending.delete(id);
                reject(new ProtoError('timeout', `No reply to ${name}`));
            }, this.opTimeoutMs);
            this.pending.set(id, { resolve: resolve, reject: reject, timer: timer });
        });
    }
    subscribe(feed) { return this.op('subscribe', { feed: this.schema.feedNumbers[feed] }); }
    unsubscribe(feed) { return this.op('unsubscribe', { feed: this.schema.feedNumbers[feed] }); }
    // The shade model and status again, without the schema.
    resync() {
        this.model.beginSync();
        return this.op('sync');
    }

    receive(text) {
        let frame;
        try {
            frame = JSON.parse(text);
        } catch (err) {
            return;
        }
        if (frame.v !== ProtoClient.VERSION) {
            this.emit('versionMismatch', { device: frame.v, client: ProtoClient.VERSION });
            return;
        }
        const d = frame.d || {};
        // hello, the sync and syncEnd have fixed numbers: they come before the
        // schema.
        switch (frame.t) {
            case 1: this.hello(d); return;
            case 4: this.syncFrame(d); return;
            case 5: this.syncEnd(); return;
            default: break;
        }
        if (!this.ready) this.held.push(frame);
        else this.dispatch(frame);
    }
    hello(d) {
        this.fw = d.fw;
        // A schema cached for another firmware is no use: the device sends the
        // one it has, as it didn't get its hash.
        if (this.schema.hash !== d.sh) {
            this.schema = new this.Schema();
            this.model.schema = this.schema;
        }
        this.schema.hash = d.sh;
        this.model.beginSync();
        this.emit('hello', d);
    }
    syncFrame(d) {
        this.schema.add(d);
        const device = {};
        for (const key of Object.keys(d)) if (!['T', 'F', 'E', 'Q'].includes(key)) device[key] = d[key];
        // The schema's rows come first, so a frame with the device's sections
        // has the whole of it.
        if (Object.keys(device).length) this.model.applySync(this.schema.expand(device));
        this.ws.send(JSON.stringify({ v: ProtoClient.VERSION, t: 3 }));
    }
    syncEnd() {
        this.model.endSync();
        if (!this.ready) {
            this.ready = true;
            this.saveSchema();
            const held = this.held;
            this.held = [];
            for (const frame of held) this.dispatch(frame);
        }
        this.emit('synced', this.model);
    }
    dispatch(frame) {
        const d = this.schema.expand(frame.d || {});
        if (frame.t === 2) {
            const p = typeof frame.id !== 'undefined' ? this.pending.get(frame.id) : null;
            if (!p) {
                if (!d.ok) this.emit('error', new ProtoError(this.schema.errors[d.code] || 'internal', d.desc, d));
                return;
            }
            this.pending.delete(frame.id);
            this.clearTimeout(p.timer);
            if (d.ok) p.resolve(d);
            else p.reject(new ProtoError(this.schema.errors[d.code] || 'internal', d.desc, d));
            return;
        }
        const name = this.schema.types[frame.t];
        if (!name) return;
        this.model.applyEvent(name, d);
        this.emit(name, d);
    }

    loadSchema() {
        if (!this.storage) return null;
        try {
            return this.Schema.load(JSON.parse(this.storage.getItem(ProtoClient.SCHEMA_KEY)));
        } catch (err) {
            return null;
        }
    }
    saveSchema() {
        if (!this.storage) return;
        try {
            this.storage.setItem(ProtoClient.SCHEMA_KEY, JSON.stringify(this.schema.save()));
        } catch (err) { /* Private windows may have no storage: the device sends the schema each time. */ }
    }
}
ProtoClient.VERSION = 2;
ProtoClient.SCHEMA_KEY = 'espsomfyProtoSchema';
if (typeof module !== 'undefined' && module.exports) module.exports = { ProtoClient, ProtoError };
