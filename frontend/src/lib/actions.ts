import type { ActivityResponse, ElementizeAdminConfig } from '../types';

export async function copyText(text: string): Promise<void> {
  if (navigator.clipboard && window.isSecureContext) {
    await navigator.clipboard.writeText(text);
    return;
  }

  const textarea = document.createElement('textarea');
  textarea.value = text;
  textarea.setAttribute('readonly', '');
  textarea.style.position = 'fixed';
  textarea.style.opacity = '0';
  document.body.appendChild(textarea);
  textarea.select();
  const copied = document.execCommand('copy');
  document.body.removeChild(textarea);
  if (!copied) throw new Error('Clipboard access is unavailable in this browser.');
}

async function postAdminAjax<T>(config: ElementizeAdminConfig, action: string, fields: Record<string, string> = {}): Promise<T> {
  const body = new URLSearchParams();
  body.set('action', action);
  body.set('nonce', config.nonces.generateKey);
  Object.entries(fields).forEach(([key, value]) => body.set(key, value));

  const response = await fetch(config.urls.ajax, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8' },
    body: body.toString(),
    credentials: 'same-origin',
  });
  const payload = (await response.json()) as {
    success?: boolean;
    data?: T & { message?: string };
  };

  if (!payload?.success || !payload.data) {
    throw new Error(payload?.data?.message || 'Elementize could not complete the request.');
  }
  return payload.data;
}

export async function generateConnectionKey(config: ElementizeAdminConfig): Promise<string> {
  const data = await postAdminAjax<{ connection_key?: string }>(config, 'elementize_generate_connection_key');
  if (!data.connection_key) throw new Error('Could not generate the connection key.');
  return data.connection_key;
}

export async function getActivity(config: ElementizeAdminConfig, limit = 20): Promise<ActivityResponse> {
  return postAdminAjax<ActivityResponse>(config, 'elementize_get_activity', { limit: String(limit) });
}

export async function undoActivity(config: ElementizeAdminConfig, activityId: string, published: boolean): Promise<void> {
  await postAdminAjax(config, 'elementize_undo_activity', {
    activity_id: activityId,
    confirm_published_page: published ? '1' : '0',
  });
}
