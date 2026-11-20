/* Triforce launcher: the page. It draws, plays the sounds and takes the mouse, keyboard and controller; everything real
   (reading the game, building, updating, saves, mods, settings) is the launcher's, behind messages:

   page -> launcher  {t:'ready'}  {t:'win', a:'min'|'max'|'close'}  {t:'pick', kind:'file'|'folder'}  {t:'drop', files:[file:// on Linux]}
                     {t:'cmd', c:<name>, ...args}   (build, stop, retry, play, update, playold, showgame, open, lang, folder, setting,
                                                    saves.import, saves.export, saves.open, mods.*, bench, benchstop, benchapply,
                                                    report, sound, workback, ...)
   launcher -> page  {t:'hello', chrome, os}   {t:'state', ...parts}   {t:'go', s}   {t:'check', st, name, msg}
                     {t:'work', ...}   {t:'log', lines}   {t:'toast', html}   {t:'sfx', id}   {t:'bench', ...}   {t:'max', on}

   Without a launcher (a plain browser) demo.js answers instead, so the design can be looked at anywhere. */
'use strict';
const $ = (s, r = document) => r.querySelector(s), $$ = (s, r = document) => [...r.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

/* ---------------- the window this page lives in ---------------- */
const HOST = window.chrome && window.chrome.webview ? 'win' : (window.external && typeof window.external.sendMessage === 'function' ? 'linux' : null);
const hostHandlers = [];
function onHost(fn) { hostHandlers.push(fn); }
function fromHost(s) { let m; try { m = typeof s === 'string' ? JSON.parse(s) : s; } catch { return; } hostHandlers.forEach(f => { try { f(m); } catch (e) { console.error(e); } }); }
function send(o) {
  const s = JSON.stringify(o);
  if (HOST === 'win') window.chrome.webview.postMessage(s);
  else if (HOST === 'linux') window.external.sendMessage(s);
  else if (window.DEMO) window.DEMO.receive(o, fromHost);
}
function cmd(c, args) { send(Object.assign({ t: 'cmd', c }, args || {})); }
if (HOST === 'win') window.chrome.webview.addEventListener('message', e => fromHost(e.data));
else if (HOST === 'linux') window.external.receiveMessage(fromHost);
document.documentElement.classList.add(HOST ? 'hosted' : 'browser', 'noart');   // (no game art until the launcher says there is)
if (HOST) document.documentElement.classList.add('host-' + HOST);
window.addEventListener('error', e => send({ t: 'log', s: 'page error: ' + e.message + ' @' + e.lineno }));

/* ---------------- fit the 1280x800 design into the window ---------------- */
function fit() { const k = Math.min(innerWidth / 1280, innerHeight / 800); $('#fit').style.transform = `translate(-50%,-50%) scale(${k})`; }
addEventListener('resize', fit); fit();

/* ---------------- the launcher's state, as last sent ---------------- */
const S = { media: { art: false, music: false, sfx: false }, sound: { music: .4, sfx: .7, muted: false }, files: [], settings: [], mods: { tab: 'browse', items: [] } };

/* ---------------- sound: the game's own music and menu sounds (only once a game is known) ---------------- */
const TRACKS = { saria: "Saria's Song", fairy: 'Fairy Fountain', temple: 'Temple of Time', itemget: 'Item get' };
const live = [];   // music being played: { el, k, target, rate, name }
let curTrack = null, unlocked = !!HOST;
addEventListener('pointerdown', () => unlocked = true, true); addEventListener('keydown', () => unlocked = true, true);
function music(name, loop = true, fade = 1.4) {
  if (name && !S.media.music) name = null;
  if (curTrack === name) return;
  curTrack = name;
  for (const m of live) { m.target = 0; m.rate = 1 / fade; }
  if (name) {
    const el = new Audio('audio/' + name + '.wav'); el.loop = loop; el.volume = 0;
    const m = { el, k: 0, target: 1, rate: 1 / fade, name };
    el.play().catch(() => { m.blocked = true; });
    if (!loop) el.onended = () => { m.target = 0; m.k = 0; if (curTrack === name) { curTrack = null; afterOneShot(name); } };
    live.push(m);
  }
  nowPlaying();
}
let lastT = performance.now();
function audioTick(t) {
  const dt = Math.min(.1, (t - lastT) / 1000); lastT = t;
  const mv = S.sound.muted ? 0 : S.sound.music;
  for (let i = live.length - 1; i >= 0; i--) {
    const m = live[i];
    m.k += Math.sign(m.target - m.k) * Math.min(Math.abs(m.target - m.k), m.rate * dt);
    m.el.volume = Math.max(0, Math.min(1, m.k * m.k * mv));
    if (m.k <= 0 && m.target === 0) { m.el.pause(); m.el.removeAttribute('src'); live.splice(i, 1); }
    else if (m.blocked && unlocked) { m.blocked = false; m.el.play().catch(() => { m.blocked = true; }); }
  }
}
const SFX_IDS = ['cursor', 'tab_l', 'tab_r', 'decide', 'decide_l', 'cancel', 'open', 'close', 'success', 'error'];
let sfxPool = null, lastCursor = 0;
function loadSfx() {
  if (sfxPool || !S.media.sfx) return;
  sfxPool = {};
  SFX_IDS.forEach(n => { sfxPool[n] = { i: 0, a: [0, 1, 2].map(() => { const a = new Audio('audio/' + n + '.wav'); a.preload = 'auto'; return a; }) }; });
}
function sfx(n) {
  if (!sfxPool || S.sound.muted || !S.sound.sfx) return;
  if (n === 'cursor') { const t = performance.now(); if (t - lastCursor < 55) return; lastCursor = t; }
  const p = sfxPool[n]; if (!p) return;
  const a = p.a[p.i++ % 3]; a.currentTime = 0; a.volume = S.sound.sfx * (n === 'cursor' ? .8 : 1); a.play().catch(() => {});
}
function nowPlaying() {
  $('#nowT').textContent = curTrack ? TRACKS[curTrack] : S.media.music ? 'Nothing playing' : 'Music comes from your game once it is added';
  $('#nowp').classList.toggle('paused', !curTrack || S.sound.muted);
  $('#spk').classList.toggle('muted', S.sound.muted);
  $('#muteB').textContent = S.sound.muted ? 'Unmute (M)' : 'Mute (M)';
}
let soundSave = 0;
function setVol(which, v, quiet) {
  S.sound[which] = Math.round(Math.max(0, Math.min(1, v)) * 100) / 100;
  $$(`[data-vol="${which}"]`).forEach(s => { s.querySelector('i').style.width = S.sound[which] * 100 + '%'; s.querySelector('u').style.left = S.sound[which] * 100 + '%'; });
  $$(`[data-volv="${which}"]`).forEach(s => s.textContent = Math.round(S.sound[which] * 100) + '%');
  if (!quiet) { clearTimeout(soundSave); soundSave = setTimeout(() => cmd('sound', S.sound), 400); }
}
function toggleMute() { S.sound.muted = !S.sound.muted; nowPlaying(); if (!S.sound.muted) sfx('decide'); cmd('sound', S.sound); }
document.addEventListener('pointerdown', e => {
  const s = e.target.closest('.slider[data-vol]'); if (!s) return;
  e.preventDefault(); setFocus(s, true);
  const which = s.dataset.vol;
  const move = ev => { const b = s.getBoundingClientRect(); setVol(which, (ev.clientX - b.left) / b.width); };
  move(e);
  const up = () => { removeEventListener('pointermove', move); removeEventListener('pointerup', up); if (which === 'sfx') sfx('cursor'); };
  addEventListener('pointermove', move); addEventListener('pointerup', up);
});

/* ---------------- screens ---------------- */
const MAIN = ['home', 'mods', 'saves', 'settings'];
const FIRST_RUN = ['welcome', 'reveal', 'done'];
const MUSIC = { welcome: null, reveal: 'saria', work: 'temple', done: 'itemget', home: 'fairy', saves: 'fairy', mods: 'fairy', settings: 'fairy', bench: 'fairy', brun: null, playing: null };
let screen = null;
function go(name) {
  if (name === screen || !$('#s-' + name)) return;
  closeSheet(true); closeVol(true);
  screen = name;
  $$('.screen').forEach(s => s.classList.toggle('on', s.id === 's-' + name));
  const busy = FIRST_RUN.includes(name) || name === 'work' || name === 'brun' || name === 'playing';
  $('#win').classList.toggle('hidebits', busy);
  $('#bar').classList.toggle('bare', name === 'brun');
  const tab = $('#s-' + name).dataset.tab;
  $$('#nav a').forEach((a, i) => a.classList.toggle('on', String(i) === tab));
  const m = MUSIC[name];
  if (name === 'done') music('itemget', false, .3);
  else if (!(m === 'fairy' && curTrack === 'itemget')) music(m, true, name === 'playing' || name === 'brun' ? 2.2 : 1.4);
  setFocus($('#' + $('#s-' + name).dataset.first) || focusables()[0], false);
  if (name === 'mods' && !S.mods.loaded) cmd('mods.list', { tab: S.mods.tab, q: '' });
  if (name === 'saves') cmd('saves.list');
  cmd('screen', { s: name });
}
function afterOneShot(name) { if (name === 'itemget' && (MUSIC[screen] === 'fairy' || screen === 'done')) music('fairy'); }

/* ---------------- focus: mouse, keyboard and controller share one focus ---------------- */
let focused = null;
function visible(el) { return !!(el.offsetParent || el.getClientRects().length) && getComputedStyle(el).visibility !== 'hidden'; }
function focusables() {
  if ($('#sheetBg').classList.contains('open')) return $$('[data-f]', $('#sheet')).filter(visible);
  if ($('#vol').classList.contains('open')) return $$('[data-f]', $('#vol')).concat($('#spk'));
  const out = $$('[data-f]', $('#s-' + screen)).filter(visible);
  if (!$('#bar').classList.contains('bare')) out.push(...$$('[data-f]', $('#bar')).filter(visible));
  return out;
}
function setFocus(el, sound) {
  if (!el || el === focused) return;
  if (focused) focused.classList.remove('kf');
  focused = el; el.classList.add('kf');
  if (el.tagName === 'INPUT') el.focus(); else if (document.activeElement && document.activeElement.tagName === 'INPUT') document.activeElement.blur();
  if (document.body.classList.contains('kb')) el.scrollIntoView({ block: 'nearest' });
  if (sound) sfx('cursor');
}
function move(dx, dy) {
  const list = focusables(); if (!list.length) return;
  if (!focused || !list.includes(focused)) return setFocus(list[0], true);
  const a = focused.getBoundingClientRect(), ax = a.left + a.width / 2, ay = a.top + a.height / 2;
  let best = null, bs = 1e9;
  for (const el of list) {
    if (el === focused) continue;
    const b = el.getBoundingClientRect(), bx = b.left + b.width / 2, by = b.top + b.height / 2;
    const p = dx ? (bx - ax) * dx : (by - ay) * dy;
    const edge = dx ? (dx > 0 ? b.left - a.right : a.left - b.right) : (dy > 0 ? b.top - a.bottom : a.top - b.bottom);
    if (p <= 4 && edge < -2) continue;
    const s = Math.max(0, edge) + Math.abs(dx ? by - ay : bx - ax) * 2.2 + (p <= 4 ? 1e5 : 0);
    if (s < bs) { bs = s; best = el; }
  }
  if (best) setFocus(best, true);
}
document.addEventListener('mouseover', e => { const el = e.target.closest('[data-f]'); if (el && focusables().includes(el)) setFocus(el, true); });
document.addEventListener('mousemove', () => document.body.classList.remove('kb'));

/* ---------------- actions ---------------- */
function activate(el) {
  if (!el) return;
  if (el.tagName === 'INPUT') { el.focus(); return; }
  if (el.dataset.go) { if (el.dataset.go !== screen) { sfx('decide'); go(el.dataset.go); } return; }
  const act = el.dataset.act; if (!act) return;
  const i = act.indexOf(':'), verb = i < 0 ? act : act.slice(0, i), arg = i < 0 ? '' : act.slice(i + 1);
  if (el.classList.contains('off')) { sfx('error'); return; }
  if (!['vol', 'mute', 'win'].includes(verb)) sfx(el.dataset.big !== undefined ? 'decide_l' : el.hasAttribute('data-back-sound') ? 'cancel' : 'decide');
  const A = ACTIONS[verb];
  if (A) A(arg, el); else cmd(verb, arg ? { arg } : undefined);   // anything the page does not know itself is the launcher's
}
document.addEventListener('click', e => {
  const j = e.target.closest('[data-j]'); if (j) return jump(j.dataset.j);
  if (e.target.closest('#jumpBtn')) return toggleJump();
  if (e.target === $('#sheetBg')) return closeSheet();
  if (!e.target.closest('#vol') && !e.target.closest('#spk') && $('#vol').classList.contains('open')) closeVol();
  const el = e.target.closest('[data-act],[data-go]'); if (el) activate(el);
});
function back() {
  if ($('#jump').classList.contains('open')) return toggleJump();
  if ($('#drawer').classList.contains('open')) return ACTIONS.details();
  if ($('#sheetBg').classList.contains('open')) return closeSheet();
  if ($('#vol').classList.contains('open')) return closeVol();
  const b = $('#s-' + screen).dataset.back; if (!b) return;
  sfx('cancel');
  if (ACTIONS[b]) return ACTIONS[b]();
  if (b === 'welcome') cmd('forget');   // back from the reveal: choose another file
  go(b);
}
function tab(d) {
  if (!MAIN.includes(screen) && screen !== 'bench') return;
  const i = MAIN.indexOf(screen === 'bench' ? 'settings' : screen);
  sfx(d < 0 ? 'tab_l' : 'tab_r'); go(MAIN[(i + d + 4) % 4]);
}
let toastT = 0;
function toast(html) {
  const h = $('#toastHost'); h.innerHTML = `<div class="toast">${html}</div>`;
  clearTimeout(toastT); toastT = setTimeout(() => h.innerHTML = '', 3600);
}

const ACTIONS = {
  go: s => go(s),
  win: a => send({ t: 'win', a }),
  pick: kind => send({ t: 'pick', kind }),
  vol: () => $('#vol').classList.contains('open') ? closeVol() : openVol(),
  mute: () => toggleMute(),
  closesheet: () => closeSheet(),
  details: () => { const d = $('#drawer'); d.classList.toggle('open'); if (d.classList.contains('open')) { const p = $('#drawerLog'); p.scrollTop = p.scrollHeight; } },
  stop: () => openSheet(`<h3>${esc(S.work && S.work.stopTitle || 'Stop?')}</h3><p>${esc(S.work && S.work.stopText || '')}</p>` +
    '<div class="acts"><span class="qlink" data-f data-act="closesheet" id="sh1">Keep going</span><div class="play sm" style="width:200px" data-f data-act="dostop">Stop</div></div>', 'sh1'),
  dostop: () => { closeSheet(true); cmd('stop'); },
  workback: () => { cmd('workback'); },
  modtab: t => { S.mods.tab = t; drawModTabs(); cmd('mods.list', { tab: t, q: $('#modQ').value }); },
  mod: (id) => openModSheet(id),
  sc: (i) => openSaveSheet(+i),
  file: (i) => { S.cur = +i; drawHome(); },
  bringin: () => openSaveFlow('import'),
  copyout: () => openSaveFlow('export'),
  opt: (a, el) => { $$('.opt', el.parentElement).forEach(o => o.classList.toggle('sel', o === el)); },
  row: (id) => settingRow(id),
  choose: (id) => cmd('choose', { id }),
};

/* ---------------- sheets and the sound panel ---------------- */
let sheetReturn = null;
function openSheet(html, first) {
  sheetReturn = focused; $('#sheet').innerHTML = html; $('#sheetBg').classList.add('open'); sfx('open');
  focused = null; setFocus($('#' + first) || $('[data-f]', $('#sheet')));
}
function closeSheet(quiet) {
  if (!$('#sheetBg').classList.contains('open')) return;
  $('#sheetBg').classList.remove('open'); if (!quiet) sfx('close');
  if (sheetReturn && document.contains(sheetReturn) && visible(sheetReturn)) { focused = null; setFocus(sheetReturn); }
}
function openVol() { $('#vol').classList.add('open'); sfx('open'); setFocus($('[data-vol="music"]')); }
function closeVol(quiet) { if (!$('#vol').classList.contains('open')) return; $('#vol').classList.remove('open'); if (!quiet) sfx('close'); setFocus($('#spk')); }
/* A list to choose one from (languages, folders, emulators): the launcher sends it, the choice goes back with its id. */
function openChoice(m) {
  const opts = (m.options || []).map((o, i) => `<div class="opt${o.sel ? ' sel' : ''}" data-f data-act="opt" data-id="${esc(o.id)}" id="op${i}"><div><b>${esc(o.name)}</b>${o.sub ? `<span> · ${esc(o.sub)}</span>` : ''}</div><span></span></div>`).join('');
  openSheet(`<h3>${esc(m.title)}</h3>${m.text ? `<p>${m.text}</p>` : ''}<div class="opts${m.options && m.options.length > 6 ? ' many' : ''}">${opts}</div>` +
    `<div class="acts"><span class="qlink" data-f data-act="closesheet" data-back-sound>Cancel</span><div class="play sm" style="width:220px" data-f id="choiceOk">${esc(m.ok || 'Choose')}</div></div>`,
    'op' + Math.max(0, (m.options || []).findIndex(o => o.sel)));
  $('#choiceOk').onclick = () => { const s = $('.opt.sel', $('#sheet')); closeSheet(true); if (s) cmd(m.reply, { id: s.dataset.id, ctx: m.ctx }); };
  $('#choiceOk').dataset.act = 'noop';
}
ACTIONS.noop = () => {};
/* A question with buttons (the DLSS licence, uninstall, a replace warning): each button's id goes back. */
function openAsk(m) {
  const btns = (m.buttons || []).map((b, i) => b.main ? `<div class="play sm" style="width:${b.w || 220}px" data-f data-act="answer:${esc(b.id)}" id="ab${i}">${esc(b.label)}</div>`
    : `<span class="qlink" data-f data-act="answer:${esc(b.id)}" id="ab${i}">${esc(b.label)}</span>`).join('');
  openSheet(`<h3>${esc(m.title)}</h3><div class="asktext">${m.html || ''}</div><div class="acts">${btns}</div>`, 'ab0');
  S.ask = m;
}
ACTIONS.answer = (id) => { closeSheet(true); cmd(S.ask.reply, { id, ctx: S.ask.ctx }); };

/* ---------------- first run ---------------- */
const portal = $('#portal');
['dragenter', 'dragover'].forEach(t => document.addEventListener(t, e => { e.preventDefault(); if (screen === 'welcome') portal.classList.add('hot'); }));
document.addEventListener('dragleave', e => { if (!e.relatedTarget) portal.classList.remove('hot'); });
document.addEventListener('drop', e => {
  e.preventDefault(); portal.classList.remove('hot'); unlocked = true;
  const dt = e.dataTransfer, files = [...dt.files];
  if (!files.length && !dt.getData('text/uri-list')) return;
  const uris = (dt.getData('text/uri-list') || '').split(/\r?\n/).filter(u => u.startsWith('file://'));
  const msg = { t: 'drop', screen, names: files.map(f => f.name), files: uris };
  if (HOST === 'win' && window.chrome.webview.postMessageWithAdditionalObjects) window.chrome.webview.postMessageWithAdditionalObjects(JSON.stringify(msg), dt.files);
  else send(msg);
});
function onCheck(m) {
  portal.classList.toggle('checking', m.st === 'checking'); portal.classList.toggle('bad', m.st === 'bad');
  $('#portalH').textContent = m.st === 'checking' ? 'Checking your game' : m.st === 'bad' ? (m.title || "That isn't your game") : 'Drop your game here';
  $('#portalP').textContent = m.st === 'idle' ? 'Your .nup file, or the folder with romfs and exefs in it' : (m.msg || m.name || '');
  if (m.st === 'bad') sfx('error');
}
function drawReveal() {
  const r = S.reveal; if (!r) return;
  $('#found').innerHTML = r.found || '';
  $('#choices').innerHTML = (r.rows || []).map(x => `<div class="${x.bad ? 'bad' : ''}"><div><label>${esc(x.label)}</label><b>${esc(x.value)}</b>${x.small ? `<small>${esc(x.small)}</small>` : ''}</div>` +
    (x.act ? `<span class="ch" data-f data-act="choose:${esc(x.act)}">${esc(x.actLabel || 'Change')}</span>` : '') + '</div>').join('');
  $('#choices').style.gridTemplateColumns = ({ 1: '1fr', 2: '1fr 1.6fr', 3: '0.9fr 2fr 1fr' })[(r.rows || []).length] || 'repeat(4, 1fr)';
  $('#under').innerHTML = (r.under ? esc(r.under) + ' · ' : '') + '<span class="lnk" style="color:var(--gold)" data-f data-act="go:welcome" data-back-sound>Choose another file</span>';
  $('#buildBtn').classList.toggle('off', !r.canBuild);
  $('#buildBtn').textContent = r.button || 'Build the game';
}

/* ---------------- the working screen: build, update, tools ---------------- */
function drawWork(w) {
  S.work = w = Object.assign(S.work || {}, w);
  $('#workEb').textContent = w.eb || ''; $('#workTitle').textContent = w.title || ''; $('#workLead').textContent = w.lead || '';
  $('#workPath').innerHTML = (w.path || []).map(p => `<span class="${p.st || ''}"><i></i>${esc(p.name)}</span>`).join('');
  $('#workPath').style.display = w.path && w.path.length ? '' : 'none';
  const f = Math.max(0, Math.min(1, w.frac || 0));
  $('#workBar').style.width = f * 100 + '%'; $('#workKnob').style.left = f * 100 + '%';
  const prev = S.workStepsDone || 0, done = (w.steps || []).filter(s => s.st === 'd').length;
  $('#workSteps').innerHTML = (w.steps || []).map(s => `<div class="${s.st || ''}"><em></em><b>${esc(s.name)}</b><span>${esc(s.right || '')}</span></div>`).join('');
  if (done > prev && screen === 'work') sfx('success');
  S.workStepsDone = done;
  const err = !!w.error;
  $('#s-work').classList.toggle('failed', err);
  $('#workErr').style.display = err ? '' : 'none';
  $('#workBody').style.display = err ? 'none' : '';
  $('#workRetry').textContent = w.retry || 'Try again';
  $('#workNote').textContent = w.note || '';
  $('#workStopWrap').style.display = w.canStop && !err ? '' : 'none';
  if (err && !S.workErrShown) { sfx('error'); S.workErrShown = true; setFocus($('#workRetry')); }
  if (!err) S.workErrShown = false;
}
function addLog(lines) {
  const p = $('#drawerLog'); const at = p.scrollTop + p.clientHeight >= p.scrollHeight - 4;
  p.textContent += lines.join('\n') + '\n';
  if (p.textContent.length > 200000) p.textContent = p.textContent.slice(-150000);
  if (at) p.scrollTop = p.scrollHeight;
}

/* ---------------- home ---------------- */
function picUrl(f) { return f && f.pic ? `url("${f.pic}")` : 'none'; }
function drawHome() {
  const h = S.home; if (!h) return;
  const files = S.files || [];
  if (S.cur == null || S.cur >= files.length) S.cur = Math.max(0, files.findIndex(f => f.last));
  const f = files[S.cur];
  const showFile = f && h.mode === 'ready';
  $('#hEb').textContent = showFile ? 'Continue · ' + f.label : h.eb;
  $('#hH1').textContent = showFile ? (f.place || h.title) : h.title;
  $('#hMeta').textContent = showFile ? f.meta : h.meta;
  $('#playBtn').textContent = h.button || 'Play';
  $('#playBtn').dataset.act = h.act || 'play';
  $('#playBtn').classList.toggle('off', !!h.off);
  $('#hLinks').innerHTML = (h.links || []).map(l => l.act ? `<span class="lnk" data-f data-act="${esc(l.act)}">${l.html}</span>` : `<span>${l.html}</span>`).join('');
  const url = picUrl(f);
  ['#homeImg', '#homePic', '#savesImg', '#modsImg', '#setImg', '#playImg', '#benchImg'].forEach(s => $(s).style.backgroundImage = url);
  $('#s-home').classList.toggle('nopic', !f);
  $('#hCap').textContent = f ? `${f.label} · ${f.last ? 'your last save · ' : ''}picture from the game` : '';
  $('#strip').innerHTML = files.map((x, i) => `<div class="f${i === S.cur ? ' on' : ''}" data-f data-act="file:${i}" id="f${i}"><img src="${esc(x.pic)}" alt=""><small>${esc(x.label)}</small></div>`).join('');
  $('.files').style.display = files.length ? '' : 'none';
  const n = h.news;
  $('#whatsnew').innerHTML = n ? `<h4>${esc(n.title)}${n.more && h.mode !== 'update' ? ' · <span class="lnk" data-f data-act="open:changes">All changes</span>' : ''}</h4>` + n.items.map(i => `<p><b>${esc(i.b)}</b> ${esc(i.p)}</p>`).join('') : '';
}
function drawPill() {
  const p = S.pill || {};
  $('#pill').innerHTML = p.text ? `<i class="dot ${esc(p.dot || '')}"></i><span>${esc(p.text)}</span>` : '';
  $('#pill').style.display = p.text ? '' : 'none';
  if (p.act) { $('#pill').dataset.act = p.act; $('#pill').setAttribute('data-f', ''); } else { delete $('#pill').dataset.act; $('#pill').removeAttribute('data-f'); }
}

/* ---------------- saves ---------------- */
function drawSaves() {
  const s = S.saves || { items: [] };
  $('#savesEb').textContent = s.eb || '';
  $('#foundSaves').textContent = s.found || 'From an emulator on this PC, or a folder.';
  $('#sgrid').innerHTML = s.items.map((f, i) => f.empty
    ? `<div class="sc empty" data-f data-act="sc:${i}" id="sc${i}"><div class="p"><span>Empty</span></div><div class="t"><b>${esc(f.label)}</b></div></div>`
    : `<div class="sc${f.last ? ' on' : ''}" data-f data-act="sc:${i}" id="sc${i}">${f.place ? `<div class="n">${esc(f.label)}</div>` : ''}<div class="p" style="background-image:url('${esc(f.pic)}')"></div>` +
      `<div class="t"><b>${esc(f.place || f.label)}</b><span>${esc(f.when)}</span></div><div class="m">${esc(f.time || '')}</div></div>`).join('');
}
function openSaveSheet(i) {
  const f = (S.saves || {}).items[i]; if (!f || f.empty) return;
  openSheet(`<h3>${esc(f.label)}${f.place ? ' · ' + esc(f.place) : ''}</h3><p>${esc(f.meta || '')}</p>` +
    `<div class="acts"><span class="qlink" data-f data-act="closesheet">Close</span><span class="qlink" data-f data-act="saves.open">Open the folder</span>` +
    `<div class="play sm" style="width:240px" data-f data-act="playslot:${f.slot}" id="sh1">Play</div></div>`, 'sh1');
}
ACTIONS.playslot = (slot) => { closeSheet(true); cmd('play', { slot: +slot }); };
function openSaveFlow(kind) { cmd(kind === 'import' ? 'saves.import' : 'saves.export'); }
$$('[data-act="open:backups"]').forEach(e => e.dataset.act = 'saves.backups');   // (the backups folder is the Saves page's own command)

/* ---------------- mods ---------------- */
function drawModTabs() { $$('#modTabs span').forEach(s => s.classList.toggle('on', s.dataset.act === 'modtab:' + S.mods.tab)); $('#modSearch').style.display = S.mods.tab === 'browse' ? '' : 'none'; }
function drawMods() {
  drawModTabs();
  const m = S.mods;
  const L = { get: '<em>Install</em>', busy: '<em>Working…</em>', on: '<em class="inst">&#9670; On</em>', off: '<em class="offm">Off</em>', update: '<em>Update</em>', kit: '<em>Install and start</em>', start: '<em class="inst">Start</em>' };
  $('#mods').innerHTML = m.items.map((x, i) => `<div class="mod" data-f data-act="mod:${i}" id="mod${i}">${x.img ? `<div class="mi" style="background-image:url('${esc(x.img)}')"></div>` : '<div class="mi none"></div>'}` +
    `<div><b>${esc(x.name)}</b><span>${esc(x.sub || '')}</span></div>${L[x.state] || ''}</div>`).join('') +
    (m.more ? `<div class="mod more" data-f data-act="mods.more" id="modMore"><div></div><div><b>More</b><span>Load the next page</span></div></div>` : '');
  $('#modNote').innerHTML = m.note || '';
}
function openModSheet(i) {
  const x = S.mods.items[i]; if (!x) return;
  const main = { get: 'Install', update: 'Update', kit: 'Install and start', start: 'Start', on: 'Turn off', off: 'Turn on' }[x.state];
  openSheet(`<h3>${esc(x.name)}</h3><p>${esc(x.sub || '')}</p>${x.desc ? `<div class="asktext modesc">${esc(x.desc)}</div>` : ''}` +
    `<div class="acts"><span class="qlink" data-f data-act="closesheet">Close</span>` + (x.page ? `<span class="qlink" data-f data-act="modgo:page:${i}">Its page</span>` : '') +
    (x.removable ? `<span class="qlink" data-f data-act="modgo:remove:${i}">Remove</span>` : '') +
    (main ? `<div class="play sm" style="width:240px" data-f data-act="modgo:main:${i}" id="sh1">${main}</div>` : '') + '</div>', 'sh1');
}
ACTIONS.modgo = (a) => { const [what, i] = a.split(':'); const x = S.mods.items[+i]; closeSheet(true); cmd('mods.' + what, { id: x.id, tab: S.mods.tab }); };
let qT = 0;
$('#modQ').addEventListener('input', () => { clearTimeout(qT); qT = setTimeout(() => cmd('mods.list', { tab: 'browse', q: $('#modQ').value }), 350); });
$('#modQ').addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === 'ArrowDown' || e.key === 'Escape') { e.preventDefault(); $('#modQ').blur(); move(0, 1); } e.stopPropagation(); });

