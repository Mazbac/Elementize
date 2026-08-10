import {
  Alert,
  Badge,
  Box,
  Divider,
  Grid,
  Group,
  NavLink,
  Paper,
  SegmentedControl,
  Stack,
  Text,
  ThemeIcon,
} from '@mantine/core';
import { useState } from 'react';
import { ConnectionPage } from '../pages/ConnectionPage';
import { HomePage } from '../pages/HomePage';
import { SettingsPage } from '../pages/SettingsPage';
import { SetupPage } from '../pages/SetupPage';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = { config: ElementizeAdminConfig };

const navItems: Array<{ key: PageKey; label: string; short: string }> = [
  { key: 'home', label: 'Home', short: 'H' },
  { key: 'setup', label: 'Setup', short: 'S' },
  { key: 'connection', label: 'Connection', short: 'C' },
  { key: 'settings', label: 'Settings', short: 'S' },
];

function Notice({ notice }: { notice: string }) {
  const messages: Record<string, string> = {
    connection_saved: 'Secure website address saved.',
    connection_cleared: 'Address override cleared. Elementize will use the automatic site address when possible.',
    connection_invalid: 'That address could not be saved. Use a public HTTPS origin without a path, query or credentials.',
  };

  const message = messages[notice];
  if (!message) return null;

  return (
    <Alert color="brand" variant="filled" radius="lg" title="Elementize">
      {message}
    </Alert>
  );
}

export function App({ config }: Props) {
  const [page, setPage] = useState<PageKey>(config.allReady ? 'home' : 'setup');

  return (
    <Box bg="brand.0" mih="calc(100vh - 32px)" p={{ base: 'sm', md: 'lg' }}>
      <Grid gap="lg">
        <Grid.Col span={{ base: 12, md: 3, lg: 2 }} visibleFrom="md">
          <Paper bg="gray.9" c="brand.0" radius="xl" p="lg" mih="calc(100vh - 68px)" withBorder>
            <Stack h="100%" gap="xl">
              <Group gap="sm">
                <ThemeIcon color="brand" variant="filled" radius="lg" size="lg">E</ThemeIcon>
                <Box>
                  <Text fw={800} c="brand.0">Elementize</Text>
                  <Text size="xs" c="gray.3">for WordPress</Text>
                </Box>
              </Group>

              <Stack gap="xs">
                {navItems.map((item) => (
                  <NavLink
                    key={item.key}
                    active={page === item.key}
                    color="brand"
                    variant="filled"
                    label={item.label}
                    c="brand.0"
                    leftSection={
                      <ThemeIcon color="brand" variant="filled" radius="md" size="sm">
                        {item.short}
                      </ThemeIcon>
                    }
                    onClick={() => setPage(item.key)}
                  />
                ))}
              </Stack>

              <Paper bg="gray.8" c="brand.0" radius="lg" p="md" withBorder mt="auto">
                <Group gap="sm" align="flex-start">
                  <ThemeIcon color="brand" variant="filled" radius="xl" size="sm">
                    {config.allReady ? '✓' : '!'}
                  </ThemeIcon>
                  <Box>
                    <Text fw={800} size="sm" c="brand.0">
                      {config.allReady ? 'WordPress ready' : 'Setup incomplete'}
                    </Text>
                    <Text size="xs" c="gray.3">v{config.version}</Text>
                  </Box>
                </Group>
              </Paper>
            </Stack>
          </Paper>
        </Grid.Col>

        <Grid.Col span={{ base: 12, md: 9, lg: 10 }}>
          <Stack gap="lg">
            <Paper hiddenFrom="md" radius="xl" p="md" bg="gray.9" c="brand.0" withBorder>
              <Stack gap="md">
                <Group justify="space-between">
                  <Group gap="sm">
                    <ThemeIcon color="brand" variant="filled" radius="lg">E</ThemeIcon>
                    <Text fw={800} c="brand.0">Elementize</Text>
                  </Group>
                  <Badge color="brand" variant="filled">
                    {config.allReady ? 'Ready' : 'Setup'}
                  </Badge>
                </Group>
                <SegmentedControl
                  fullWidth
                  color="brand"
                  value={page}
                  onChange={(value) => setPage(value as PageKey)}
                  data={navItems.map((item) => ({ label: item.label, value: item.key }))}
                />
              </Stack>
            </Paper>

            <Paper radius="xl" p={{ base: 'md', sm: 'lg' }} bg="brand.0" withBorder>
              <Group justify="space-between" align="flex-start" gap="lg">
                <Box>
                  <Text size="xs" fw={800} tt="uppercase" c="gray.5">Elementize</Text>
                  <Text fw={800} c="gray.9">Guarded editing, made simple.</Text>
                </Box>
                <Badge color="brand" variant="filled" radius="xl" size="lg">
                  {config.allReady ? 'Ready' : 'Needs setup'}
                </Badge>
              </Group>
            </Paper>

            <Notice notice={config.notice} />

            <Box>
              {page === 'home' && <HomePage config={config} onNavigate={setPage} />}
              {page === 'setup' && <SetupPage config={config} onNavigate={setPage} />}
              {page === 'connection' && <ConnectionPage config={config} />}
              {page === 'settings' && <SettingsPage config={config} />}
            </Box>

            <Box py="md">
              <Divider color="gray.3" mb="md" />
              <Stack gap={2}>
                <Text size="sm" fw={700} c="gray.8">Elementize keeps layout and visual design in Elementor.</Text>
                <Text size="xs" c="gray.5">Content writes remain guarded by fresh state, revisions and persisted verification.</Text>
              </Stack>
            </Box>
          </Stack>
        </Grid.Col>
      </Grid>
    </Box>
  );
}
