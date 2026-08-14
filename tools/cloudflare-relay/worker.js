export default {
  async fetch(request, env) {
    const incoming = new URL(request.url);

    if (incoming.pathname === '/__elementize_relay') {
      if (!env.TARGET_ORIGIN) {
        return Response.json({ ready: false, code: 'relay_disconnected' }, { status: 503 });
      }
      try {
        const probe = new URL('/wp-json/elementize/v1/status', env.TARGET_ORIGIN);
        const response = await fetch(new Request(probe, { method: 'GET' }));
        const ready = response.status < 520 || response.status > 530;
        return Response.json({ ready, upstream_status: response.status }, { status: ready ? 200 : 503 });
      } catch {
        return Response.json({ ready: false, code: 'relay_unreachable' }, { status: 503 });
      }
    }

    if (!incoming.pathname.startsWith('/wp-json/elementize/v1/')) {
      return new Response('Not found', { status: 404 });
    }

    if (!env.TARGET_ORIGIN) {
      return Response.json({ error: 'Elementize local tunnel is not connected.', code: 'relay_disconnected' }, { status: 503 });
    }

    const target = new URL(incoming.pathname + incoming.search, env.TARGET_ORIGIN);
    const upstream = new Request(target, request);
    try {
      const response = await fetch(upstream);
      if (response.status >= 520 && response.status <= 530) {
        return Response.json({ error: 'Elementize relay is temporarily unavailable.', code: 'relay_unreachable' }, { status: 503 });
      }
      return response;
    } catch {
      return Response.json({ error: 'Elementize relay is temporarily unavailable.', code: 'relay_unreachable' }, { status: 503 });
    }
  },
};