/* ---------------- settings: a list the launcher sends, and the game's own Options menu in tabs ---------------- */
S.settab = 'launcher';
function launcherRows() {
  return (S.settings || []).map(r => r.h ? `<h4>${esc(r.h)}</h4>` : r.kind === 'slider' ?
    `<div class="row-set" style="cursor:default"><b>${esc(r.label)}</b><div class="rng"><div class="slider" data-f data-vol="${esc(r.id)}" id="row-${esc(r.id)}"><i></i><u></u></div><span data-volv="${esc(r.id)}"></span></div><em></em></div>` :
    `<div class="row-set${r.danger ? ' danger' : ''}${r.off ? ' off' : ''}" data-f data-act="row:${esc(r.id)}" id="row-${esc(r.id)}"><b>${esc(r.label)}</b><span>${esc(r.value || '')}</span><em>${esc(r.actLabel || 'Change')}</em></div>`).join('');
}
function gameRows(tab) {
  const g = S.gameopts; if (!g || !g.data || !g.data[tab]) return '';
  const lock = g.locked ? ' off' : '';
  return `<p class="gnote">${esc(g.note || '')}</p>` + g.data[tab].map(sec => `<h4>${esc(sec.h)}</h4>` + sec.rows.map(r => r.kind === 'slider'
    ? `<div class="row-set gs${lock}" style="cursor:default" title="${esc(r.help || '')}"><b>${esc(r.label)}</b><div class="rng"><div class="slider" data-f data-gkey="${esc(r.key)}" data-lo="${r.lo}" data-hi="${r.hi}" data-step="${r.step}" data-v="${r.v}" id="g-${esc(r.key)}"><i style="width:${pct(r)}%"></i><u style="left:${pct(r)}%"></u></div><span class="gval">${esc(r.value)}</span></div><em></em></div>`
    : `<div class="row-set${lock}" data-f data-act="gopt:${esc(r.key)}" id="g-${esc(r.key)}" title="${esc(r.help || '')}"><b>${esc(r.label)}</b><span>${esc(r.value)}</span><em>Change</em></div>`).join('')).join('');
}
function pct(r) { return r.hi > r.lo ? Math.round((r.v - r.lo) / (r.hi - r.lo) * 1000) / 10 : 0; }
function drawSettings() {
  const tabs = [{ id: 'launcher', name: 'Launcher' }].concat(S.gameopts && S.gameopts.tabs || []);
  if (!tabs.some(t => t.id === S.settab)) S.settab = 'launcher';
  const keep = focused && focused.id;
  $('#setTabs').innerHTML = tabs.map(t => `<span data-f data-act="settab:${t.id}" id="st-${t.id}" class="${t.id === S.settab ? 'on' : ''}">${esc(t.name)}</span>`).join('');
  $('#set').innerHTML = S.settab === 'launcher' ? launcherRows() : gameRows(S.settab);
  $('#s-settings').dataset.first = 'st-' + S.settab;
  setVol('music', S.sound.music, true); setVol('sfx', S.sound.sfx, true);
  $('#benchCard').style.display = S.bench && S.bench.available && (S.settab === 'launcher' || S.settab === 'graphics') ? '' : 'none';
  $('#benchGo').classList.toggle('off', !(S.bench && S.bench.ready));
  $('#benchLast').textContent = S.bench && S.bench.last || '';
  if (keep && $('#' + keep) && screen === 'settings') { focused = null; setFocus($('#' + keep)); }
}
ACTIONS.settab = (t) => { S.settab = t; drawSettings(); $('#set').scrollTop = 0; setFocus($('#st-' + t)); };
ACTIONS.gopt = (key) => cmd('gopt.choose', { key });
/* the game's sliders: drag, or left / right while focused; the value goes to the launcher when let go */
function gStep(el, v) {
  const lo = +el.dataset.lo, hi = +el.dataset.hi, st = +el.dataset.step || 1;
  v = Math.max(lo, Math.min(hi, lo + Math.round((v - lo) / st) * st));
  el.dataset.v = v; const p = hi > lo ? (v - lo) / (hi - lo) * 100 : 0;
  el.querySelector('i').style.width = p + '%'; el.querySelector('u').style.left = p + '%';
  const lab = el.parentElement.querySelector('.gval'); if (lab) lab.textContent = String(v);
  return v;
}
let gSendT = 0;
function gSend(el) { clearTimeout(gSendT); gSendT = setTimeout(() => cmd('gopt.value', { key: el.dataset.gkey, value: +el.dataset.v }), 350); }
document.addEventListener('pointerdown', e => {
  const s = e.target.closest('.slider[data-gkey]'); if (!s || s.closest('.off')) return;
  e.preventDefault(); setFocus(s, true);
  const mv = ev => { const b = s.getBoundingClientRect(); gStep(s, +s.dataset.lo + (ev.clientX - b.left) / b.width * (+s.dataset.hi - +s.dataset.lo)); };
  mv(e);
  const up = () => { removeEventListener('pointermove', mv); removeEventListener('pointerup', up); sfx('cursor'); gSend(s); };
  addEventListener('pointermove', mv); addEventListener('pointerup', up);
});
function gNudge(d) { if (!focused || !focused.dataset.gkey || focused.closest('.off')) return false; gStep(focused, +focused.dataset.v + d * (+focused.dataset.step || 1)); sfx('cursor'); gSend(focused); return true; }
function settingRow(id) { cmd('setting', { id }); }
// (the list is asked for each time the page opens, so it shows what is true now: Steam, shortcuts, the game folder)
new MutationObserver(() => { if ($('#s-settings').classList.contains('on')) cmd('settings.list'); }).observe($('#s-settings'), { attributes: true, attributeFilter: ['class'] });

