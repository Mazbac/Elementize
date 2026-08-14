export default {
  async fetch(request, env) {
    const incoming = new URL(request.url);

    if (incoming.pathname === '/__elementize_relay') {
      return Response.json({ ready: Boolean(env.TARGET_ORIGIN) });
    }

    if (!incoming.pathname.startsWith('/wp-json/elementize/v1/')) {
      return new Response('Not found', { status: 404 });
    }

    if (!env.TARGET_ORIGIN) {
      return new Response('Elementize local tunnel is not connected.', { status: 503 });
    }

    const target = new URL(incoming.pathname + incoming.search, env.TARGET_ORIGIN);
    const upstream = new Request(target, request);
    return fetch(upstream);
  },
};
