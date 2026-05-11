/* =====================================================================
 * AetherShell Panel Designer — app.js
 * State management, drag & drop, style editor, C bridge.
 * ===================================================================== */

'use strict';

/* ── State ─────────────────────────────────────────────────────────── */
const state = {
  layout:  null,   // panel.json content (parsed)
  styles:  {},     // { '#pill-id': { bgColor, bgAlpha, radius, ... } }
  plugins: [],     // available plugin descriptors
  selectedPill: null, // pill ID string or null
  panelBg: { color: '#000000', alpha: 0 },
  dirty: false,
};

/* Default style for a newly created/selected pill */
function defaultStyle(pillId) {
  const transparent = ['left-pill','workspaces-pill','sni-pill','keyboard-pill','menu-pill'];
  const isTransparent = transparent.includes(pillId);
  return {
    bgColor:     '#000000',
    bgAlpha:     isTransparent ? 0 : 30,
    radius:      14,
    marginV:     4,
    marginH:     isTransparent ? 0 : 4,
    paddingH:    isTransparent ? 0 : 4,
    borderOn:    false,
    borderColor: '#ffffff',
    borderAlpha: 15,
  };
}

/* ── C bridge ──────────────────────────────────────────────────────── */
function sendToC(obj) {
  if (window.webkit?.messageHandlers?.panelDesigner) {
    window.webkit.messageHandlers.panelDesigner.postMessage(JSON.stringify(obj));
  } else {
    console.log('[Designer→C]', obj);
  }
}

/* Called by C main.c after page load */
window._aether = {
  init(layout, savedState, plugins) {
    state.plugins = plugins || [];
    state.layout  = layout  || defaultLayout();

    if (savedState) {
      state.styles    = savedState.styles    || {};
      state.panelBg   = savedState.panelBg   || state.panelBg;
      state.winStyles = savedState.winStyles || {};
    }

    renderPalette();
    renderZones();
    renderPreview();
    syncPanelBgControls();
  },

  onSaved() {
    state.dirty = false;
    document.getElementById('dirty-badge').classList.add('hidden');
    toast('Saved ✓', 'success');
  },
};

function defaultLayout() {
  return {
    panel: { height: 36, spacing: 0, margin: { top: 0, left: 0, right: 0 } },
    layout: {
      left:   [{ type: 'pill', id: 'menu-pill',       spacing: 0, plugins: ['aether-appmenu','aether-clipboard'] },
               { type: 'pill', id: 'workspaces-pill', spacing: 0, plugins: ['aether-workspaces'] }],
      center: [{ type: 'pill', id: 'center-pill',     spacing: 0, plugins: ['aether-clock'] }],
      right:  [{ type: 'pill', id: 'sys-pill',        spacing: 0, plugins: ['aether-battery','aether-search','aether-notifs','aether-cc'] },
               { type: 'pill', id: 'status-pill',     spacing: 0, plugins: ['aether-wifi','aether-bt','aether-mic','aether-volume'] },
               { type: 'pill', id: 'keyboard-pill',   spacing: 0, plugins: ['aether-keyboard'] },
               { type: 'pill', id: 'sni-pill',        spacing: 0, plugins: ['aether-sni-tray'] }],
    }
  };
}

/* ── Markup dirty ──────────────────────────────────────────────────── */
function markDirty() {
  state.dirty = true;
  document.getElementById('dirty-badge').classList.remove('hidden');
}

/* ── Plugin lookup ─────────────────────────────────────────────────── */
function pluginById(id) {
  return state.plugins.find(p => p.id === id) || { id, name: id, icon: '▪' };
}

/* ── Palette ───────────────────────────────────────────────────────── */
function renderPalette() {
  const list = document.getElementById('plugin-list');
  list.innerHTML = '';
  for (const p of state.plugins) {
    const el = document.createElement('div');
    el.className = 'plugin-chip';
    el.draggable = true;
    el.dataset.pluginId = p.id;
    el.innerHTML = `<span class="plugin-chip-icon">${p.icon}</span>
                    <span class="plugin-chip-name">${p.name}</span>
                    <span style="color:var(--text-muted);font-size:10px">${p.zone}</span>`;
    el.addEventListener('dragstart', onPluginDragStart);
    list.appendChild(el);
  }
}

