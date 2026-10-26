const byId = id => document.getElementById(id);
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

function needStatus(kind, value) {
  if (value < 25) return kind === 'bladder' ? 'SUSPICIOUSLY STABLE' : 'INTESTINAL PEACE';
  if (value < 50) return kind === 'bladder' ? 'LIQUID ASSETS DETECTED' : 'BROWN NOISE RISING';
  if (value < 75) return 'PRESSURE RISING';
  if (value < 90) return 'URGENT: LOCATE THRONE';
  return 'CRITICAL: ABANDON DIGNITY';
}

export class HUD {
  constructor() {
    this.lastNotice = 0;
    this.lastEvent = 0;
    this.toastTimer = null;
    this.mode = 'third';
    this.map = byId('minimap').getContext('2d');
  }

  network(status) {
    const dot = byId('net-dot');
    dot.className = `status-dot ${status}`;
    byId('net-status').textContent = status === 'online' ? 'CITY NET // ONLINE' :
      status === 'offline' ? 'CITY NET // RECONNECTING' : 'DIALING THE VOID';
  }

  welcome(message, identity) {
    byId('loading').classList.add('done');
    byId('citizen-id').textContent = `CITIZEN ${String(message.id).padStart(3, '0')}`;
    byId('citizen-name').textContent = identity.toUpperCase();
    byId('player-name').value = identity;
  }

  identity(name) {
    byId('citizen-name').textContent = name.toUpperCase();
  }

  update(state, localId, world) {
    const local = state.characters.find(character => character.id === localId);
    byId('population').textContent = state.characters.length;
    this.events(state.events);
    if (!local) return;
    byId('citizen-name').textContent = local.name.toUpperCase();
    byId('identity-swatch').style.background = local.color;
    byId('ai-tag').textContent = local.ai ? 'AI' : 'HUMAN?';
    this.need('bladder', local.bladder);
    this.need('bowel', local.bowel);
    this.context(local, state, world);
    this.minimap(state, localId, world);
    if (local.notice > this.lastNotice) {
      this.lastNotice = local.notice;
      if (local.noticeText) this.toast(local.noticeText);
    }
    byId('piss-indicator').classList.toggle('hidden', !local.pissing || this.mode !== 'first');
  }

  need(kind, value) {
    const rounded = Math.round(value);
    byId(`${kind}-value`).textContent = `${rounded}%`;
    byId(`${kind}-bar`).style.width = `${clamp(value, 0, 100)}%`;
    byId(`${kind}-state`).textContent = needStatus(kind, value);
    byId(`${kind}-need`).classList.toggle('critical', value >= 90);
  }

  context(local, state, world) {
    const prompt = byId('context-prompt');
    const key = document.querySelector('.context-key');
    const title = byId('context-title');
    const detail = byId('context-detail');
    const dx = local.x - world.toilet.frontX;
    const dz = local.z - world.toilet.frontZ;
    const near = Math.hypot(dx, dz) <= world.interactionRange;
    const occupied = state.toiletUser !== 0 && state.toiletUser !== local.id;
    prompt.classList.toggle('available', near && !occupied && !local.onToilet);
    if (local.onToilet) {
      key.textContent = '…';
      title.textContent = 'VOIDING BIOLOGICAL PAYLOAD';
      detail.textContent = 'MULTIPURPOSE THRONE ACTIVE · PLEASE KEEP YOUR HANDS INSIDE THE BLOODLINE';
    } else if (near && occupied) {
      key.textContent = '⌛';
      title.textContent = 'THRONE OCCUPIED';
      detail.textContent = 'AWAIT YOUR TERRIBLE INHERITANCE · CLICK TO QUEUE';
    } else if (near) {
      key.textContent = 'E';
      title.textContent = 'USE PORCELAIN THRONE';
      detail.textContent = 'EMPTIES BLADDER + BOWEL · SATISFACTION NOT GUARANTEED';
    } else if (this.mode === 'first') {
      key.textContent = 'F';
      title.textContent = 'HOLD TO COMMIT LIQUID HERESY';
      detail.textContent = 'E: APPROACH THRONE · CLICK VIEW: CAPTURE MOUSE · ESC: RELEASE';
    } else {
      key.textContent = '↖';
      title.textContent = 'CLICK THRONE TO AUTO-WALK';
      detail.textContent = `INTERACTION RANGE ${world.interactionRange.toFixed(1)} M · CLICK FLOOR TO PATHFIND`;
    }
  }

