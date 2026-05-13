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

  /* Called by C monitor when a .so is added / removed from the plugin dir */
  updatePlugins(newPlugins) {
    const prev = state.plugins.length;
    state.plugins = newPlugins || [];
    renderPalette();
    const diff = state.plugins.length - prev;
    if (diff > 0)
      toast(`🔌 ${diff} new plugin${diff > 1 ? 's' : ''} detected`, 'success');
    else if (diff < 0)
      toast(`🗑️ Plugin removed — list updated`, '');
    else
      toast('🔌 Plugin list refreshed', '');
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

/*
 * WIN_CSS_MAP — maps plugin-id → { windowId, outerId }
 *
 * windowId : GTK name set on the popup window (for CSS scope isolation)
 * outerId  : GTK name of the inner styled container (bg/border/radius go here)
 *            NULL = windowId IS the styled element (e.g. sidebar card)
 *
 * Must match the names used in gtk_widget_set_name() in each indicator source
 * file AND the window_css_id / outer_css_id in builtin_plugins.c.
 *
 * CSS generated: "#windowId #outerId { ... }" or "#windowId { ... }" if
 * outerId is null.  All other rules use "#windowId child { ... }" scope.
 */
const WIN_CSS_MAP = {
  'aether-volume':  { windowId: 'volume-mixer-window',      outerId: 'mixer-outer' },
  'aether-mic':     { windowId: 'mic-mixer-window',         outerId: 'mic-mixer-outer' },
  'aether-wifi':    { windowId: 'wifi-indicator-window',    outerId: 'wifi-popup-outer' },
  'aether-bt':      { windowId: 'bt-indicator-window',      outerId: 'wifi-popup-outer' },
  'aether-battery': { windowId: 'battery-indicator-window', outerId: 'battery-popup-outer' },
  'aether-notifs':  { windowId: 'notifications-popover',    outerId: 'main-box' },
  'aether-cc':      { windowId: 'control-center-popover',   outerId: 'main-box' },
  'aether-appmenu': { windowId: 'app-menu-popover',         outerId: 'app-menu-box' },
  'aether-clock':   { windowId: 'sidebar',                  outerId: null },
};

/* Per-window style state — keyed by plugin id (e.g. 'aether-volume') */
if (!state.winStyles) state.winStyles = {};

/* Default theme values mirror the AETHER_THEME_DARK macro dark palette */
function defaultWinStyle(pluginId) {
  /* Accent colour per plugin — mirrors builtin_plugins.c theme definitions */
  const ACCENTS = {
    'aether-volume':  '#00e5ff',  /* cyan  */
    'aether-mic':     '#ff4444',  /* red   */
    'aether-wifi':    '#4fc3f7',  /* light blue */
    'aether-bt':      '#448aff',  /* indigo blue */
    'aether-battery': '#69f0ae',  /* green */
    'aether-notifs':  '#ce93d8',  /* purple */
    'aether-cc':      '#00e5ff',  /* cyan  */
    'aether-appmenu': '#eeeeee',  /* white */
    'aether-clock':   '#00e5ff',  /* cyan  */
  };
  return {
    accentColor:   ACCENTS[pluginId] || '#00e5ff',
    accentAlpha:   100,
    bgColor:       '#0c0c0e',
    bgAlpha:       92,
    surfaceColor:  '#181820',
    surfaceAlpha:  85,
    elementColor:  '#2a2a30',
    elementAlpha:  100,
    textColor:     '#cccccc',
    textAlpha:     100,
    text2Color:    '#888888',
    text2Alpha:    100,
    iconColor:     '#888888',
    iconAlpha:     100,
    borderOn:      true,
    borderColor:   '#ffffff',
    borderAlpha:   10,
    radius:        14,
    width:         320,
  };
}

function currentWinKey() {
  return document.getElementById('win-select').value;
}

function loadWinControls(key) {
  if (!state.winStyles[key]) state.winStyles[key] = defaultWinStyle(key);
  const s   = state.winStyles[key];
  const map = WIN_CSS_MAP[key];

  /* Show scope hint so the user knows which GTK selectors are affected */
  const outerSel = map ? (map.outerId
    ? `#${map.windowId} #${map.outerId}`
    : `#${map.windowId}`) : '?';
  const hint = document.getElementById('win-scope-code');
  if (hint) hint.textContent = outerSel;

  function sv(id, val) { const e = document.getElementById(id); if (e) e.value = val; }
  function st(id, val) { const e = document.getElementById(id); if (e) e.textContent = val; }
  function sc(id, val) { const e = document.getElementById(id); if (e) e.checked = val; }

  sv('win-accent-color',   s.accentColor);   st('win-accent-alpha-val',  s.accentAlpha + '%');  sv('win-accent-alpha',  s.accentAlpha);
  sv('win-bg-color',       s.bgColor);       st('win-bg-alpha-val',      s.bgAlpha + '%');       sv('win-bg-alpha',      s.bgAlpha);
  sv('win-surface-color',  s.surfaceColor);  st('win-surface-alpha-val', s.surfaceAlpha + '%'); sv('win-surface-alpha', s.surfaceAlpha);
  sv('win-element-color',  s.elementColor);  st('win-element-alpha-val', s.elementAlpha + '%'); sv('win-element-alpha', s.elementAlpha);
  sv('win-text-color',     s.textColor);     st('win-text-alpha-val',    s.textAlpha + '%');     sv('win-text-alpha',    s.textAlpha);
  sv('win-text2-color',    s.text2Color);    st('win-text2-alpha-val',   s.text2Alpha + '%');    sv('win-text2-alpha',   s.text2Alpha);
  sv('win-icon-color',     s.iconColor);     st('win-icon-alpha-val',    s.iconAlpha + '%');     sv('win-icon-alpha',    s.iconAlpha);
  sc('win-border-on',      s.borderOn);
  sv('win-border-color',   s.borderColor);   st('win-border-alpha-val',  s.borderAlpha + '%');  sv('win-border-alpha',  s.borderAlpha);
  sv('win-radius',         s.radius);        st('win-radius-val',        s.radius + 'px');
  sv('win-width',          s.width);         st('win-width-val',         s.width + 'px');
}

/*
 * generateWindowCSS — produces per-plugin SCOPED CSS.
 *
 * Every rule is prefixed with the window's GTK id selector so rules from
 * different plugins can NEVER conflict, even if they share inner widget names
 * (e.g. both bt and wifi use "#wifi-popup-outer" internally).
 *
 * Selector structure:
 *   Background/border/radius → "#windowId #outerId { ... }"
 *   Labels                  → "#windowId label { ... }"
 *   Icons                   → "#windowId image { ... }"
 *   Slider trough           → "#windowId scale trough { ... }"
 *   Slider fill (accent)    → "#windowId scale highlight { ... }"
 *   Toggle bg               → "#windowId switch { ... }"
 *   Toggle active (accent)  → "#windowId switch:checked { ... }"
 *   Surface/card            → "#windowId .card { ... }"
 */
function generateWindowCSS() {
  const lines = ['/* ── Window theme overrides (Panel Designer) ── */'];

  for (const [pluginId, s] of Object.entries(state.winStyles)) {
    const map = WIN_CSS_MAP[pluginId];
    if (!map) continue;

    const win     = map.windowId;
    const outerSel = map.outerId ? `#${win} #${map.outerId}` : `#${win}`;
    const winSel   = `#${win}`;

    const bg      = rgba(s.bgColor,      s.bgAlpha);
    const surface = rgba(s.surfaceColor, s.surfaceAlpha);
    const element = rgba(s.elementColor, s.elementAlpha);
    const text    = rgba(s.textColor,    s.textAlpha);
    const text2   = rgba(s.text2Color,   s.text2Alpha);
    const icon    = rgba(s.iconColor,    s.iconAlpha);
    const accent  = rgba(s.accentColor,  s.accentAlpha);
    const border  = s.borderOn
      ? `1px solid ${rgba(s.borderColor, s.borderAlpha)}`
      : 'none';

    lines.push(
      `/* ${pluginId} — scoped under #${win} */`,
      `${outerSel} { background-color: ${bg}; border-radius: ${s.radius}px; border: ${border}; min-width: ${s.width}px; }`,
      `${winSel} label { color: ${text}; }`,
      `${winSel} .muted, ${winSel} .subtitle { color: ${text2}; }`,
      `${winSel} image { color: ${icon}; }`,
      `${winSel} .card { background-color: ${surface}; }`,
      `${winSel} scale trough { background-color: ${element}; }`,
      `${winSel} switch { background-color: ${element}; }`,
      `${winSel} scale highlight { background-color: ${accent}; }`,
      `${winSel} switch:checked { background-color: ${accent}; }`,
      `${winSel} .active-cyan { color: ${accent}; background-color: ${rgba(s.accentColor, 15)}; border-color: ${rgba(s.accentColor, 30)}; box-shadow: 0 0 12px ${rgba(s.accentColor, 15)}; }`,
      `${winSel} .active-cyan-text { color: ${accent}; }`,
      ''
    );
  }
  return lines.join('\n');
}

/* Wire window controls */
function wireWindowControls() {
  document.getElementById('win-select').addEventListener('change', () => {
    loadWinControls(currentWinKey());
  });

  /* Range sliders — [inputId, displayId, unit, stateKey] */
  const winRanges = [
    ['win-accent-alpha',  'win-accent-alpha-val',  '%',  'accentAlpha'],
    ['win-bg-alpha',      'win-bg-alpha-val',      '%',  'bgAlpha'],
    ['win-surface-alpha', 'win-surface-alpha-val', '%',  'surfaceAlpha'],
    ['win-element-alpha', 'win-element-alpha-val', '%',  'elementAlpha'],
    ['win-text-alpha',    'win-text-alpha-val',    '%',  'textAlpha'],
    ['win-text2-alpha',   'win-text2-alpha-val',   '%',  'text2Alpha'],
    ['win-icon-alpha',    'win-icon-alpha-val',    '%',  'iconAlpha'],
    ['win-border-alpha',  'win-border-alpha-val',  '%',  'borderAlpha'],
    ['win-radius',        'win-radius-val',        'px', 'radius'],
    ['win-width',         'win-width-val',         'px', 'width'],
  ];
  for (const [id, valId, unit, prop] of winRanges) {
    const el = document.getElementById(id);
    if (!el) continue;
    el.addEventListener('input', function() {
      document.getElementById(valId).textContent = this.value + unit;
      const key = currentWinKey();
      if (!state.winStyles[key]) state.winStyles[key] = defaultWinStyle(key);
      state.winStyles[key][prop] = Number(this.value);
      markDirty();
    });
  }

  /* Color pickers — [inputId, stateKey] */
  const winColors = [
    ['win-accent-color',  'accentColor'],
    ['win-bg-color',      'bgColor'],
    ['win-surface-color', 'surfaceColor'],
    ['win-element-color', 'elementColor'],
    ['win-text-color',    'textColor'],
    ['win-text2-color',   'text2Color'],
    ['win-icon-color',    'iconColor'],
    ['win-border-color',  'borderColor'],
  ];
  for (const [id, prop] of winColors) {
    const el = document.getElementById(id);
    if (!el) continue;
    el.addEventListener('input', function() {
      const key = currentWinKey();
      if (!state.winStyles[key]) state.winStyles[key] = defaultWinStyle(key);
      state.winStyles[key][prop] = this.value;
      markDirty();
    });
  }

  /* Checkbox */
  document.getElementById('win-border-on').addEventListener('change', function() {
    const key = currentWinKey();
    if (!state.winStyles[key]) state.winStyles[key] = defaultWinStyle(key);
    state.winStyles[key].borderOn = this.checked;
    markDirty();
  });

  /* Reset button */
  document.getElementById('btn-reset-win').addEventListener('click', () => {
    const key = currentWinKey();
    state.winStyles[key] = defaultWinStyle(key);
    loadWinControls(key);
    markDirty();
  });

  /* Apply immediately */
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
loadWinControls('aether-volume');