document.getElementById('plugin-search').addEventListener('input', function() {
  const q = this.value.toLowerCase();
  document.querySelectorAll('.plugin-chip').forEach(el => {
    el.style.display = el.dataset.pluginId.toLowerCase().includes(q) ||
      el.querySelector('.plugin-chip-name').textContent.toLowerCase().includes(q)
      ? '' : 'none';
  });
});

/* ── Zone rendering ────────────────────────────────────────────────── */
function renderZones() {
  ['left','center','right'].forEach(zone => {
    const el = document.getElementById('zone-' + zone);
    el.innerHTML = '';
    const pills = state.layout.layout[zone] || [];
    for (const pill of pills) {
      el.appendChild(buildPillCard(pill, zone));
    }
    setupZoneDrop(el);
  });
}

function buildPillCard(pill, zone) {
  const card = document.createElement('div');
  card.className = 'pill-card' + (state.selectedPill === pill.id ? ' selected' : '');
  card.dataset.pillId = pill.id;
  card.dataset.zone   = zone;
  card.draggable = true;

  card.innerHTML = `
    <div class="pill-card-header">
      <span class="pill-drag-handle" title="Drag pill">⠿</span>
      <span class="pill-id-label">#${pill.id}</span>
      <div class="pill-actions">
        <button class="pill-btn" title="Rename" data-action="rename">✏️</button>
        <button class="pill-btn del" title="Delete pill" data-action="delete">✕</button>
      </div>
    </div>
    <div class="pill-plugins" data-pill-id="${pill.id}"></div>`;

  const pluginArea = card.querySelector('.pill-plugins');
  for (const pid of (pill.plugins || [])) {
    pluginArea.appendChild(buildPluginTag(pid, pill.id));
  }

  /* Pill-level events */
  card.addEventListener('click', e => {
    if (e.target.closest('[data-action]')) return;
    selectPill(pill.id);
  });

  card.querySelector('[data-action="delete"]').addEventListener('click', e => {
    e.stopPropagation();
    removePill(pill.id, zone);
  });

  card.querySelector('[data-action="rename"]').addEventListener('click', e => {
    e.stopPropagation();
    renamePill(pill, card);
  });

  card.addEventListener('dragstart', onPillDragStart);

  /* Drop zone for plugins inside pill */
  setupPluginDropInPill(pluginArea, pill.id);

  return card;
}

function buildPluginTag(pluginId, pillId) {
  const p   = pluginById(pluginId);
  const tag = document.createElement('div');
  tag.className = 'plugin-tag';
  tag.draggable = true;
  tag.dataset.pluginId = pluginId;
  tag.dataset.pillId   = pillId;
  tag.innerHTML = `<span>${p.icon}</span><span>${p.name}</span>
                   <span class="remove-plugin" title="Remove">✕</span>`;
  tag.querySelector('.remove-plugin').addEventListener('click', e => {
    e.stopPropagation();
    removePluginFromPill(pluginId, pillId);
  });
  tag.addEventListener('dragstart', onTagDragStart);
  return tag;
}

/* ── Selection ─────────────────────────────────────────────────────── */
function selectPill(pillId) {
  state.selectedPill = pillId;
  document.querySelectorAll('.pill-card').forEach(c =>
    c.classList.toggle('selected', c.dataset.pillId === pillId));
  document.querySelectorAll('.preview-pill').forEach(c =>
    c.classList.toggle('selected', c.dataset.pillId === pillId));

  /* Ensure style exists */
  if (!state.styles[pillId]) state.styles[pillId] = defaultStyle(pillId);
  showInspector(pillId);
}

/* ── Inspector ─────────────────────────────────────────────────────── */
function showInspector(pillId) {
  document.getElementById('inspector-empty').classList.add('hidden');
  document.getElementById('inspector-panel').classList.remove('hidden');
  document.getElementById('inspector-target-label').textContent = '#' + pillId;

  const s = state.styles[pillId];
  setEl('prop-bg-color',      s.bgColor);
  setRange('prop-bg-alpha',    'prop-bg-alpha-val', s.bgAlpha, '%');
  setRange('prop-radius',      'prop-radius-val',   s.radius,  'px');
  setRange('prop-margin-v',    'prop-margin-v-val', s.marginV, 'px');
  setRange('prop-margin-h',    'prop-margin-h-val', s.marginH, 'px');
  setRange('prop-padding',     'prop-padding-val',  s.paddingH,'px');
  document.getElementById('prop-border-on').checked = s.borderOn;
  setEl('prop-border-color',   s.borderColor);
  setRange('prop-border-alpha','prop-border-alpha-val', s.borderAlpha, '%');
}

