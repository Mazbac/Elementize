import {
  Box,
  Card,
  Paper,
  SimpleGrid,
  Stack,
  Table,
  Text,
  ThemeIcon,
  Title,
} from '@mantine/core';
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
      <Box>
        <Text size="xs" fw={800} tt="uppercase" c="brand.6">Settings</Text>
        <Title order={1} c="gray.9">Safety first, without the clutter.</Title>
        <Text c="gray.5" mt="xs" maw={700} lh={1.6}>
          Elementize keeps advanced safeguards on by design, so there is very little you need to configure here.
        </Text>
      </Box>

      <Stack gap="md">
        <Text size="xs" fw={800} tt="uppercase" c="brand.6">Safety model</Text>
        <SimpleGrid cols={{ base: 1, sm: 2 }} spacing="md">
          {safety.map(([title, body]) => (
            <Card key={title} bg="brand.0" radius="lg" p="xl" withBorder>
              <ThemeIcon color="brand" variant="filled" radius="xl" size="lg">✓</ThemeIcon>
              <Title order={3} mt="md" c="gray.9">{title}</Title>
              <Text c="gray.5" mt="xs" lh={1.6}>{body}</Text>
            </Card>
          ))}
        </SimpleGrid>
      </Stack>

      <Paper bg="brand.0" radius="lg" p="xl" withBorder>
        <Stack gap="lg">
          <Title order={3} c="gray.9">Environment</Title>
          <Table withRowBorders>
            <Table.Tbody>
              <Table.Tr>
                <Table.Td c="gray.5">Elementize</Table.Td>
                <Table.Td><Text fw={700} c="gray.9">v{config.version}</Text></Table.Td>
              </Table.Tr>
              <Table.Tr>
                <Table.Td c="gray.5">Elementor</Table.Td>
                <Table.Td><Text fw={700} c="gray.9">{config.requirements.elementorDetail}</Text></Table.Td>
              </Table.Tr>
              <Table.Tr>
                <Table.Td c="gray.5">Pixfort</Table.Td>
                <Table.Td><Text fw={700} c="gray.9">{config.requirements.pixfortDetail}</Text></Table.Td>
              </Table.Tr>
              <Table.Tr>
                <Table.Td c="gray.5">WordPress revisions</Table.Td>
                <Table.Td><Text fw={700} c="gray.9">{config.requirements.revisions ? 'Enabled' : 'Needs attention'}</Text></Table.Td>
              </Table.Tr>
            </Table.Tbody>
          </Table>
        </Stack>
      </Paper>
    </Stack>
  );
}
