import type { ElementizeAdminConfig } from '../types';

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

export async function generateConnectionKey(config: ElementizeAdminConfig): Promise<string> {
  const body = new URLSearchParams();
  body.set('action', 'elementize_generate_connection_key');
  body.set('nonce', config.nonces.generateKey);

  const response = await fetch(config.urls.ajax, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8' },
    body: body.toString(),
    credentials: 'same-origin',
  });
  const payload = (await response.json()) as {
    success?: boolean;
    data?: { connection_key?: string; message?: string };
  };

  if (!payload?.success || !payload.data?.connection_key) {
    throw new Error(payload?.data?.message || 'Could not generate the connection key.');
  }

  return payload.data.connection_key;
}
