async function relayUnavailable(response) {
  if (response.status >= 520 && response.status <= 530) return true;
  if (response.status < 400) return false;

  const contentType = response.headers.get('content-type') || '';
  if (!contentType.includes('text/html') && !contentType.includes('text/plain')) return false;
  try {
    const body = await response.clone().text();
    return /ERR_NGROK_(3200|3209|3210|8012)|endpoint is offline/i.test(body);
  } catch {
    return false;
  }
}

function upstreamRequest(request, target) {
  const upstream = new Request(target, request);
  upstream.headers.set('ngrok-skip-browser-warning', '1');
  return upstream;
}

async function fetchUpstream(request, target) {
  try {
    const response = await fetch(upstreamRequest(request, target));
    if (await relayUnavailable(response)) {
      return Response.json({ error: 'Elementize relay is temporarily unavailable.', code: 'relay_unreachable' }, { status: 503 });
    }
    return response;
  } catch {
    return Response.json({ error: 'Elementize relay is temporarily unavailable.', code: 'relay_unreachable' }, { status: 503 });
  }
}
export default {
  async fetch(request, env) {
    const incoming = new URL(request.url);

    if (!env.TARGET_ORIGIN) {
      return Response.json({ error: 'Elementize relay is not configured.', code: 'relay_disconnected' }, { status: 503 });
    }

    if (incoming.pathname === '/__elementize_relay') {
      const probe = new URL('/wp-json/elementize/v1/status', env.TARGET_ORIGIN);
      const response = await fetchUpstream(new Request(probe, { method: 'GET' }), probe);
      const ready = response.status !== 503;
      return Response.json({ ready, upstream_status: response.status }, { status: ready ? 200 : 503 });
    }

    if (!incoming.pathname.startsWith('/wp-json/elementize/v1/')) {
      return new Response('Not found', { status: 404 });
    }

    const target = new URL(incoming.pathname + incoming.search, env.TARGET_ORIGIN);
    return fetchUpstream(request, target);
  },
};
