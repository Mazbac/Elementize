import { spawn } from 'node:child_process';
import { readFile, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';

const [browser, url, screenshot, profile, widthRaw, heightRaw, maxHeightRaw, settleRaw] = process.argv.slice(2);
const width = Number(widthRaw || 1440);
const height = Number(heightRaw || 900);
const maxHeight = Number(maxHeightRaw || 12000);
const settleMs = Number(settleRaw || 1800);
if (!browser || !url || !screenshot || !profile || !Number.isFinite(width) || !Number.isFinite(height)) throw new Error('Invalid CDP capture arguments.');
if (typeof fetch !== 'function' || typeof WebSocket !== 'function') throw new Error('Node 22+ with built-in fetch and WebSocket is required.');

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const chrome = spawn(browser, [
  '--headless=new', '--disable-gpu', '--disable-dev-shm-usage', '--hide-scrollbars', '--mute-audio',
  '--no-first-run', '--no-default-browser-check', '--disable-extensions', '--disable-background-networking',
  '--disable-sync', '--metrics-recording-only', '--disable-logging', '--log-level=3',
  '--ignore-certificate-errors', '--allow-insecure-localhost', '--remote-debugging-port=0', '--remote-allow-origins=*',
  `--user-data-dir=${profile}`, 'about:blank'
], { stdio: 'ignore', windowsHide: true });

let ws;
try {
  const portFile = `${profile}/DevToolsActivePort`;
  for (let n = 0; n < 120 && !existsSync(portFile); n++) await sleep(50);
  if (!existsSync(portFile)) throw new Error('Chrome DevToolsActivePort was not created.');
  const [port] = String(await readFile(portFile, 'utf8')).trim().split(/\r?\n/);
  let target = null;
  for (let n = 0; n < 80 && !target; n++) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      const list = await response.json();
      target = list.find(item => item && item.type === 'page') || null;
    } catch {}
    if (!target) await sleep(50);
  }
  if (!target?.webSocketDebuggerUrl) throw new Error('No inspectable Chrome page target was found.');

  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    ws.addEventListener('open', resolve, { once: true });
    ws.addEventListener('error', reject, { once: true });
  });
  let nextId = 1;
  const pending = new Map();
  ws.addEventListener('message', event => {
    const message = JSON.parse(String(event.data));
    if (!message.id || !pending.has(message.id)) return;
    const waiter = pending.get(message.id);
    pending.delete(message.id);
    message.error ? waiter.reject(new Error(JSON.stringify(message.error))) : waiter.resolve(message.result);
  });
  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method, params }));
  });

  await send('Page.enable');
  await send('Runtime.enable');
  await send('Emulation.setDeviceMetricsOverride', {
    width, height, deviceScaleFactor: 1, mobile: width <= 500,
    screenWidth: width, screenHeight: height,
    screenOrientation: { angle: 0, type: 'portraitPrimary' }
  });
  if (width <= 768) await send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
  await send('Page.navigate', { url });
  for (let n = 0; n < 100; n++) {
    const ready = await send('Runtime.evaluate', { expression: 'document.readyState', returnByValue: true });
    if (ready?.result?.value === 'complete') break;
    await sleep(50);
  }
  await sleep(Math.max(300, settleMs));
  const measured = await send('Runtime.evaluate', {
    expression: '({innerWidth,innerHeight,scrollWidth:document.documentElement.scrollWidth,scrollHeight:Math.max(document.documentElement.scrollHeight,document.body?document.body.scrollHeight:0)})',
    returnByValue: true
  });
  const metrics = measured?.result?.value || {};
  const captureHeight = Math.max(height, Math.min(Number(metrics.scrollHeight || height), maxHeight));
  const shot = await send('Page.captureScreenshot', {
    format: 'png', captureBeyondViewport: true, fromSurface: true,
    clip: { x: 0, y: 0, width, height: captureHeight, scale: 1 }
  });
  if (!shot?.data) throw new Error('Chrome did not return screenshot data.');
  await writeFile(screenshot, Buffer.from(shot.data, 'base64'));
  process.stdout.write(JSON.stringify({ ok: true, width, height, captureHeight, metrics }) + '\n');
} finally {
  try { ws?.close(); } catch {}
  try { chrome.kill(); } catch {}
}
