const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

test('monitor displays samples and clears stale values on errors', async () => {
  const html = fs.readFileSync(path.join(__dirname, '..', 'monitor_page.h'), 'utf8');
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements = {
    status: { textContent: '' },
    red: { textContent: '' },
    ir: { textContent: '' },
    chart: {
      width: 600, height: 170,
      getContext: () => ({ clearRect() {}, beginPath() {}, moveTo() {}, lineTo() {}, stroke() {} })
    }
  };
  let nextPoll;
  let response = { red: 123, ir: 456, age_ms: 10 };
  const context = {
    document: { getElementById: id => elements[id] },
    AbortSignal,
    fetch: async () => {
      if (response instanceof Error) throw response;
      return { ok: true, json: async () => response };
    },
    setTimeout: fn => { nextPoll = fn; }
  };
  vm.runInNewContext(script, context);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(elements.red.textContent, 123);
  assert.equal(elements.ir.textContent, 456);
  assert.match(elements.status.textContent, /10 ms/);

  response = new Error('offline');
  await nextPoll();
  assert.equal(elements.red.textContent, '—');
  assert.equal(elements.ir.textContent, '—');
  assert.match(elements.status.textContent, /offline/);
});