/* ---------------- benchmark ---------------- */
/* a scene's frame times (ms) as a small line: higher = slower, the 16.7 ms line (60 fps) dashed */
function sparkline(pts, cls) {
  if (!pts || pts.length < 2) return '';
  const max = Math.max(20, ...pts) * 1.1, w = 300, h = 46;
  const d = pts.map((v, i) => `${(i / (pts.length - 1) * w).toFixed(1)},${(h - v / max * h).toFixed(1)}`).join(' ');
  const y60 = (h - 16.7 / max * h).toFixed(1);
  return `<svg class="spark ${cls || ''}" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none"><line x1="0" x2="${w}" y1="${y60}" y2="${y60}" class="l60"/><polyline points="${d}"/></svg>`;
}
function onBench(b) {
  if (b.phase === 'run') {
    if (screen !== 'brun') go('brun');
    if (b.place) { $('#bPlace').style.backgroundImage = b.place.img ? `url("${b.place.img}")` : 'none'; $('#bName').textContent = b.place.name; }
    $('#bPass').textContent = b.pass || ''; $('#bLine').textContent = b.line || '';
    $('#bProg').style.width = Math.min(100, (b.frac || 0) * 100) + '%';
  } else if (b.phase === 'done') {
    $('#benchEb').textContent = b.eb || '';
    $('#places').innerHTML = (b.places || []).map(p => `<div class="place noimg${p.fps2 != null ? ' two' : ''}"><div class="pg">${sparkline(p.graph)}${p.graph2 ? sparkline(p.graph2, 'b') : ''}</div>` +
      (p.fps2 != null
        ? `<div class="two"><b>${esc(p.name)}</b><span><i>Your settings</i><strong>${p.fps}</strong> fps &#183; low ${p.low}</span><span class="vs"><i>${esc(b.second || '')}</i><strong>${p.fps2}</strong> fps &#183; low ${p.low2}</span></div></div>`
        : `<div><b>${esc(p.name)}</b><span><strong>${p.fps}</strong> fps &#183; low ${p.low}</span></div></div>`)).join('');
    $('#verdict').innerHTML = b.verdict || '';
    $('#recs').innerHTML = (b.recs || []).map(r => `<div class="rec${r.same ? ' same' : ''}"><b>${esc(r.name)}</b><s>${esc(r.from)}</s><em>&#8594;</em><strong>${esc(r.to)}</strong></div>`).join('');
    const c = $('#compareBtn'); c.textContent = b.compare || ''; c.style.display = b.compare ? '' : 'none';
    $('#applyBtn').classList.toggle('off', !b.apply);
    $('#applyBtn').textContent = b.apply ? 'Apply' : 'Nothing to change';
    sfx('success'); go('bench');
  } else if (b.phase === 'stopped') go('settings');
}

