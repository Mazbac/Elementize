import {
  Alert,
  Box,
  Button,
  Card,
  Code,
  CopyButton,
  Grid,
  Group,
  Paper,
  ScrollArea,
  SimpleGrid,
  Stack,
  Stepper,
  Text,
  ThemeIcon,
  Title,
} from '@mantine/core';
import { useMemo, useState, type ReactNode } from 'react';
import { generateConnectionKey } from '../lib/actions';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = {
  config: ElementizeAdminConfig;
  onNavigate: (page: PageKey) => void;
};

function RequirementCard({ label, ready, detail }: { label: string; ready: boolean; detail: string }) {
  return (
    <Card bg="brand.0" radius="lg" p="lg" withBorder>
      <Group align="flex-start" wrap="nowrap">
        <ThemeIcon color="brand" variant="filled" radius="xl" size="lg">
          {ready ? '✓' : '!'}
        </ThemeIcon>
        <Box>
          <Text fw={700} c="gray.9">{label}</Text>
          <Text size="sm" c="gray.5" mt={4}>{detail}</Text>
        </Box>
      </Group>
    </Card>
  );
}

function SetupAction({
  number,
  title,
  description,
  action,
}: {
  number: number;
  title: string;
  description: string;
  action: ReactNode;
}) {
  return (
    <Card bg="brand.0" radius="lg" p="lg" withBorder>
      <Grid align="center" gutter="md">
        <Grid.Col span={{ base: 12, sm: 1 }}>
          <ThemeIcon color="brand" variant="filled" radius="lg" size="lg">{number}</ThemeIcon>
        </Grid.Col>
        <Grid.Col span={{ base: 12, sm: 7 }}>
          <Text fw={800} c="gray.9">{title}</Text>
          <Text size="sm" c="gray.5" mt={4}>{description}</Text>
        </Grid.Col>
        <Grid.Col span={{ base: 12, sm: 4 }}>
          <Group justify="flex-end">{action}</Group>
        </Grid.Col>
      </Grid>
    </Card>
  );
}

