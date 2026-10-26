import { normalizeAppearance, savedAppearance, saveAppearance } from './appearance.js';

const FIRST = ['Dumpster', 'Crusader', 'Moist', 'Turbo', 'Baron', 'Soggy', 'Cyber', 'Illegitimate'];
const LAST = ['Kevin', 'ofVenlo', 'Pissington', 'McFlushed', 'theUnflushed', 'vonBidet', 'Sock', '1453'];

export function defaultIdentity() {
  try {
    const saved = localStorage.getItem('gtc-identity');
    if (saved) return saved.slice(0, 18);
  } catch { /* Private browsing can deny storage. */ }
  return `${FIRST[Math.floor(Math.random() * FIRST.length)]}${LAST[Math.floor(Math.random() * LAST.length)]}`;
}

export function saveIdentity(name) {
  try { localStorage.setItem('gtc-identity', name); } catch { /* Nonessential. */ }
}

export class Network {
  constructor(identity) {
    this.identity = identity;
    this.appearance = savedAppearance();
    this.id = 0;
    this.world = null;
    this.socket = null;
    this.retry = null;
    this.onWelcome = () => {};
    this.onState = () => {};
    this.onStatus = () => {};
  }

  connect() {
    clearTimeout(this.retry);
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    this.onStatus('connecting');
    const socket = new WebSocket(`${protocol}//${location.host}/ws`);
    this.socket = socket;
    socket.addEventListener('open', () => {
      if (socket === this.socket) this.onStatus('online');
    });
    socket.addEventListener('message', event => {
      if (socket !== this.socket) return;
      let message;
      try { message = JSON.parse(event.data); } catch { return; }
      if (message.type === 'welcome') {
        this.id = message.id;
        this.world = message.world;
        this.send({ type: 'join', name: this.identity });
        if (this.appearance) this.send({ type: 'customize', ...this.appearance });
        this.onWelcome(message);
      } else if (message.type === 'state') {
        this.onState(message);
      }
    });
    socket.addEventListener('close', () => {
      if (socket !== this.socket) return;
      this.onStatus('offline');
      this.id = 0;
      this.retry = setTimeout(() => this.connect(), 1800);
    });
    socket.addEventListener('error', () => {
      if (socket === this.socket) this.onStatus('offline');
    });
  }

  send(message) {
    if (this.socket?.readyState === WebSocket.OPEN) this.socket.send(JSON.stringify(message));
  }

  input(input) { this.send({ type: 'input', ...input }); }
  path(point) { this.send({ type: 'path', x: point.x, z: point.z }); }
  toilet(action = 'sit') { this.send({ type: 'toilet', action }); }
  cheat(need, delta) { this.send({ type: 'cheat', need, delta }); }

  customize(appearance) {
    this.appearance = normalizeAppearance(appearance);
    saveAppearance(this.appearance);
    this.send({ type: 'customize', ...this.appearance });
  }

  rename(name) {
    this.identity = name;
    saveIdentity(name);
    this.send({ type: 'join', name });
  }
}
