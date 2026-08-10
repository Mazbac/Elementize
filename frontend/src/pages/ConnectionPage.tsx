import { Button, Group, Paper, Stack, Text, TextInput, Title } from '@mantine/core';
import { useState } from 'react';
import type { ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

export function ConnectionPage({ config }: Props) {
  const [technicalOpen, setTechnicalOpen] = useState(false);
  const ready = config.environment.connectionReady;

  return (
    <Stack gap="xl">
      <div>
        <Text className="elz-eyebrow">Connection</Text>
        <Title order={1}>Keep the connection simple.</Title>
        <Text className="elz-muted-copy" mt={8} maw={700}>
          Most users never need to change this after setup. Elementize only needs a public HTTPS address that ChatGPT can reach.
        </Text>
      </div>

      <Paper className="elz-connection-hero" radius="xl" p="xl">
        <div className={ready ? 'elz-status-pill elz-status-pill--ready' : 'elz-status-pill'}>
          <span className="elz-status-dot" />
          {ready ? 'Secure address ready' : 'Secure address required'}
        </div>
        <Title order={2} mt="md">{ready ? config.environment.effectiveOrigin : 'Connect this WordPress site to the public web.'}</Title>
        <Text className="elz-muted-copy" mt={8} maw={720}>
          {ready
            ? 'This is the address Elementize places into the Action schema for your Custom GPT.'
            : 'For local WordPress sites, use your HTTPS tunnel address. Production sites can usually use their normal HTTPS domain.'}
        </Text>
      </Paper>

      <Paper className="elz-card" radius="lg" p="xl">
        <Title order={3}>Secure website address</Title>
        <Text className="elz-muted-copy" size="sm" mt={6} mb="lg">
          Paste only the HTTPS origin. Do not add /wp-json or another path.
        </Text>
        <form method="post" action={config.urls.adminPost}>
          <input type="hidden" name="action" value="elementize_save_connection" />
          <input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
          <Stack gap="md">
            <TextInput
              name="elementize_public_api_origin"
              type="url"
              defaultValue={config.environment.storedPublic}
              placeholder={config.environment.effectiveOrigin || 'https://your-secure-address.example'}
              className="elz-input"
              aria-label="Public HTTPS address"
            />
            <Group>
              <Button type="submit" className="elz-primary-button">Save address</Button>
            </Group>
          </Stack>
        </form>

        {config.environment.storedPublic && (
          <form method="post" action={config.urls.adminPost} className="elz-clear-form">
            <input type="hidden" name="action" value="elementize_save_connection" />
            <input type="hidden" name="elementize_clear_public_origin" value="1" />
            <input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
            <Button type="submit" className="elz-quiet-button" mt="md">Use automatic site address instead</Button>
          </form>
        )}
      </Paper>

      <Paper className="elz-card" radius="lg" p="xl">
        <Group justify="space-between" align="center">
          <div>
            <Text fw={800}>Technical details</Text>
            <Text className="elz-muted-copy" size="sm">Only open this if you are troubleshooting.</Text>
          </div>
          <Button className="elz-quiet-button" onClick={() => setTechnicalOpen((value) => !value)}>
            {technicalOpen ? 'Hide' : 'Show'}
          </Button>
        </Group>
        {technicalOpen && (
          <Stack gap="sm" mt="lg">
            <div className="elz-detail-row"><span>WordPress site</span><strong>{config.environment.siteOrigin || 'Unavailable'}</strong></div>
            <div className="elz-detail-row"><span>Public HTTPS detected</span><strong>{config.environment.sitePublicHttps ? 'Yes' : 'No'}</strong></div>
            <div className="elz-detail-row"><span>Stored override</span><strong>{config.environment.storedPublic || 'None'}</strong></div>
          </Stack>
        )}
      </Paper>
    </Stack>
  );
}
