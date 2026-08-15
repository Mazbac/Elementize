import worker from '../tools/cloudflare-relay/worker.js';

const request = new Request('https://relay.test/wp-json/elementize/v1/status');
const env = { TARGET_ORIGIN: 'https://assigned.ngrok-free.app' };
const originalFetch = globalThis.fetch;

async function expectRelayUnavailable(response, label) {
  const body = await response.json();
  if (response.status !== 503 || body.code !== 'relay_unreachable') throw new Error(`${label} failover mapping failed`);
}

try {
  globalThis.fetch = async () => new Response('origin dns error', { status: 530 });
  await expectRelayUnavailable(await worker.fetch(request, env), '530');

  globalThis.fetch = async () => new Response('<html>ERR_NGROK_3200 endpoint is offline</html>', { status: 404, headers: { 'content-type': 'text/html' } });
  await expectRelayUnavailable(await worker.fetch(request, env), 'ngrok offline');

  let forwardedHeader = '';
  globalThis.fetch = async (upstream) => {
    forwardedHeader = upstream.headers.get('ngrok-skip-browser-warning') || '';
    return Response.json({ code: 'rest_no_route' }, { status: 404 });
  };
  let response = await worker.fetch(request, env);
  if (response.status !== 404) throw new Error('WordPress JSON 404 must pass through');
  if (forwardedHeader !== '1') throw new Error('ngrok interstitial bypass header missing');

  globalThis.fetch = async () => new Response('auth required', { status: 401 });
  response = await worker.fetch(new Request('https://relay.test/__elementize_relay'), env);
  const health = await response.json();
  if (response.status !== 200 || health.ready !== true || health.upstream_status !== 401) throw new Error('health readiness mapping failed');

  globalThis.fetch = async () => { throw new Error('network down'); };
  await expectRelayUnavailable(await worker.fetch(request, env), 'network exception');
} finally {
  globalThis.fetch = originalFetch;
}

console.log('Worker failover contract OK');
