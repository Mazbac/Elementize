import { Button, Group, Paper, SimpleGrid, Stack, Text, Title } from '@mantine/core';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = {
  config: ElementizeAdminConfig;
  onNavigate: (page: PageKey) => void;
};

const capabilities = [
  ['Copy & links', 'Update recognized text, buttons and destinations without touching layout.'],
  ['Images', 'Use existing Media Library images, ChatGPT uploads or permitted public web images.'],
  ['Pixfort icons', 'Search the installed Pixfort icon library and use exact verified icons.'],
  ['Protected changes', 'Fresh-state checks, revisions and persisted verification protect every write.'],
] as const;

export function HomePage({ config, onNavigate }: Props) {
  const ready = config.allReady;

  return (
    <Stack gap="xl">
      <Paper className="elz-hero" radius="xl" p={{ base: 'xl', md: 36 }}>
        <Stack gap="md" maw={760}>
          <div className={ready ? 'elz-status-pill elz-status-pill--ready' : 'elz-status-pill'}>
            <span className="elz-status-dot" />
            {ready ? 'WordPress is ready' : 'Setup needs attention'}
          </div>
          <Title order={1} className="elz-hero-title">
            {ready ? 'Your site is ready for Elementize.' : 'Finish setup, then edit your site from ChatGPT.'}
          </Title>
          <Text className="elz-hero-copy" size="lg">
            Elementize keeps layout and design inside Elementor while ChatGPT safely handles recognized content,
            images and Pixfort icons.
          </Text>
          <Group gap="sm" mt="xs">
            <Button className="elz-primary-button" size="md" onClick={() => onNavigate('setup')}>
              {ready ? 'Connect ChatGPT' : 'Continue setup'}
            </Button>
            <Button
              component="a"
              href={config.urls.gptBuilder}
              target="_blank"
              rel="noreferrer"
              className="elz-secondary-button"
              size="md"
            >
              Open GPT Builder
            </Button>
          </Group>
        </Stack>
      </Paper>

      <section>
        <Stack gap="xs" mb="md">
          <Text className="elz-eyebrow">What you can do</Text>
          <Title order={2}>Simple controls, guarded underneath.</Title>
        </Stack>
        <SimpleGrid cols={{ base: 1, sm: 2 }} spacing="md">
          {capabilities.map(([title, body], index) => (
            <Paper key={title} className="elz-card" radius="lg" p="xl">
              <div className="elz-card-icon">{index + 1}</div>
              <Title order={3} mt="md" mb={8}>
                {title}
              </Title>
              <Text className="elz-muted-copy">{body}</Text>
            </Paper>
          ))}
        </SimpleGrid>
      </section>

      <Paper className="elz-trust-card" radius="lg" p="xl">
        <div>
          <Text className="elz-eyebrow">Built for trust</Text>
          <Title order={3} mt={4}>Elementize does not become your page builder.</Title>
          <Text className="elz-muted-copy" mt={8} maw={760}>
            Layout, spacing, responsive design, typography and visual styling stay in Elementor. Elementize only
            exposes the guarded content surface you intentionally enabled.
          </Text>
        </div>
      </Paper>
    </Stack>
  );
}