/* ---------------- messages from the launcher ---------------- */
onHost(m => {
  switch (m.t) {
    case 'hello': document.documentElement.classList.toggle('nochrome', !m.chrome); break;
    case 'max': document.documentElement.classList.toggle('maxed', m.on); break;
    case 'state': {
      const had = { music: S.media.music, sfx: S.media.sfx };
      for (const k in m) if (k !== 't') S[k] = m[k];
      if (m.sound) { setVol('music', S.sound.music, true); setVol('sfx', S.sound.sfx, true); }
      if (m.media) {
        document.documentElement.classList.toggle('noart', !S.media.art);
        if (S.media.art) $$('.gamelogo').forEach(i => { if (!i.complete || !i.naturalWidth) i.src = 'img/game/Logo_USEU_00.png?' + (S.media.v || ''); });
        loadSfx();
        if (!had.music && S.media.music && screen) { const want = MUSIC[screen]; curTrack = null; music(want === 'itemget' ? 'fairy' : want); }
      }
      drawPill(); drawHome(); drawReveal(); drawSaves(); drawSettings(); if (m.mods) drawMods(); nowPlaying();
      if (m.done) { $('#doneEb').textContent = S.done.eb || ''; $('#doneLead').textContent = S.done.lead || ''; }
      break;
    }
    case 'go': go(m.s); break;
    case 'check': onCheck(m); break;
    case 'work': drawWork(m); break;
    case 'log': addLog(m.lines || []); break;
    case 'toast': toast(m.html); if (m.sfx) sfx(m.sfx); break;
    case 'sfx': sfx(m.id); break;
    case 'choice': openChoice(m); break;
    case 'ask': openAsk(m); break;
    case 'bench': onBench(m); break;
    case 'copy': navigator.clipboard && navigator.clipboard.writeText(m.text).then(() => toast('Copied'), () => toast('Could not copy')); break;
  }
});

