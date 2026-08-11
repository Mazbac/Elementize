import { Check, Copy, ExternalLink, KeyRound } from 'lucide-react';
import { useMemo, useState, type ReactNode } from 'react';
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Separator } from '@/components/ui/separator';
import { Textarea } from '@/components/ui/textarea';
import { copyText, generateConnectionKey } from '../lib/actions';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = {
  config: ElementizeAdminConfig;
  onNavigate: (page: PageKey) => void;
};

function ActionRow({ number, title, description, action }: { number: number; title: string; description: string; action: ReactNode }) {
  return (
    <div className="flex flex-col gap-3 py-1 sm:flex-row sm:items-center">
      <Badge variant="secondary" className="h-7 w-7 shrink-0 justify-center p-0">{number}</Badge>
      <div className="min-w-0 flex-1">
        <p className="font-medium">{title}</p>
        <p className="mt-1 text-sm text-muted-foreground">{description}</p>
      </div>
      <div className="shrink-0">{action}</div>
    </div>
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

  return (
    <div className="space-y-5">
      <div className="flex flex-col gap-2 sm:flex-row sm:items-end sm:justify-between">
        <div>
          <h2 className="text-lg font-semibold">Setup</h2>
          <p className="mt-1 text-sm text-muted-foreground">Connect your Custom GPT once. Elementize handles the WordPress side.</p>
        </div>
        <Badge variant={websiteReady ? 'default' : 'secondary'} className="w-fit">{websiteReady ? 'WordPress ready' : 'Website needs attention'}</Badge>
      </div>

      {!websiteReady && (
        <Card>
          <CardHeader>
            <CardTitle>Website checks</CardTitle>
            <CardDescription>Resolve these before connecting ChatGPT.</CardDescription>
          </CardHeader>
          <CardContent>
            {requirements.map(([label, ready, detail], index) => (
              <div key={label}>
                {index > 0 && <Separator className="my-3" />}
                <div className="flex items-start justify-between gap-4">
                  <div>
                    <p className="font-medium">{label}</p>
                    <p className="mt-1 text-sm text-muted-foreground">{detail}</p>
                  </div>
                  <Badge variant={ready ? 'outline' : 'secondary'}>{ready ? 'Ready' : 'Fix'}</Badge>
                </div>
              </div>
            ))}
            {!config.environment.connectionReady && (
              <Button variant="outline" className="mt-4" onClick={() => onNavigate('connection')}>Set public address</Button>
            )}
          </CardContent>
        </Card>
      )}

      <Card>
        <CardHeader>
          <CardTitle>Connect ChatGPT</CardTitle>
          <CardDescription>Four actions, then send one test message.</CardDescription>
        </CardHeader>
        <CardContent>
          <ActionRow
            number={1}
            title="Open GPT Builder"
            description="Keep this WordPress page open while you configure the GPT."
            action={
              <Button asChild variant="outline" size="sm">
                <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink /></a>
              </Button>
            }
          />
          <Separator className="my-4" />
          <ActionRow
            number={2}
            title="Copy instructions"
            description="Paste them into the GPT Instructions field."
            action={<Button size="sm" disabled={!config.instructions} onClick={() => handleCopy(config.instructions, 'instructions')}>{copied === 'instructions' ? <Check /> : <Copy />}{copied === 'instructions' ? 'Copied' : 'Copy instructions'}</Button>}
          />
          <Separator className="my-4" />
          <ActionRow
            number={3}
            title="Copy Action schema"
            description="Create one Action in GPT Builder and paste the schema."
            action={<Button size="sm" disabled={!config.schema} onClick={() => handleCopy(config.schema, 'schema')}>{copied === 'schema' ? <Check /> : <Copy />}{copied === 'schema' ? 'Copied' : 'Copy schema'}</Button>}
          />
          <Separator className="my-4" />
          <ActionRow
            number={4}
            title="Generate connection key"
            description="Choose API Key → Basic in Action authentication and paste this key."
            action={<Button size="sm" disabled={keyBusy || !websiteReady} onClick={handleGenerateKey}><KeyRound />{keyBusy ? 'Generating…' : 'Generate key'}</Button>}
          />

          {connectionKey && (
            <>
              <Separator className="my-4" />
              <div className="space-y-3">
                <div>
                  <p className="font-medium">Connection key</p>
                  <p className="mt-1 text-sm text-muted-foreground">Shown once. Treat it like a password.</p>
                </div>
                <Textarea readOnly value={connectionKey} className="min-h-20 resize-none font-mono text-xs" />
                <Button variant="outline" size="sm" onClick={() => handleCopy(connectionKey, 'key')}>
                  {copied === 'key' ? <Check /> : <Copy />}
                  {copied === 'key' ? 'Copied' : 'Copy connection key'}
                </Button>
              </div>
            </>
          )}

          {keyError && (
            <Alert className="mt-4">
              <AlertTitle>Could not generate the key</AlertTitle>
              <AlertDescription>{keyError}</AlertDescription>
            </Alert>
          )}

          <Separator className="my-4" />
          <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
            <div>
              <p className="font-medium">Test the connection</p>
              <p className="mt-1 text-sm text-muted-foreground">Send one status message from your Custom GPT.</p>
            </div>
            <div className="flex flex-wrap gap-2">
              <Button variant="outline" size="sm" onClick={() => handleCopy(config.testPrompt, 'test')}>
                {copied === 'test' ? <Check /> : <Copy />}
                {copied === 'test' ? 'Copied' : 'Copy test message'}
              </Button>
              <Button asChild variant="outline" size="sm">
                <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink /></a>
              </Button>
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