function setEl(id, val)  { const e = document.getElementById(id); if (e) e.value = val; }
function setRange(id, valId, num, unit) {
  const e = document.getElementById(id);
  const v = document.getElementById(valId);
  if (e) e.value = num;
  if (v) v.textContent = num + unit;
}

function hideInspector() {
  document.getElementById('inspector-empty').classList.remove('hidden');
  document.getElementById('inspector-panel').classList.add('hidden');
}

/* Wire inspector controls */
function wireInspector() {
  const controls = [
    ['prop-bg-alpha',     'prop-bg-alpha-val',     '%',  'bgAlpha'],
    ['prop-radius',       'prop-radius-val',        'px', 'radius'],
    ['prop-margin-v',     'prop-margin-v-val',      'px', 'marginV'],
    ['prop-margin-h',     'prop-margin-h-val',      'px', 'marginH'],
    ['prop-padding',      'prop-padding-val',       'px', 'paddingH'],
    ['prop-border-alpha', 'prop-border-alpha-val',  '%',  'borderAlpha'],
  ];

  for (const [id, valId, unit, prop] of controls) {
    document.getElementById(id).addEventListener('input', function() {
      document.getElementById(valId).textContent = this.value + unit;
      if (state.selectedPill) {
        state.styles[state.selectedPill][prop] = Number(this.value);
        onStyleChange();
      }
    });
  }

  document.getElementById('prop-bg-color').addEventListener('input', function() {
    if (state.selectedPill) {
      state.styles[state.selectedPill].bgColor = this.value;
      onStyleChange();
    }
  });

  document.getElementById('prop-border-on').addEventListener('change', function() {
    if (state.selectedPill) {
      state.styles[state.selectedPill].borderOn = this.checked;
      onStyleChange();
    }
  });

  document.getElementById('prop-border-color').addEventListener('input', function() {
    if (state.selectedPill) {
      state.styles[state.selectedPill].borderColor = this.value;
      onStyleChange();
    }
  });

  /* Panel background */
  document.getElementById('prop-panel-color').addEventListener('input', function() {
    state.panelBg.color = this.value;
    onStyleChange();
  });
  document.getElementById('prop-panel-alpha').addEventListener('input', function() {
    document.getElementById('prop-panel-alpha-val').textContent = this.value + '%';
    state.panelBg.alpha = Number(this.value);
    onStyleChange();
  });

  document.getElementById('btn-reset-pill').addEventListener('click', () => {
    if (!state.selectedPill) return;
    state.styles[state.selectedPill] = defaultStyle(state.selectedPill);
    showInspector(state.selectedPill);
    onStyleChange();
  });
}

function syncPanelBgControls() {
  document.getElementById('prop-panel-color').value = state.panelBg.color;
  document.getElementById('prop-panel-alpha').value = state.panelBg.alpha;
  document.getElementById('prop-panel-alpha-val').textContent = state.panelBg.alpha + '%';
}

function onStyleChange() {
  markDirty();
  renderPreview();
  const css = generateCSS();
  sendToC({ action: 'apply_css', css });
}

/* ── CSS generation ────────────────────────────────────────────────── */
function hexToRgb(hex) {
  const r = parseInt(hex.slice(1,3),16);
  const g = parseInt(hex.slice(3,5),16);
  const b = parseInt(hex.slice(5,7),16);
  return [r,g,b];
}

function rgba(hex, alpha) {
  const [r,g,b] = hexToRgb(hex);
  return `rgba(${r}, ${g}, ${b}, ${(alpha/100).toFixed(2)})`;
}