/* ---------------- keyboard ---------------- */
document.addEventListener('keydown', e => {
  if (e.key === 'F2' && !HOST) { e.preventDefault(); return toggleJump(); }
  if (document.activeElement && document.activeElement.tagName === 'INPUT' && !['Escape', 'Enter', 'ArrowDown', 'ArrowUp', 'Tab'].includes(e.key)) return;
  const k = e.key; let used = true;
  if (focused && focused.dataset.vol && (k === 'ArrowLeft' || k === 'ArrowRight')) { setVol(focused.dataset.vol, S.sound[focused.dataset.vol] + (k === 'ArrowLeft' ? -.05 : .05)); sfx('cursor'); }
  else if (focused && focused.dataset.gkey && (k === 'ArrowLeft' || k === 'ArrowRight')) gNudge(k === 'ArrowLeft' ? -1 : 1);
  else if (k === 'ArrowLeft') move(-1, 0); else if (k === 'ArrowRight') move(1, 0);
  else if (k === 'ArrowUp') move(0, -1); else if (k === 'ArrowDown') move(0, 1);
  else if (k === 'Enter' || k === ' ') activate(focused);
  else if (k === 'Escape' || k === 'Backspace') back();
  else if (k === 'PageUp' || (k === 'q' && !e.ctrlKey)) tab(-1); else if (k === 'PageDown' || (k === 'e' && !e.ctrlKey)) tab(1);
  else if (k === 'm' || k === 'M') toggleMute();
  else if (k === 'Tab') { const l = focusables(), i = l.indexOf(focused); setFocus(l[(i + (e.shiftKey ? -1 : 1) + l.length) % l.length], true); }
  else used = false;
  if (used) { e.preventDefault(); document.body.classList.add('kb'); }
});