  setMode(mode) {
    this.mode = mode;
    document.querySelectorAll('[data-mode]').forEach(button => button.classList.toggle('active', button.dataset.mode === mode));
    byId('crosshair').classList.toggle('hidden', mode === 'god');
    const hints = {
      third: '<b>CLICK</b> CAPTURE MOUSE <span>·</span> <b>WASD</b> MOVE <span>·</span> <b>MOUSE</b> ORBIT <span>·</span> <b>E</b> INTERACT',
      god: '<b>WASD</b> FAST PAN <span>·</span> <b>SHIFT</b> BOOST <span>·</span> <b>LMB</b> MOVE CITIZEN / INTERACT <span>·</span> <b>WHEEL</b> ZOOM',
      first: '<b>WASD</b> MOVE <span>·</span> <b>MOUSE</b> LOOK <span>·</span> <b>HOLD F</b> PISS <span>·</span> <b>E</b> THRONE',
    };
    byId('control-hint').innerHTML = hints[mode];
    byId('piss-indicator').classList.add('hidden');
  }

  events(events) {
    for (const event of events) {
      if (event.id <= this.lastEvent) continue;
      this.lastEvent = event.id;
      const item = document.createElement('div');
      item.className = 'feed-item new';
      const tag = document.createElement('b');
      tag.textContent = 'CITY//FEED';
      item.append(tag, document.createTextNode(event.text));
      byId('event-feed').prepend(item);
      setTimeout(() => item.classList.remove('new'), 1800);
    }
    const feed = byId('event-feed');
    while (feed.children.length > 4) feed.lastElementChild.remove();
  }

  toast(text) {
    const toast = byId('toast');
    toast.textContent = text;
    toast.classList.add('visible');
    clearTimeout(this.toastTimer);
    this.toastTimer = setTimeout(() => toast.classList.remove('visible'), 2800);
  }

  minimap(state, localId, world) {
    const context = this.map;
    const width = context.canvas.width;
    const height = context.canvas.height;
    const scale = Math.min((width - 18) / world.width, (height - 18) / world.depth);
    const project = (x, z) => ({ x: width / 2 + x * scale, y: height / 2 + z * scale });
    context.clearRect(0, 0, width, height);
    context.fillStyle = '#09100b';
    context.fillRect(0, 0, width, height);
    const origin = project(-world.width / 2, -world.depth / 2);
    context.strokeStyle = '#435348';
    context.lineWidth = 2;
    context.strokeRect(origin.x, origin.y, world.width * scale, world.depth * scale);
    context.strokeStyle = 'rgba(116,143,119,.09)';
    context.lineWidth = 1;
    for (let x = -8; x <= 8; x += 2) {
      const a = project(x, -7), b = project(x, 7);
      context.beginPath(); context.moveTo(a.x, a.y); context.lineTo(b.x, b.y); context.stroke();
    }
    for (const puddle of state.puddles) {
      const point = project(puddle.x, puddle.z);
      context.fillStyle = 'rgba(220,199,61,.5)';
      context.beginPath(); context.arc(point.x, point.y, Math.max(2, puddle.size * scale * 0.45), 0, Math.PI * 2); context.fill();
    }
    for (const obstacle of world.obstacles) {
      if (obstacle.kind === 'boundary') continue;
      const point = project(obstacle.x, obstacle.z);
      context.fillStyle = obstacle.kind === 'toilet' ? '#d7ff54' : '#526158';
      context.fillRect(point.x - obstacle.hx * scale, point.y - obstacle.hz * scale,
        obstacle.hx * 2 * scale, obstacle.hz * 2 * scale);
    }
    for (const character of state.characters) {
      const point = project(character.x, character.z);
      const local = character.id === localId;
      context.fillStyle = local ? '#d7ff54' : character.ai ? '#ff765f' : character.color;
      context.beginPath(); context.arc(point.x, point.y, local ? 4 : 3, 0, Math.PI * 2); context.fill();
      context.strokeStyle = context.fillStyle;
      context.beginPath(); context.moveTo(point.x, point.y);
      context.lineTo(point.x + Math.sin(character.yaw) * 7, point.y + Math.cos(character.yaw) * 7); context.stroke();
    }
  }

  help(show) {
    byId('help').classList.toggle('hidden', !show);
  }

  fatal(message) {
    byId('fatal-message').textContent = message;
    byId('fatal').classList.remove('hidden');
  }
}
