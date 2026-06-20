const fs = require('fs');

const state = {
  selectedPill: 'sys-pill',
  AetherCoreBg: { color: '#000000', alpha: 0 },
  styles: {
    'menu-pill': { width: 0, height: 0, bgColor: '#000000', bgAlpha: 30, radius: 14, marginV: 4, marginH: 4, paddingH: 4, borderOn: false },
    'sys-pill': { width: 100, height: 80, bgColor: '#000000', bgAlpha: 30, radius: 14, marginV: 4, marginH: 4, paddingH: 4, borderOn: false }
  }
};

function rgba(hex, alpha) { return 'rgba(0,0,0,1)'; } // stub

function generateCSS() {
  const lines = ['/* AetherShell AetherCore Designer — Auto-generated */\n'];
  const pb = state.AetherCoreBg;
  lines.push(`#AetherCore-bar {\n    background-color: ${rgba(pb.color, pb.alpha)};\n}\n`);
  for (const [pillId, s] of Object.entries(state.styles)) {
    if (!s) continue;
    const border = s.borderOn ? `\n    border: 1px solid ${rgba(s.borderColor, s.borderAlpha)};` : '\n    border: none;';
    const w = s.width ?? 0;
    const h = s.height ?? 0;
    const sizeLines = [
      w > 0 ? `\n    min-width: ${w}px;\n    max-width: ${w}px;` : '',
      h > 0 ? `\n    min-height: ${h}px;\n    max-height: ${h}px;` : '',
    ].join('');
    lines.push(`#${pillId} {
    background-color: ${rgba(s.bgColor, s.bgAlpha)};
    border-radius: ${s.radius}px;
    margin-top: ${s.marginV}px;
    margin-bottom: ${s.marginV}px;
    margin-left: ${s.marginH}px;
    margin-right: ${s.marginH}px;
    padding: 0 ${s.paddingH}px;${border}${sizeLines}
}\n`);
  }
  return lines.join('\n');
}

console.log(generateCSS());