/* ---------------- controller (Xbox layout; the browser maps others to it): A, B, LB, RB, D-pad, left stick, Y mute ---------------- */
const held = {}; let stickAt = 0, stickDir = '';
function pad(t) {
  if (!document.hasFocus()) return;
  const gp = [...(navigator.getGamepads ? navigator.getGamepads() : [])].find(g => g && g.connected); if (!gp) return;
  const B = i => gp.buttons[i] && gp.buttons[i].pressed;
  const edge = (i, fn) => { const p = B(i); if (p && !held[i]) { document.body.classList.add('kb'); unlocked = true; fn(); } held[i] = p; };
  const slide = d => { if (focused && focused.dataset.vol) { setVol(focused.dataset.vol, S.sound[focused.dataset.vol] + d * .05); sfx('cursor'); return true; } return gNudge(d); };
  edge(0, () => activate(focused)); edge(1, back); edge(4, () => tab(-1)); edge(5, () => tab(1));
  edge(3, toggleMute); edge(9, () => { if (MAIN.includes(screen)) go('settings'); });
  edge(12, () => move(0, -1)); edge(13, () => move(0, 1)); edge(14, () => slide(-1) || move(-1, 0)); edge(15, () => slide(1) || move(1, 0));
  const x = gp.axes[0] || 0, y = gp.axes[1] || 0, dir = Math.abs(x) > .55 ? (x > 0 ? 'r' : 'l') : Math.abs(y) > .55 ? (y > 0 ? 'd' : 'u') : '';
  if (dir && (dir !== stickDir || t - stickAt > 220)) {
    document.body.classList.add('kb'); stickAt = t + (dir !== stickDir ? 160 : 0);
    ({ l: () => slide(-1) || move(-1, 0), r: () => slide(1) || move(1, 0), u: () => move(0, -1), d: () => move(0, 1) })[dir]();
  }
  stickDir = dir;
}
function frame(t) { audioTick(t); pad(t); requestAnimationFrame(frame); }
requestAnimationFrame(frame);

/* ---------------- preview only: jump to a screen (the stand-in plays along) ---------------- */
function toggleJump() { $('#jump').classList.toggle('open'); }
function jump(j) { $('#jump').classList.remove('open'); unlocked = true; if (window.DEMO) window.DEMO.jump(j, fromHost); }
if (!HOST && window.DEMO) $('#jump').innerHTML = window.DEMO.jumps.map(([k, n]) => `<a data-j="${k}">${n}</a>`).join('');

setVol('music', S.sound.music, true); setVol('sfx', S.sound.sfx, true); nowPlaying();
send({ t: 'ready' });
