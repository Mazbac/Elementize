import {
  Badge,
  Box,
  Button,
  Card,
  Group,
  Paper,
  SimpleGrid,
  Stack,
  Text,
  ThemeIcon,
  Title,
} from '@mantine/core';
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
      <Paper bg="brand.1" radius="xl" p={{ base: 'xl', md: 36 }} withBorder>
        <Stack gap="md" maw={760}>
          <Badge color="brand" variant="filled" radius="xl" size="lg" w="fit-content">
            {ready ? 'WordPress is ready' : 'Setup needs attention'}
          </Badge>
          <Title order={1} c="gray.9" fz={{ base: 34, sm: 44, md: 52 }} lh={1.05}>
            {ready ? 'Your site is ready for Elementize.' : 'Finish setup, then edit your site from ChatGPT.'}
          </Title>
          <Text c="gray.8" size="lg" lh={1.6}>
            Elementize keeps layout and design inside Elementor while ChatGPT safely handles recognized content,
            images and Pixfort icons.
          </Text>
          <Group gap="sm" mt="xs">
            <Button color="brand" variant="filled" size="md" onClick={() => onNavigate('setup')}>
              {ready ? 'Connect ChatGPT' : 'Continue setup'}
            </Button>
            <Button
              component="a"
              href={config.urls.gptBuilder}
              target="_blank"
              rel="noreferrer"
              variant="default"
              size="md"
            >
              Open GPT Builder
            </Button>
          </Group>
        </Stack>
      </Paper>

      <Stack gap="md">
        <Box>
          <Text size="xs" fw={800} tt="uppercase" c="brand.6">What you can do</Text>
          <Title order={2} c="gray.9">Simple controls, guarded underneath.</Title>
        </Box>

        <SimpleGrid cols={{ base: 1, sm: 2 }} spacing="md">
          {capabilities.map(([title, body], index) => (
            <Card key={title} bg="brand.0" radius="lg" p="xl" withBorder>
              <ThemeIcon color="brand" variant="filled" radius="lg" size="lg">
                {index + 1}
              </ThemeIcon>
              <Title order={3} mt="md" c="gray.9">{title}</Title>
              <Text c="gray.5" mt="xs" lh={1.6}>{body}</Text>
            </Card>
          ))}
        </SimpleGrid>
      </Stack>

      <Paper bg="gray.8" c="brand.0" radius="lg" p="xl" withBorder>
        <Stack gap="xs" maw={800}>
          <Text size="xs" fw={800} tt="uppercase" c="brand.1">Built for trust</Text>
          <Title order={3} c="brand.0">Elementize does not become your page builder.</Title>
          <Text c="gray.3" lh={1.6}>
            Layout, spacing, responsive design, typography and visual styling stay in Elementor. Elementize only
            exposes the guarded content surface you intentionally enabled.
          </Text>
        </Stack>
      </Paper>
    </Stack>
  );
}