function generateCSS() {
  const lines = ['/* AetherShell Panel Designer — Auto-generated */\n'];

  /* Panel background */
  const pb = state.panelBg;
  lines.push(`#panel-bar {\n    background-color: ${rgba(pb.color, pb.alpha)};\n}\n`);

  /* Per-pill */
  for (const [pillId, s] of Object.entries(state.styles)) {
    if (!s) continue;
    const border = s.borderOn
      ? `\n    border: 1px solid ${rgba(s.borderColor, s.borderAlpha)};`
      : '\n    border: none;';
    lines.push(
`#${pillId} {
    background-color: ${rgba(s.bgColor, s.bgAlpha)};
    border-radius: ${s.radius}px;
    margin-top: ${s.marginV}px;
    margin-bottom: ${s.marginV}px;
    margin-left: ${s.marginH}px;
    margin-right: ${s.marginH}px;
    padding: 0 ${s.paddingH}px;${border}
}\n`);
  }
  return lines.join('\n');
}

/* ── Layout generation ─────────────────────────────────────────────── */
function generateLayoutJSON() {
  return JSON.stringify(state.layout, null, 2);
}

/* ── Preview ───────────────────────────────────────────────────────── */
function renderPreview() {
  ['left','center','right'].forEach(zone => {
    const el = document.getElementById('preview-' + zone);
    el.innerHTML = '';
    const pills = state.layout.layout[zone] || [];
    for (const pill of pills) {
      el.appendChild(buildPreviewPill(pill));
    }
  });

  /* Panel background */
  const pb = state.panelBg;
  document.getElementById('preview-bar').style.backgroundColor = rgba(pb.color, pb.alpha);
}

function buildPreviewPill(pill) {
  const s = state.styles[pill.id] || defaultStyle(pill.id);
  const div = document.createElement('div');
  div.className = 'preview-pill' + (state.selectedPill === pill.id ? ' selected' : '');
  div.dataset.pillId = pill.id;
  div.title = '#' + pill.id;
  div.style.cssText = `
    background-color: ${rgba(s.bgColor, s.bgAlpha)};
    border-radius: ${s.radius}px;
    margin: ${s.marginV}px ${s.marginH}px;
    padding: 0 ${Math.max(s.paddingH, 4)}px;
    border: ${s.borderOn ? `1px solid ${rgba(s.borderColor, s.borderAlpha)}` : 'none'};
  `;

  for (const pid of (pill.plugins || [])) {
    const p = pluginById(pid);
    const ic = document.createElement('span');
    ic.className = 'preview-pill-icon';
    ic.textContent = p.icon;
    div.appendChild(ic);
  }

  div.addEventListener('click', () => selectPill(pill.id));
  return div;
}

/* ── Drag & Drop — pills between zones ─────────────────────────────── */
let dragData = null;

function onPillDragStart(e) {
  dragData = { type: 'pill', pillId: this.dataset.pillId, fromZone: this.dataset.zone };
  e.dataTransfer.effectAllowed = 'move';
  e.dataTransfer.setData('text/plain', this.dataset.pillId);
  this.classList.add('dragging');
}

function onPluginDragStart(e) {
  dragData = { type: 'from-palette', pluginId: this.dataset.pluginId };
  e.dataTransfer.effectAllowed = 'copy';
  e.dataTransfer.setData('text/plain', this.dataset.pluginId);
}

function onTagDragStart(e) {
  dragData = { type: 'plugin-tag', pluginId: this.dataset.pluginId, fromPill: this.dataset.pillId };
  e.dataTransfer.effectAllowed = 'move';
  e.dataTransfer.setData('text/plain', this.dataset.pluginId);
  e.stopPropagation();
}

function setupZoneDrop(zoneEl) {
  zoneEl.addEventListener('dragover', e => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    if (dragData?.type === 'pill') zoneEl.classList.add('drag-over');
  });
  zoneEl.addEventListener('dragleave', e => {
    if (!zoneEl.contains(e.relatedTarget)) zoneEl.classList.remove('drag-over');
  });
  zoneEl.addEventListener('drop', e => {
    e.preventDefault();
    zoneEl.classList.remove('drag-over');
    if (!dragData) return;
    const toZone = zoneEl.dataset.zone;
    if (dragData.type === 'pill' && dragData.fromZone !== toZone) {
      const snap = { ...dragData };
      dragData = null;
      movePill(snap.pillId, snap.fromZone, toZone);
    } else {
      dragData = null;
    }
  });
}