export function SetupPage({ config, onNavigate }: Props) {
  const websiteReady =
    config.requirements.elementor &&
    config.requirements.pixfort &&
    config.requirements.revisions &&
    config.requirements.applicationPasswords &&
    config.environment.connectionReady &&
    config.materialsReady;

  const [active, setActive] = useState(websiteReady ? 1 : 0);
  const [connectionKey, setConnectionKey] = useState('');
  const [keyBusy, setKeyBusy] = useState(false);
  const [keyError, setKeyError] = useState('');

  const requirements = useMemo(
    () => [
      ['Elementor', config.requirements.elementor, config.requirements.elementorDetail],
      ['Pixfort Core', config.requirements.pixfort, config.requirements.pixfortDetail],
      ['Safe revisions', config.requirements.revisions, config.requirements.revisions ? 'Enabled' : 'Enable page revisions first'],
      ['Secure login', config.requirements.applicationPasswords, config.requirements.applicationPasswords ? 'Available' : 'Application Passwords are unavailable'],
      ['Secure website address', config.environment.connectionReady, config.environment.connectionReady ? config.environment.effectiveOrigin : 'Needs a public HTTPS address'],
      ['GPT setup materials', config.materialsReady, config.materialsReady ? 'Available' : 'Plugin setup files could not be loaded'],
    ] as const,
    [config]
  );

  async function handleGenerateKey() {
    setKeyBusy(true);
    setKeyError('');
    try {
      setConnectionKey(await generateConnectionKey(config));
    } catch (error) {
      setKeyError(error instanceof Error ? error.message : 'Could not generate the connection key.');
    } finally {
      setKeyBusy(false);
    }
  }

  return (
    <Stack gap="xl">
      <Box>
        <Text size="xs" fw={800} tt="uppercase" c="brand.6">Setup</Text>
        <Title order={1} c="gray.9">Connect Elementize in three clear steps.</Title>
        <Text c="gray.5" mt="xs" maw={720} lh={1.6}>
          The technical pieces stay underneath. You only need to check your website, connect the GPT once and run one test.
        </Text>
      </Box>

      <Paper bg="brand.0" radius="xl" p={{ base: 'lg', md: 'xl' }} withBorder>
        <Stepper active={active} onStepClick={setActive} allowNextStepsSelect={websiteReady} color="brand">
          <Stepper.Step label="Website" description="Check compatibility">
            <Stack gap="lg" mt="xl">
              <SimpleGrid cols={{ base: 1, sm: 2, lg: 3 }} spacing="md">
                {requirements.map(([label, ready, detail]) => (
                  <RequirementCard key={label} label={label} ready={ready} detail={detail} />
                ))}
              </SimpleGrid>

              <Alert color="brand" variant="filled" radius="lg" title={websiteReady ? 'Your WordPress site is ready.' : 'Setup needs attention.'}>
                {websiteReady
                  ? 'Continue to connect your Custom GPT.'
                  : 'Resolve the checks above before connecting ChatGPT.'}
              </Alert>

              <Group justify="flex-end">
                {!config.environment.connectionReady && (
                  <Button variant="default" onClick={() => onNavigate('connection')}>Set secure address</Button>
                )}
                <Button color="brand" variant="filled" disabled={!websiteReady} onClick={() => setActive(1)}>
                  Continue to ChatGPT
                </Button>
              </Group>
            </Stack>
          </Stepper.Step>

          <Stepper.Step label="ChatGPT" description="Connect once">
            <Stack gap="md" mt="xl">
              <SetupAction
                number={1}
                title="Open your Custom GPT"
                description="Open GPT Builder in a new tab and keep this page open beside it."
                action={
                  <Button component="a" href={config.urls.gptBuilder} target="_blank" rel="noreferrer" variant="default">
                    Open GPT Builder
                  </Button>
                }
              />

              <SetupAction
                number={2}
                title="Add Elementize instructions"
                description="Paste these into the GPT Instructions field."
                action={
                  <CopyButton value={config.instructions} timeout={1500}>
                    {({ copied, copy }) => (
                      <Button color="brand" variant="filled" disabled={!config.instructions} onClick={copy}>
                        {copied ? 'Copied' : 'Copy instructions'}
                      </Button>
                    )}
                  </CopyButton>
                }
              />

              <SetupAction
                number={3}
                title="Add one Elementize Action"
                description="Create one Action in GPT Builder and paste this schema."
                action={
                  <CopyButton value={config.schema} timeout={1500}>
                    {({ copied, copy }) => (
                      <Button color="brand" variant="filled" disabled={!config.schema} onClick={copy}>
                        {copied ? 'Copied' : 'Copy Action schema'}
                      </Button>
                    )}
                  </CopyButton>
                }
              />

              <SetupAction
                number={4}
                title="Connect securely"
                description="In Action authentication choose API Key → Basic, then paste the one-time connection key."
                action={
                  <Button color="brand" variant="filled" loading={keyBusy} onClick={handleGenerateKey}>
                    Generate connection key
                  </Button>
                }
              />

              {connectionKey && (
                <Paper bg="brand.1" radius="lg" p="lg" withBorder>
                  <Stack gap="sm">
                    <Box>
                      <Text fw={800} c="gray.9">Connection key</Text>
                      <Text size="sm" c="gray.5" mt={4}>Shown once. Treat it like a password and paste it directly into GPT Builder.</Text>
                    </Box>
                    <ScrollArea type="auto">
                      <Code block bg="gray.8" c="brand.0">{connectionKey}</Code>
                    </ScrollArea>
                    <CopyButton value={connectionKey} timeout={1500}>
                      {({ copied, copy }) => (
                        <Button variant="default" onClick={copy} w="fit-content">
                          {copied ? 'Copied' : 'Copy connection key'}
                        </Button>
                      )}
                    </CopyButton>
                  </Stack>
                </Paper>
              )}

              {keyError && (
                <Alert color="brand" variant="filled" radius="lg" title="Could not generate the key">
                  {keyError}
                </Alert>
              )}

              <Group justify="space-between" mt="sm">
                <Button variant="default" onClick={() => setActive(0)}>Back</Button>
                <Button color="brand" variant="filled" onClick={() => setActive(2)}>Continue to test</Button>
              </Group>
            </Stack>
          </Stepper.Step>

          <Stepper.Step label="Test" description="Confirm it works">
            <Stack gap="lg" mt="xl">
              <Paper bg="brand.0" radius="lg" p="xl" withBorder>
                <Stack gap="md">
                  <Box>
                    <Text size="xs" fw={800} tt="uppercase" c="brand.6">Final check</Text>
                    <Title order={3} c="gray.9">Send one test message to your GPT.</Title>
                    <Text c="gray.5" mt="xs" maw={680} lh={1.6}>
                      Elementize cannot inspect your Custom GPT configuration directly. This message confirms the Action can reach WordPress.
                    </Text>
                  </Box>
                  <ScrollArea type="auto">
                    <Code block bg="gray.8" c="brand.0">{config.testPrompt}</Code>
                  </ScrollArea>
                  <Group gap="sm">
                    <CopyButton value={config.testPrompt} timeout={1500}>
                      {({ copied, copy }) => (
                        <Button color="brand" variant="filled" onClick={copy}>
                          {copied ? 'Copied' : 'Copy test message'}
                        </Button>
                      )}
                    </CopyButton>
                    <Button component="a" href={config.urls.gptBuilder} target="_blank" rel="noreferrer" variant="default">
                      Open GPT Builder
                    </Button>
                  </Group>
                </Stack>
              </Paper>

              <Group justify="space-between">
                <Button variant="default" onClick={() => setActive(1)}>Back</Button>
                <Button variant="default" onClick={() => onNavigate('home')}>Return home</Button>
              </Group>
            </Stack>
          </Stepper.Step>
        </Stepper>
      </Paper>
    </Stack>
  );
}
