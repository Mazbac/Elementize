import { Check, Copy, ExternalLink, KeyRound } from 'lucide-react';
import { useMemo, useState, type ReactNode } from 'react';
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Progress } from '@/components/ui/progress';
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs';
import { Textarea } from '@/components/ui/textarea';
import { copyText, generateConnectionKey } from '../lib/actions';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = {
  config: ElementizeAdminConfig;
  onNavigate: (page: PageKey) => void;
};

type StepKey = 'website' | 'chatgpt' | 'test';

function RequirementCard({ label, ready, detail }: { label: string; ready: boolean; detail: string }) {
  return (
    <Card>
      <CardContent className="flex items-start gap-3 pt-5">
        <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-md bg-secondary text-primary">
          {ready ? <Check className="h-4 w-4" /> : <span className="text-xs font-semibold">!</span>}
        </div>
        <div className="min-w-0">
          <div className="flex flex-wrap items-center gap-2">
            <p className="font-medium">{label}</p>
            <Badge variant={ready ? 'outline' : 'secondary'}>{ready ? 'Ready' : 'Needs attention'}</Badge>
          </div>
          <p className="mt-1 break-words text-sm text-muted-foreground">{detail}</p>
        </div>
      </CardContent>
    </Card>
  );
}

function SetupAction({ number, title, description, action }: { number: number; title: string; description: string; action: ReactNode }) {
  return (
    <Card>
      <CardContent className="flex flex-col gap-3 pt-5 sm:flex-row sm:items-center">
        <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-md bg-primary text-xs font-semibold text-primary-foreground">{number}</div>
        <div className="min-w-0 flex-1">
          <p className="font-medium">{title}</p>
          <p className="mt-1 text-sm text-muted-foreground">{description}</p>
        </div>
        <div className="shrink-0">{action}</div>
      </CardContent>
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

  const [step, setStep] = useState<StepKey>(websiteReady ? 'chatgpt' : 'website');
  const [connectionKey, setConnectionKey] = useState('');
  const [keyBusy, setKeyBusy] = useState(false);
  const [keyError, setKeyError] = useState('');
  const [copied, setCopied] = useState('');

  const requirements = useMemo(
    () => [
      ['Elementor', config.requirements.elementor, config.requirements.elementorDetail],
      ['Pixfort Core', config.requirements.pixfort, config.requirements.pixfortDetail],
      ['WordPress revisions', config.requirements.revisions, config.requirements.revisions ? 'Enabled' : 'Enable revisions before editing'],
      ['Secure login', config.requirements.applicationPasswords, config.requirements.applicationPasswords ? 'Application Passwords available' : 'Application Passwords unavailable'],
      ['Public HTTPS address', config.environment.connectionReady, config.environment.connectionReady ? config.environment.effectiveOrigin : 'A public HTTPS address is required'],
      ['GPT setup materials', config.materialsReady, config.materialsReady ? 'Instructions and Action schema available' : 'Setup files could not be loaded'],
    ] as const,
    [config]
  );

  async function handleCopy(value: string, key: string) {
    await copyText(value);
    setCopied(key);
    window.setTimeout(() => setCopied(''), 1500);
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

  const progress = step === 'website' ? 33 : step === 'chatgpt' ? 66 : 100;

  return (
    <div className="space-y-5">
      <div>
        <h2 className="text-lg font-semibold">Setup</h2>
        <p className="mt-1 max-w-2xl text-sm text-muted-foreground">
          Check WordPress, connect your Custom GPT once, then confirm the connection.
        </p>
      </div>

      <Card>
        <CardHeader className="pb-3">
          <div className="flex items-center justify-between gap-4">
            <div>
              <CardTitle>Connect Elementize</CardTitle>
              <CardDescription className="mt-1">Three short steps. Technical details stay out of the way.</CardDescription>
            </div>
            <Badge variant="outline">{progress}%</Badge>
          </div>
          <Progress value={progress} className="mt-3" />
        </CardHeader>
        <CardContent>
          <Tabs value={step} onValueChange={(value) => setStep(value as StepKey)}>
            <TabsList className="grid h-auto w-full grid-cols-3 bg-secondary/60">
              <TabsTrigger value="website">1. Website</TabsTrigger>
              <TabsTrigger value="chatgpt" disabled={!websiteReady}>2. ChatGPT</TabsTrigger>
              <TabsTrigger value="test" disabled={!websiteReady}>3. Test</TabsTrigger>
            </TabsList>

            <TabsContent value="website" className="space-y-4 pt-2">
              <div className="grid gap-3 md:grid-cols-2 lg:grid-cols-3">
                {requirements.map(([label, ready, detail]) => (
                  <RequirementCard key={label} label={label} ready={ready} detail={detail} />
                ))}
              </div>

              <Alert variant={websiteReady ? 'accent' : 'default'}>
                <AlertTitle>{websiteReady ? 'Your WordPress site is ready.' : 'Some checks still need attention.'}</AlertTitle>
                <AlertDescription>
                  {websiteReady ? 'Continue to connect your Custom GPT.' : 'Resolve the items above before continuing.'}
                </AlertDescription>
              </Alert>

              <div className="flex flex-wrap justify-end gap-2">
                {!config.environment.connectionReady && (
                  <Button variant="outline" onClick={() => onNavigate('connection')}>Set public address</Button>
                )}
                <Button disabled={!websiteReady} onClick={() => setStep('chatgpt')}>Continue</Button>
              </div>
            </TabsContent>

            <TabsContent value="chatgpt" className="space-y-3 pt-2">
              <SetupAction
                number={1}
                title="Open GPT Builder"
                description="Keep this WordPress page open beside your Custom GPT configuration."
                action={
                  <Button asChild variant="outline" size="sm">
                    <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink className="h-4 w-4" /></a>
                  </Button>
                }
              />
              <SetupAction
                number={2}
                title="Add Elementize instructions"
                description="Paste the Elementize instructions into the GPT Instructions field."
                action={<Button size="sm" disabled={!config.instructions} onClick={() => handleCopy(config.instructions, 'instructions')}>{copied === 'instructions' ? <Check className="h-4 w-4" /> : <Copy className="h-4 w-4" />}{copied === 'instructions' ? 'Copied' : 'Copy instructions'}</Button>}
              />
              <SetupAction
                number={3}
                title="Add one Action"
                description="Create one Action in GPT Builder and paste the Elementize schema."
                action={<Button size="sm" disabled={!config.schema} onClick={() => handleCopy(config.schema, 'schema')}>{copied === 'schema' ? <Check className="h-4 w-4" /> : <Copy className="h-4 w-4" />}{copied === 'schema' ? 'Copied' : 'Copy Action schema'}</Button>}
              />
              <SetupAction
                number={4}
                title="Connect securely"
                description="Choose API Key → Basic in Action authentication, then paste the one-time key."
                action={<Button size="sm" disabled={keyBusy} onClick={handleGenerateKey}><KeyRound className="h-4 w-4" />{keyBusy ? 'Generating…' : 'Generate key'}</Button>}
              />

              {connectionKey && (
                <Card>
                  <CardHeader className="pb-3">
                    <CardTitle className="text-sm">Connection key</CardTitle>
                    <CardDescription>Shown once. Treat it like a password and paste it directly into GPT Builder.</CardDescription>
                  </CardHeader>
                  <CardContent className="space-y-3">
                    <Textarea readOnly value={connectionKey} className="min-h-20 resize-none font-mono text-xs" />
                    <Button variant="outline" size="sm" onClick={() => handleCopy(connectionKey, 'key')}>
                      {copied === 'key' ? <Check className="h-4 w-4" /> : <Copy className="h-4 w-4" />}
                      {copied === 'key' ? 'Copied' : 'Copy connection key'}
                    </Button>
                  </CardContent>
                </Card>
              )}

              {keyError && (
                <Alert>
                  <AlertTitle>Could not generate the key</AlertTitle>
                  <AlertDescription>{keyError}</AlertDescription>
                </Alert>
              )}

              <div className="flex items-center justify-between gap-2 pt-1">
                <Button variant="outline" onClick={() => setStep('website')}>Back</Button>
                <Button onClick={() => setStep('test')}>Continue to test</Button>
              </div>
            </TabsContent>

            <TabsContent value="test" className="space-y-4 pt-2">
              <Card>
                <CardHeader>
                  <CardTitle>Send one test message</CardTitle>
                  <CardDescription>
                    Elementize cannot inspect your Custom GPT configuration directly. This confirms the Action can reach WordPress.
                  </CardDescription>
                </CardHeader>
                <CardContent className="space-y-3">
                  <Textarea readOnly value={config.testPrompt} className="min-h-28 resize-none font-mono text-xs" />
                  <div className="flex flex-wrap gap-2">
                    <Button onClick={() => handleCopy(config.testPrompt, 'test')}>
                      {copied === 'test' ? <Check className="h-4 w-4" /> : <Copy className="h-4 w-4" />}
                      {copied === 'test' ? 'Copied' : 'Copy test message'}
                    </Button>
                    <Button asChild variant="outline">
                      <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink className="h-4 w-4" /></a>
                    </Button>
                  </div>
                </CardContent>
              </Card>
              <div className="flex items-center justify-between gap-2">
                <Button variant="outline" onClick={() => setStep('chatgpt')}>Back</Button>
                <Button variant="outline" onClick={() => onNavigate('home')}>Return home</Button>
              </div>
            </TabsContent>
          </Tabs>
        </CardContent>
      </Card>
    </div>
  );
}