function setupPluginDropInPill(pluginArea, pillId) {
  pluginArea.addEventListener('dragover', e => {
    e.preventDefault();
    e.stopPropagation();
    e.dataTransfer.dropEffect = dragData?.type === 'from-palette' ? 'copy' : 'move';
    if (dragData?.type === 'from-palette' || dragData?.type === 'plugin-tag')
      pluginArea.classList.add('drag-over');
  });
  pluginArea.addEventListener('dragleave', e => {
    if (!pluginArea.contains(e.relatedTarget)) pluginArea.classList.remove('drag-over');
  });
  pluginArea.addEventListener('drop', e => {
    e.preventDefault();
    e.stopPropagation();
    pluginArea.classList.remove('drag-over');
    if (!dragData) return;
    const snap = { ...dragData };
    dragData = null;
    if (snap.type === 'from-palette') {
      addPluginToPill(snap.pluginId, pillId);
    } else if (snap.type === 'plugin-tag' && snap.fromPill !== pillId) {
      movePluginBetweenPills(snap.pluginId, snap.fromPill, pillId);
    }
  });
}

/* ── Layout mutations ──────────────────────────────────────────────── */
function findPill(pillId) {
  for (const zone of ['left','center','right']) {
    const arr = state.layout.layout[zone];
    const idx = arr.findIndex(p => p.id === pillId);
    if (idx >= 0) return { zone, arr, idx, pill: arr[idx] };
  }
  return null;
}

function movePill(pillId, fromZone, toZone) {
  const fromArr = state.layout.layout[fromZone];
  const idx = fromArr.findIndex(p => p.id === pillId);
  if (idx < 0) return;
  const [pill] = fromArr.splice(idx, 1);
  state.layout.layout[toZone].push(pill);
  markDirty();
  renderZones();
  renderPreview();
}

function removePill(pillId, zone) {
  const arr = state.layout.layout[zone];
  const idx = arr.findIndex(p => p.id === pillId);
  if (idx >= 0) arr.splice(idx, 1);
  if (state.selectedPill === pillId) {
    state.selectedPill = null;
    hideInspector();
  }
  markDirty();
  renderZones();
  renderPreview();
}

function addPill(zone) {
  const id = prompt('Pill ID (e.g. my-pill):');
  if (!id || !id.trim()) return;
  const pillId = id.trim().replace(/[^a-z0-9-]/g, '-');
  state.layout.layout[zone].push({ type: 'pill', id: pillId, spacing: 0, plugins: [] });
  markDirty();
  renderZones();
  renderPreview();
  selectPill(pillId);
}

function renamePill(pill, card) {
  const newId = prompt('New pill ID:', pill.id);
  if (!newId || !newId.trim() || newId === pill.id) return;
  const clean = newId.trim().replace(/[^a-z0-9-]/g, '-');
  if (state.styles[pill.id]) {
    state.styles[clean] = state.styles[pill.id];
    delete state.styles[pill.id];
  }
  if (state.selectedPill === pill.id) state.selectedPill = clean;
  pill.id = clean;
  markDirty();
  renderZones();
  renderPreview();
}

function addPluginToPill(pluginId, pillId) {
  const found = findPill(pillId);
  if (!found) return;
  if (!found.pill.plugins.includes(pluginId))
    found.pill.plugins.push(pluginId);
  markDirty();
  renderZones();
  renderPreview();
}

function removePluginFromPill(pluginId, pillId) {
  const found = findPill(pillId);
  if (!found) return;
  found.pill.plugins = found.pill.plugins.filter(p => p !== pluginId);
  markDirty();
  renderZones();
  renderPreview();
}

function movePluginBetweenPills(pluginId, fromPillId, toPillId) {
  removePluginFromPill(pluginId, fromPillId);
  addPluginToPill(pluginId, toPillId);
}

/* ── Add pill buttons ──────────────────────────────────────────────── */
document.querySelectorAll('.btn-add-pill').forEach(btn => {
  btn.addEventListener('click', () => addPill(btn.dataset.zone));
});

/* ── Toolbar buttons ───────────────────────────────────────────────── */
document.getElementById('btn-save').addEventListener('click', () => {
  const fullCSS = generateCSS() + '\n' + generateWindowCSS();
  sendToC({
    action:  'save',
    layout:  generateLayoutJSON(),
    css:     fullCSS,
    state:   JSON.stringify({ styles: state.styles, panelBg: state.panelBg, winStyles: state.winStyles }),
  });
});

