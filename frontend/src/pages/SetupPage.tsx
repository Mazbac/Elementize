import { Button, Group, Paper, SimpleGrid, Stack, Stepper, Text, Title } from '@mantine/core';
import { useMemo, useState } from 'react';
import { copyText, generateConnectionKey } from '../lib/actions';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = {
  config: ElementizeAdminConfig;
  onNavigate: (page: PageKey) => void;
};

type CopyState = 'idle' | 'copied';

function RequirementCard({ label, ready, detail }: { label: string; ready: boolean; detail: string }) {
  return (
    <Paper className="elz-mini-card" radius="lg" p="lg">
      <div className={ready ? 'elz-checkmark elz-checkmark--ready' : 'elz-checkmark'}>{ready ? '✓' : '!'}</div>
      <Text fw={700} mt="sm">{label}</Text>
      <Text className="elz-muted-copy" size="sm" mt={4}>{detail}</Text>
    </Paper>
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
  const [instructionsState, setInstructionsState] = useState<CopyState>('idle');
  const [schemaState, setSchemaState] = useState<CopyState>('idle');
  const [testState, setTestState] = useState<CopyState>('idle');
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
    ] as const,
    [config]
  );

  async function handleCopy(value: string, setter: (state: CopyState) => void) {
    await copyText(value);
    setter('copied');
    window.setTimeout(() => setter('idle'), 1500);
  }

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
      <div>
        <Text className="elz-eyebrow">Setup</Text>
        <Title order={1}>Connect Elementize in three clear steps.</Title>
        <Text className="elz-muted-copy" mt={8} maw={720}>
          The technical pieces stay underneath. You only need to check your website, connect the GPT once and run one test.
        </Text>
      </div>

      <Paper className="elz-card" radius="xl" p={{ base: 'lg', md: 'xl' }}>
        <Stepper active={active} onStepClick={setActive} className="elz-stepper" allowNextStepsSelect={websiteReady}>
          <Stepper.Step label="Website" description="Check compatibility">
            <Stack gap="lg" mt="xl">
              <SimpleGrid cols={{ base: 1, sm: 2, lg: 3 }} spacing="md">
                {requirements.map(([label, ready, detail]) => (
                  <RequirementCard key={label} label={label} ready={ready} detail={detail} />
                ))}
              </SimpleGrid>

              {websiteReady ? (
                <Paper className="elz-inline-success" radius="lg" p="lg">
                  <Text fw={800}>Your WordPress site is ready.</Text>
                  <Text size="sm" mt={4}>Continue to connect your Custom GPT.</Text>
                </Paper>
              ) : (
                <Paper className="elz-inline-attention" radius="lg" p="lg">
                  <Text fw={800}>One or more checks need attention.</Text>
                  <Text size="sm" mt={4}>Fix the item above before connecting ChatGPT.</Text>
                </Paper>
              )}

              <Group justify="flex-end">
                {!config.environment.connectionReady && (
                  <Button className="elz-secondary-button" onClick={() => onNavigate('connection')}>
                    Set secure address
                  </Button>
                )}
                <Button className="elz-primary-button" disabled={!websiteReady} onClick={() => setActive(1)}>
                  Continue to ChatGPT
                </Button>
              </Group>
            </Stack>
          </Stepper.Step>

          <Stepper.Step label="ChatGPT" description="Connect once">
            <Stack gap="md" mt="xl">
              <Paper className="elz-setup-action" radius="lg" p="lg">
                <div className="elz-step-number">1</div>
                <div className="elz-setup-action__body">
                  <Text fw={800}>Open your Custom GPT</Text>
                  <Text className="elz-muted-copy" size="sm" mt={4}>Open GPT Builder in a new tab and keep this page open beside it.</Text>
                </div>
                <Button component="a" href={config.urls.gptBuilder} target="_blank" rel="noreferrer" className="elz-secondary-button">
                  Open GPT Builder
                </Button>
              </Paper>

              <Paper className="elz-setup-action" radius="lg" p="lg">
                <div className="elz-step-number">2</div>
                <div className="elz-setup-action__body">
                  <Text fw={800}>Add Elementize instructions</Text>
                  <Text className="elz-muted-copy" size="sm" mt={4}>Paste these into the GPT Instructions field.</Text>
                </div>
                <Button className="elz-primary-button" disabled={!config.instructions} onClick={() => handleCopy(config.instructions, setInstructionsState)}>
                  {instructionsState === 'copied' ? 'Copied' : 'Copy instructions'}
                </Button>
              </Paper>

              <Paper className="elz-setup-action" radius="lg" p="lg">
                <div className="elz-step-number">3</div>
                <div className="elz-setup-action__body">
                  <Text fw={800}>Add one Elementize Action</Text>
                  <Text className="elz-muted-copy" size="sm" mt={4}>Create one Action in GPT Builder and paste this schema.</Text>
                </div>
                <Button className="elz-primary-button" disabled={!config.schema} onClick={() => handleCopy(config.schema, setSchemaState)}>
                  {schemaState === 'copied' ? 'Copied' : 'Copy Action schema'}
                </Button>
              </Paper>

              <Paper className="elz-setup-action" radius="lg" p="lg">
                <div className="elz-step-number">4</div>
                <div className="elz-setup-action__body">
                  <Text fw={800}>Connect securely</Text>
                  <Text className="elz-muted-copy" size="sm" mt={4}>In the Action authentication settings choose API Key → Basic, then paste this one-time key.</Text>
                </div>
                <Button className="elz-primary-button" loading={keyBusy} onClick={handleGenerateKey}>
                  Generate connection key
                </Button>
              </Paper>

              {connectionKey && (
                <Paper className="elz-secret-card" radius="lg" p="lg">
                  <Text fw={800}>Connection key</Text>
                  <Text className="elz-muted-copy" size="sm" mt={4}>Shown once. Treat it like a password and paste it directly into GPT Builder.</Text>
                  <div className="elz-code-block" role="textbox" aria-label="Connection key">{connectionKey}</div>
                  <Button className="elz-secondary-button" mt="md" onClick={() => copyText(connectionKey)}>Copy connection key</Button>
                </Paper>
              )}

              {keyError && (
                <Paper className="elz-inline-attention" radius="lg" p="lg">
                  <Text fw={800}>Could not generate the key.</Text>
                  <Text size="sm" mt={4}>{keyError}</Text>
                </Paper>
              )}

              <Group justify="space-between" mt="sm">
                <Button className="elz-quiet-button" onClick={() => setActive(0)}>Back</Button>
                <Button className="elz-primary-button" onClick={() => setActive(2)}>Continue to test</Button>
              </Group>
            </Stack>
          </Stepper.Step>

          <Stepper.Step label="Test" description="Confirm it works">
            <Stack gap="lg" mt="xl">
              <Paper className="elz-test-card" radius="lg" p="xl">
                <Text className="elz-eyebrow">Final check</Text>
                <Title order={3} mt={4}>Send one test message to your GPT.</Title>
                <Text className="elz-muted-copy" mt={8} maw={680}>
                  Elementize cannot inspect your Custom GPT configuration directly. This message confirms the Action can reach WordPress.
                </Text>
                <div className="elz-code-block">{config.testPrompt}</div>
                <Group gap="sm" mt="md">
                  <Button className="elz-primary-button" onClick={() => handleCopy(config.testPrompt, setTestState)}>
                    {testState === 'copied' ? 'Copied' : 'Copy test message'}
                  </Button>
                  <Button component="a" href={config.urls.gptBuilder} target="_blank" rel="noreferrer" className="elz-secondary-button">
                    Open GPT Builder
                  </Button>
                </Group>
              </Paper>
              <Group justify="space-between">
                <Button className="elz-quiet-button" onClick={() => setActive(1)}>Back</Button>
                <Button className="elz-secondary-button" onClick={() => onNavigate('home')}>Return home</Button>
              </Group>
            </Stack>
          </Stepper.Step>
        </Stepper>
      </Paper>
    </Stack>
  );
}
