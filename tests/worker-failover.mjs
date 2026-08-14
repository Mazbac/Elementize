import worker from '../tools/cloudflare-relay/worker.js';

const request = new Request('https://relay.test/wp-json/elementize/v1/status');
const env = { TARGET_ORIGIN: 'https://dead.trycloudflare.com' };

const originalFetch = globalThis.fetch;
try {
  globalThis.fetch = async () => new Response('origin dns error', { status: 530 });
  let response = await worker.fetch(request, env);
  let body = await response.json();
  if (response.status !== 503 || body.code !== 'relay_unreachable') throw new Error('530 failover mapping failed');

  globalThis.fetch = async () => { throw new Error('network down'); };
  response = await worker.fetch(request, env);
  body = await response.json();
  if (response.status !== 503 || body.code !== 'relay_unreachable') throw new Error('network exception mapping failed');

  response = await worker.fetch(new Request('https://relay.test/__elementize_relay'), env);
  body = await response.json();
  if (response.status !== 503 || body.ready !== false) throw new Error('health failover mapping failed');
} finally {
  globalThis.fetch = originalFetch;
}

console.log('Worker failover contract OK');
