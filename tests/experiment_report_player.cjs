// 离线播放器的事件逻辑测试；不代表真实浏览器布局或交互验收
const fs = require('fs');
const vm = require('vm');
const assert = require('assert/strict');
const html = fs.readFileSync(process.argv[2], 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const players = JSON.parse(script.match(/const players=([\s\S]*?);\ndocument/)[1]);
let serial = 0;
const timers = new Map();
const elements = players.filter(p => p.frames.length).map((p, i) => {
  const nodes = Object.fromEntries(['img', '.slider', '.play', '.caption', '.prev', '.next']
    .map(key => [key, { value: 0, textContent: '', src: '' }]));
  return { dataset: { player: i }, nodes, querySelector: key => nodes[key] };
});
vm.runInNewContext(script, {
  document: { querySelectorAll: () => elements },
  setTimeout: (fn, delay) => { assert(delay >= 80); timers.set(++serial, fn); return serial; },
  clearTimeout: id => timers.delete(id)
});
elements.forEach((element, i) => {
  const n = element.nodes, frames = players[i].frames;
  assert.equal(n.img.src, frames[0].image);
  n['.next'].onclick();
  assert.equal(n.img.src, frames[Math.min(1, frames.length - 1)].image);
  n['.prev'].onclick();
  assert.equal(n.img.src, frames[0].image);
  n['.slider'].value = frames.length - 1;
  n['.slider'].oninput();
  assert(n['.caption'].textContent.includes('mesh ' + frames.at(-1).meshHash));
  n['.play'].onclick();
  assert.equal(n.img.src, frames[0].image);
  n['.play'].onclick();
  assert.equal(timers.size, 0);
  n['.play'].onclick();
  let count = 0;
  while (timers.size) {
    const [id, fn] = timers.entries().next().value;
    timers.delete(id); fn();
    assert(++count <= frames.length);
  }
  assert.equal(n.img.src, frames.at(-1).image);
  assert.equal(n['.play'].textContent, '播放');
});
console.log(JSON.stringify({ logicChecks: 'pass', players: elements.length,
  actualBrowserInteraction: 'not tested by this software unit test' }));
