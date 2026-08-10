import {
  Accordion,
  Badge,
  Box,
  Button,
  Code,
  Group,
  Input,
  Paper,
  ScrollArea,
  Stack,
  Table,
  Text,
  TextInput,
  Title,
} from '@mantine/core';
import type { ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

export function ConnectionPage({ config }: Props) {
  const ready = config.environment.connectionReady;

  return (
    <Stack gap="xl">
      <Box>
        <Text size="xs" fw={800} tt="uppercase" c="brand.6">Connection</Text>
        <Title order={1} c="gray.9">Keep the connection simple.</Title>
        <Text c="gray.5" mt="xs" maw={700} lh={1.6}>
          Most users never need to change this after setup. Elementize only needs a public HTTPS address that ChatGPT can reach.
        </Text>
      </Box>

      <Paper bg="brand.1" radius="xl" p="xl" withBorder>
        <Stack gap="md">
          <Badge color="brand" variant="filled" radius="xl" size="lg" w="fit-content">
            {ready ? 'Secure address ready' : 'Secure address required'}
          </Badge>
          <Title order={2} c="gray.9">
            {ready ? config.environment.effectiveOrigin : 'Connect this WordPress site to the public web.'}
          </Title>
          <Text c="gray.5" maw={720} lh={1.6}>
            {ready
              ? 'This is the address Elementize places into the Action schema for your Custom GPT.'
              : 'For local WordPress sites, use your HTTPS tunnel address. Production sites can usually use their normal HTTPS domain.'}
          </Text>
          {ready && (
            <ScrollArea type="auto">
              <Code block bg="gray.8" c="brand.0">{config.environment.effectiveOrigin}</Code>
            </ScrollArea>
          )}
        </Stack>
      </Paper>

      <Paper bg="brand.0" radius="lg" p="xl" withBorder>
        <Stack gap="lg">
          <Box>
            <Title order={3} c="gray.9">Secure website address</Title>
            <Text c="gray.5" size="sm" mt={6}>
              Paste only the HTTPS origin. Do not add /wp-json or another path.
            </Text>
          </Box>

          <Box component="form" method="post" action={config.urls.adminPost}>
            <Input type="hidden" name="action" value="elementize_save_connection" />
            <Input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
            <Stack gap="md">
              <TextInput
                name="elementize_public_api_origin"
                type="url"
                defaultValue={config.environment.storedPublic}
                placeholder={config.environment.effectiveOrigin || 'https://your-secure-address.example'}
                label="Public HTTPS address"
                description="Use the website origin only."
                size="md"
              />
              <Group>
                <Button type="submit" color="brand" variant="filled">Save address</Button>
              </Group>
            </Stack>
          </Box>

          {config.environment.storedPublic && (
            <Box component="form" method="post" action={config.urls.adminPost}>
              <Input type="hidden" name="action" value="elementize_save_connection" />
              <Input type="hidden" name="elementize_clear_public_origin" value="1" />
              <Input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
              <Button type="submit" variant="default">Use automatic site address instead</Button>
            </Box>
          )}
        </Stack>
      </Paper>

      <Accordion variant="contained" radius="lg">
        <Accordion.Item value="technical">
          <Accordion.Control>
            <Box>
              <Text fw={800} c="gray.9">Technical details</Text>
              <Text size="sm" c="gray.5">Only open this if you are troubleshooting.</Text>
            </Box>
          </Accordion.Control>
          <Accordion.Panel>
            <Table withRowBorders>
              <Table.Tbody>
                <Table.Tr>
                  <Table.Td c="gray.5">WordPress site</Table.Td>
                  <Table.Td><Text fw={700} c="gray.9">{config.environment.siteOrigin || 'Unavailable'}</Text></Table.Td>
                </Table.Tr>
                <Table.Tr>
                  <Table.Td c="gray.5">Public HTTPS detected</Table.Td>
                  <Table.Td><Text fw={700} c="gray.9">{config.environment.sitePublicHttps ? 'Yes' : 'No'}</Text></Table.Td>
                </Table.Tr>
                <Table.Tr>
                  <Table.Td c="gray.5">Stored override</Table.Td>
                  <Table.Td><Text fw={700} c="gray.9">{config.environment.storedPublic || 'None'}</Text></Table.Td>
                </Table.Tr>
              </Table.Tbody>
            </Table>
          </Accordion.Panel>
        </Accordion.Item>
      </Accordion>
    </Stack>
  );
}
