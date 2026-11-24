#pragma once

#include <Arduino.h>

const char kWebPage[] PROGMEM = R"PAGE(
<!doctype html>
<html lang="zh-Hant">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-C3 燈條控制</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; background: #101725; color: #edf2ff; }
    body { max-width: 32rem; margin: 0 auto; padding: 2rem 1rem; }
    h1 { font-size: 1.6rem; } p { color: #b6c4df; line-height: 1.5; }
    form { background: #1c2940; padding: 1.25rem; border-radius: 1rem; display: grid; gap: 1.2rem; }
    label { display: grid; gap: .5rem; } select, button { font: inherit; padding: .7rem; border-radius: .5rem; }
    select { background: #101725; color: inherit; border: 1px solid #71819e; }
    input[type=color] { width: 100%; height: 3rem; padding: .2rem; }
    input[type=range] { width: 100%; }
    button { border: 0; background: #91b4ff; color: #101725; font-weight: bold; cursor: pointer; }
    button:disabled { opacity: .5; cursor: wait; }
    #message { min-height: 1.5em; }
  </style>
</head>
<body>
  <h1>ESP32-C3 燈條控制</h1>
  <p>控制 GPIO 2 上的 30 顆 WS2812B。設定在裝置重新啟動後會重置。</p>
  <form id="controls">
    <label>燈效
      <select id="mode" name="mode">
        <option value="rainbow">彩虹</option>
        <option value="solid">單色</option>
        <option value="off">關閉</option>
      </select>
    </label>
    <label>單色顏色 <input id="color" name="color" type="color" value="#ff0000"></label>
    <label>亮度 <span><output id="brightnessValue">20</output> / 20</span>
      <input id="brightness" name="brightness" type="range" min="0" max="20" value="20">
    </label>
    <button id="apply" type="submit" disabled>套用設定</button>
  </form>
  <p id="message" role="status" aria-live="polite">正在讀取裝置狀態…</p>
  <button id="refresh" type="button">重新讀取狀態</button>
  <script>
    const form = document.getElementById('controls');
    const mode = document.getElementById('mode');
    const color = document.getElementById('color');
    const brightness = document.getElementById('brightness');
    const brightnessValue = document.getElementById('brightnessValue');
    const message = document.getElementById('message');
    const apply = document.getElementById('apply');
    const refresh = document.getElementById('refresh');
    brightness.addEventListener('input', () => brightnessValue.value = brightness.value);
    async function request(url, options) {
      const response = await fetch(url, { ...options, cache: 'no-store', signal: AbortSignal.timeout(8000) });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      return response.json();
    }
    function showState(state) {
      mode.value = state.mode;
      color.value = '#' + state.color;
      brightness.value = state.brightness;
      brightnessValue.value = state.brightness;
    }
    async function load() {
      apply.disabled = true;
      refresh.disabled = true;
      message.textContent = '正在讀取裝置狀態…';
      try {
        showState(await request('/api/state'));
        message.textContent = '已連線。';
        apply.disabled = false;
      } catch (error) {
        message.textContent = '無法讀取裝置：' + error.message + '。請確認 Wi-Fi 連線後重試。';
      } finally {
        refresh.disabled = false;
      }
    }
    form.addEventListener('submit', async event => {
      event.preventDefault();
      apply.disabled = true;
      refresh.disabled = true;
      message.textContent = '正在套用…';
      try {
        const body = new URLSearchParams({ mode: mode.value, color: color.value.slice(1), brightness: brightness.value });
        showState(await request('/api/state', { method: 'POST', body }));
        message.textContent = '設定已套用。';
        apply.disabled = false;
      } catch (error) {
        message.textContent = '無法確認設定：' + error.message + '。請重新讀取狀態。';
      } finally {
        refresh.disabled = false;
      }
    });
    refresh.addEventListener('click', load);
    load();
  </script>
</body>
</html>
)PAGE";