document.getElementById('btn-apply').addEventListener('click', () => {
  const fullCSS = generateCSS() + '\n' + generateWindowCSS();
  sendToC({ action: 'apply_css', css: fullCSS });
  toast('Style applied — panel updated live ✓', 'success');
});

document.getElementById('btn-restart').addEventListener('click', () => {
  const fullCSS = generateCSS() + '\n' + generateWindowCSS();
  sendToC({
    action:  'save',
    layout:  generateLayoutJSON(),
    css:     fullCSS,
    state:   JSON.stringify({ styles: state.styles, panelBg: state.panelBg, winStyles: state.winStyles }),
  });
  setTimeout(() => sendToC({ action: 'restart_panel' }), 400);
  toast('Saving and restarting panel…');
});

document.getElementById('btn-reset').addEventListener('click', () => {
  if (!confirm('Discard all unsaved changes?')) return;
  state.styles      = {};
  state.selectedPill = null;
  hideInspector();
  renderZones();
  renderPreview();
  markDirty();
});

/* ── Toast ─────────────────────────────────────────────────────────── */
let _toastTimer = null;
function toast(msg, type = '') {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.className   = 'show ' + type;
  clearTimeout(_toastTimer);
  _toastTimer = setTimeout(() => { el.className = ''; }, 2800);
}

/* ── Inspector tab switching ───────────────────────────────────────── */
document.querySelectorAll('.itab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.itab').forEach(t => t.classList.remove('active'));
    tab.classList.add('active');
    document.querySelectorAll('.tab-content').forEach(c => c.classList.add('hidden'));
    document.getElementById('tab-' + tab.dataset.tab).classList.remove('hidden');
  });
});

/* ── Window styles system ──────────────────────────────────────────── */

/* CSS ID maps per window key — verified from gtk_widget_set_name() in source */
const WIN_CSS_MAP = {
  cc:        { outer: '#main-box',              header: '#card-actions',
               accent: null,                    window: null },
  notifs:    { outer: '#control-center-content',header: null,
               accent: null,                    window: null },
  sidebar:   { outer: '#sidebar',               header: '#clock_panel',
               accent: null,                    window: '#sidebar-popup-surface' },
  'app-menu':{ outer: '#app-menu-box',          header: null,
               accent: '.app-menu-item:hover',  window: null },
  volume:    { outer: '#mixer-outer',            header: '#mixer-header',
               accent: '#mixer-slider scale trough highlight', window: '#volume-mixer-window' },
  mic:       { outer: '#mic-mixer-outer',        header: '#mic-mixer-header',
               accent: '#mic-mixer-slider scale trough highlight', window: '#mic-mixer-window' },
  wifi:      { outer: '#wifi-popup-outer',       header: '#wifi-popup-header',
               accent: null,                    window: '#wifi-indicator-window' },
  battery:   { outer: '#battery-popup-outer',    header: '#battery-popup-header',
               accent: null,                    window: '#battery-indicator-window' },
};

/* Per-window style state */
if (!state.winStyles) state.winStyles = {};

function defaultWinStyle() {
  return { bgColor: '#161b22', bgAlpha: 97, radius: 14,
           borderOn: false, borderColor: '#ffffff', borderAlpha: 10,
           headerColor: '#7c3aed', headerAlpha: 15,
           accentColor: '#7c3aed', accentAlpha: 100,
           width: 320 };
}

function currentWinKey() {
  return document.getElementById('win-select').value;
}

function loadWinControls(key) {
  if (!state.winStyles[key]) state.winStyles[key] = defaultWinStyle();
  const s = state.winStyles[key];

  document.getElementById('win-bg-color').value        = s.bgColor;
  document.getElementById('win-bg-alpha').value        = s.bgAlpha;
  document.getElementById('win-bg-alpha-val').textContent = s.bgAlpha + '%';
  document.getElementById('win-radius').value          = s.radius;
  document.getElementById('win-radius-val').textContent = s.radius + 'px';
  document.getElementById('win-border-on').checked     = s.borderOn;
  document.getElementById('win-border-color').value    = s.borderColor;
  document.getElementById('win-border-alpha').value    = s.borderAlpha;
  document.getElementById('win-border-alpha-val').textContent = s.borderAlpha + '%';
  document.getElementById('win-header-color').value    = s.headerColor;
  document.getElementById('win-header-alpha').value    = s.headerAlpha;
  document.getElementById('win-header-alpha-val').textContent = s.headerAlpha + '%';
  document.getElementById('win-accent-color').value    = s.accentColor;
  document.getElementById('win-accent-alpha').value    = s.accentAlpha;
  document.getElementById('win-accent-alpha-val').textContent = s.accentAlpha + '%';
  document.getElementById('win-width').value           = s.width;
  document.getElementById('win-width-val').textContent = s.width + 'px';
}

