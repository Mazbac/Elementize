import { Paper, SimpleGrid, Stack, Text, Title } from '@mantine/core';
import type { ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

const safety = [
  ['Fresh state required', 'Every write is based on a fresh page read and exact expected values.'],
  ['Revision before change', 'Elementize requires a recoverable WordPress revision before a content write.'],
  ['Persisted verification', 'Changes are checked against saved Elementor data, not only memory.'],
  ['Published-page confirmation', 'Live pages require an additional explicit confirmation before editing.'],
] as const;

export function SettingsPage({ config }: Props) {
  return (
    <Stack gap="xl">
      <div>
        <Text className="elz-eyebrow">Settings</Text>
        <Title order={1}>Safety first, without the clutter.</Title>
        <Text className="elz-muted-copy" mt={8} maw={700}>
          Elementize keeps advanced safeguards on by design, so there is very little you need to configure here.
        </Text>
      </div>

      <section>
        <Text className="elz-eyebrow" mb="sm">Safety model</Text>
        <SimpleGrid cols={{ base: 1, sm: 2 }} spacing="md">
          {safety.map(([title, body]) => (
            <Paper key={title} className="elz-card" radius="lg" p="xl">
              <div className="elz-checkmark elz-checkmark--ready">✓</div>
              <Title order={3} mt="md">{title}</Title>
              <Text className="elz-muted-copy" mt={6}>{body}</Text>
            </Paper>
          ))}
        </SimpleGrid>
      </section>

      <Paper className="elz-card" radius="lg" p="xl">
        <Title order={3}>Environment</Title>
        <Stack gap="sm" mt="lg">
          <div className="elz-detail-row"><span>Elementize</span><strong>v{config.version}</strong></div>
          <div className="elz-detail-row"><span>Elementor</span><strong>{config.requirements.elementorDetail}</strong></div>
          <div className="elz-detail-row"><span>Pixfort</span><strong>{config.requirements.pixfortDetail}</strong></div>
          <div className="elz-detail-row"><span>WordPress revisions</span><strong>{config.requirements.revisions ? 'Enabled' : 'Needs attention'}</strong></div>
        </Stack>
      </Paper>
    </Stack>
  );
}
