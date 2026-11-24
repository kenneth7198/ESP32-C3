#pragma once

#include <Arduino.h>

// 網頁存在韌體內，手機直接開啟 ESP32-C3 的 IP 即可使用。
const char MONITOR_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-Hant">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MAX30102 訊號監看</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; background: #101725; color: #edf2ff; }
    body { max-width: 34rem; margin: auto; padding: 1.5rem 1rem; }
    h1 { font-size: 1.5rem; } p { line-height: 1.5; color: #b6c4df; }
    .card { background: #1c2940; padding: 1rem; margin: 1rem 0; border-radius: .8rem; }
    .value { font-size: 1.8rem; color: #8fb4ff; font-variant-numeric: tabular-nums; }
    canvas { width: 100%; height: 170px; background: #101725; border-radius: .5rem; }
  </style>
</head>
<body>
  <h1>MAX30102 訊號監看</h1>
  <p>紅光與紅外線原始讀值；不是血氧濃度或醫療診斷結果。</p>
  <div class="card">
    <p id="status" role="status">正在讀取感測器…</p>
    <div>紅光 <span class="value" id="red">—</span></div>
    <div>紅外線 <span class="value" id="ir">—</span></div>
  </div>
  <div class="card">
    <p>紅外線趨勢（每 0.5 秒讀取一次最新值）</p>
    <canvas id="chart" width="600" height="170" aria-label="紅外線趨勢圖"></canvas>
  </div>
  <script>
    const status = document.getElementById('status');
    const red = document.getElementById('red');
    const ir = document.getElementById('ir');
    const canvas = document.getElementById('chart');
    const context = canvas.getContext('2d');
    const points = [];
    function draw() {
      context.clearRect(0, 0, canvas.width, canvas.height);
      if (points.length < 2) return;
      const min = Math.min(...points), max = Math.max(...points);
      const range = Math.max(max - min, 1);
      context.strokeStyle = '#5eead4';
      context.lineWidth = 2;
      context.beginPath();
      points.forEach((v, i) => {
        const x = i * (canvas.width - 1) / 119;
        const y = canvas.height - 8 - (v - min) * (canvas.height - 16) / range;
        if (i === 0) context.moveTo(x, y); else context.lineTo(x, y);
      });
      context.stroke();
    }
    async function poll() {
      try {
        const response = await fetch('/api/max30102', { cache: 'no-store', signal: AbortSignal.timeout(4000) });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        red.textContent = data.red;
        ir.textContent = data.ir;
        status.textContent = '已連線；最新取樣距今 ' + data.age_ms + ' ms。';
        points.push(data.ir);
        if (points.length > 120) points.shift();
        draw();
      } catch (error) {
        status.textContent = '無法讀取感測器：' + error.message + '。請檢查接線與網路。';
        red.textContent = '—';
        ir.textContent = '—';
        points.length = 0;
        draw();
      }
      setTimeout(poll, 500);
    }
    poll();
  </script>
</body>
</html>
)HTML";