function generateWindowCSS() {
  const lines = ['/* ── Window styles (Panel Designer) ── */'];

  /* Always ensure every key has at least default values */
  for (const key of Object.keys(WIN_CSS_MAP)) {
    if (!state.winStyles[key]) state.winStyles[key] = defaultWinStyle();
  }

  for (const [key, s] of Object.entries(state.winStyles)) {
    const map = WIN_CSS_MAP[key];
    if (!map) continue;
    const bg     = rgba(s.bgColor, s.bgAlpha);
    const border = s.borderOn
      ? `1px solid ${rgba(s.borderColor, s.borderAlpha)}`
      : 'none';
    const hdr    = rgba(s.headerColor, s.headerAlpha);
    const acc    = rgba(s.accentColor, s.accentAlpha);

    if (map.window) lines.push(`#${map.window.replace(/^#/,'')} { min-width: ${s.width}px; }`);
    if (map.outer)  lines.push(`${map.outer} { background-color: ${bg}; border-radius: ${s.radius}px; border: ${border}; }`);
    if (map.header) lines.push(`${map.header} { background-color: ${hdr}; }`);
    if (map.accent) lines.push(`${map.accent} { background-color: ${acc}; }`);
  }
  return lines.join('\n');
}

/* Wire window controls */
function wireWindowControls() {
  document.getElementById('win-select').addEventListener('change', () => {
    loadWinControls(currentWinKey());
  });

  const winControls = [
    ['win-bg-alpha',     'win-bg-alpha-val',     '%',  'bgAlpha'],
    ['win-radius',       'win-radius-val',        'px', 'radius'],
    ['win-border-alpha', 'win-border-alpha-val',  '%',  'borderAlpha'],
    ['win-header-alpha', 'win-header-alpha-val',  '%',  'headerAlpha'],
    ['win-accent-alpha', 'win-accent-alpha-val',  '%',  'accentAlpha'],
    ['win-width',        'win-width-val',         'px', 'width'],
  ];

  for (const [id, valId, unit, prop] of winControls) {
    document.getElementById(id).addEventListener('input', function() {
      document.getElementById(valId).textContent = this.value + unit;
      const key = currentWinKey();
      if (!state.winStyles[key]) state.winStyles[key] = defaultWinStyle();
      state.winStyles[key][prop] = Number(this.value);
      markDirty();
    });
  }

  for (const [id, prop] of [['win-bg-color','bgColor'],['win-border-color','borderColor'],
                              ['win-header-color','headerColor'],['win-accent-color','accentColor']]) {
    document.getElementById(id).addEventListener('input', function() {
      const key = currentWinKey();
      if (!state.winStyles[key]) state.winStyles[key] = defaultWinStyle();
      state.winStyles[key][prop] = this.value;
      markDirty();
    });
  }

  document.getElementById('win-border-on').addEventListener('change', function() {
    const key = currentWinKey();
    if (!state.winStyles[key]) state.winStyles[key] = defaultWinStyle();
    state.winStyles[key].borderOn = this.checked;
    markDirty();
  });

  document.getElementById('btn-reset-win').addEventListener('click', () => {
    const key = currentWinKey();
    state.winStyles[key] = defaultWinStyle();
    loadWinControls(key);
    markDirty();
  });

  document.getElementById('btn-apply-win').addEventListener('click', () => {
    const allCSS = generateCSS() + '\n' + generateWindowCSS();
    sendToC({ action: 'apply_css', css: allCSS });
    toast('Window style applied live ✓', 'success');
  });
}

/* Extend generateCSS to include window styles */
const _origGenerateCSS = generateCSS;
window.generateFullCSS = function() {
  return _origGenerateCSS() + '\n' + generateWindowCSS();
};

/* ── Boot ──────────────────────────────────────────────────────────── */
wireInspector();
wireWindowControls();
loadWinControls('cc');
