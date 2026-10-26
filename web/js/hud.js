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
    this.targetedKind = null;
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
    this.cheatNeed('bladder', local.bladder);
    this.cheatNeed('bowel', local.bowel);
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

  cheatNeed(kind, value) {
    byId(`cheat-${kind}-value`).textContent = `${Math.round(value)}%`;
    byId(`cheat-${kind}-bar`).style.width = `${clamp(value, 0, 100)}%`;
  }

  cheats(open) {
    byId('cheats').classList.toggle('hidden', !open);
  }

  wheel(open, local) {
    byId('action-wheel').classList.toggle('hidden', !open);
    if (!open || !local) return;
    const active = local.toiletAction !== 'none';
    document.querySelectorAll('[data-toilet-action]').forEach(button => {
      button.classList.toggle('hidden', active ? button.dataset.toiletAction !== 'stand' : button.dataset.toiletAction === 'stand');
    });
    document.querySelector('.wheel-core').classList.toggle('hidden', active);
    byId('wheel-piss-detail').textContent = local.male ? 'STANDING' : 'SEATED';
  }

  target(kind) {
    this.targetedKind = kind || null;
    byId('crosshair').classList.toggle('targeted', this.targetedKind === 'toilet');
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
    const busy = local.toiletAction !== 'none';
    prompt.classList.toggle('available', (near || busy) && !occupied);
    if (busy) {
      key.textContent = 'E';
      title.textContent = '[E] Stand up';
      const actions = { sit: 'SITTING · STAY A WHILE', shit: 'BOWEL DOWNLOAD', piss: local.male ? 'STANDING BLADDER RELIEF' : 'SEATED BLADDER RELIEF' };
      detail.textContent = `${actions[local.toiletAction] || 'THRONE ACTIVE'} · E / WASD STANDS`;
    } else if (near && occupied) {
      key.textContent = '⌛';
      title.textContent = 'THRONE OCCUPIED';
      detail.textContent = 'AWAIT YOUR TERRIBLE INHERITANCE · NO QUEUE-CAMPING THE BOWL';
    } else if (near) {
      key.textContent = 'E';
      title.textContent = '[E] Interact';
      detail.textContent = `CHOICE WHEEL · SIT / SHIT / PISS ${local.male ? 'STANDING' : 'SEATED'}`;
    } else {
      key.textContent = '↖';
      title.textContent = this.targetedKind === 'toilet' ? 'THRONE OUT OF RANGE' : 'MOVE CLOSER TO INTERACT';
      detail.textContent = `INTERACTION RANGE ${world.interactionRange.toFixed(1)} M · NO AUTO-WALK · F1 CHEATS`;
    }
  }

  setMode(mode) {
    this.mode = mode;
    document.querySelectorAll('[data-mode]').forEach(button => button.classList.toggle('active', button.dataset.mode === mode));
    byId('crosshair').classList.toggle('hidden', mode === 'god');
    const hints = {
      third: '<b>CLICK</b> CAPTURE MOUSE <span>·</span> <b>WASD</b> MOVE <span>·</span> <b>E</b> ACTION WHEEL <span>·</span> <b>C</b> CREATOR <span>·</span> <b>F1</b> CHEATS',
      god: '<b>WASD</b> FAST PAN <span>·</span> <b>SHIFT</b> BOOST <span>·</span> <b>LMB</b> MOVE / INTERACT IN RANGE <span>·</span> <b>C</b> CREATOR',
      first: '<b>WASD</b> MOVE <span>·</span> <b>MOUSE</b> LOOK <span>·</span> <b>HOLD F</b> PISS <span>·</span> <b>E</b> WHEEL <span>·</span> <b>C</b> CREATOR',
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
