import { Check, Copy, Download, ExternalLink, KeyRound, Terminal } from 'lucide-react';
import { useEffect, useState, type ReactNode } from 'react';
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Textarea } from '@/components/ui/textarea';
import { copyText, generateConnectionKey, getConnections } from '../lib/actions';
import type { ConnectionSummary, ElementizeAdminConfig, PageKey } from '../types';

type Props = {
  config: ElementizeAdminConfig;
  onNavigate: (page: PageKey) => void;
};

function Step({ number, title, children }: { number: number; title: string; children: ReactNode }) {
  return (
    <div className="grid gap-3 sm:grid-cols-[32px_1fr]">
      <Badge variant="secondary" className="h-7 w-7 justify-center p-0">{number}</Badge>
      <div className="min-w-0 space-y-3">
        <p className="font-medium">{title}</p>
        {children}
      </div>
    </div>
  );
}

function originHostname(origin: string): string {
  try {
    return new URL(origin).hostname;
  } catch {
    return '';
  }
}

function cloudflareCommand(siteOrigin: string): string {
  const origin = siteOrigin || 'http://localhost';
  const hostname = originHostname(origin);
  const hostHeader = hostname ? ` --http-host-header "${hostname}"` : '';
  const noTlsVerify = origin.toLowerCase().startsWith('https://') ? ' --no-tls-verify' : '';
  return `cloudflared tunnel --url "${origin}"${hostHeader}${noTlsVerify}`;
}

export function SetupPage({ config, onNavigate }: Props) {
  const needsTunnel = !config.environment.sitePublicHttps;
  const coreReady =
    config.requirements.elementor &&
    config.requirements.pixfort &&
    config.requirements.revisions &&
    config.requirements.applicationPasswords &&
    config.materialsReady;

  const [connectionSummary, setConnectionSummary] = useState<ConnectionSummary | null>(null);
  const [connectionSummaryError, setConnectionSummaryError] = useState('');
  const [connectionKey, setConnectionKey] = useState('');
  const [keyBusy, setKeyBusy] = useState(false);
  const [keyError, setKeyError] = useState('');
  const [copied, setCopied] = useState('');

  const tunnelCommand = cloudflareCommand(config.environment.siteOrigin);
  const summaryLoading = connectionSummary === null && !connectionSummaryError;
  const existingKeyCount = connectionSummary?.activeCount ?? 0;
  const hasExistingKey = existingKeyCount > 0;
  const firstTimePairing = config.environment.connectionReady && !summaryLoading && !hasExistingKey;

  useEffect(() => {
    let cancelled = false;
    setConnectionSummaryError('');
    void getConnections(config)
      .then((summary) => {
        if (!cancelled) setConnectionSummary(summary);
      })
      .catch((error) => {
        if (!cancelled) setConnectionSummaryError(error instanceof Error ? error.message : 'Could not load connection status.');
      });
    return () => {
      cancelled = true;
    };
  }, [config]);

  async function handleCopy(value: string, key: string) {
    await copyText(value);
    setCopied(key);
    window.setTimeout(() => setCopied(''), 1500);
  }

  async function handleGenerateKey() {
    setKeyBusy(true);
    setKeyError('');
    try {
      const key = await generateConnectionKey(config);
      setConnectionKey(key);
      try {
        setConnectionSummary(await getConnections(config));
      } catch {
        // The generated key remains valid even if this optional refresh fails.
      }
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
          <p className="mt-1 text-sm text-muted-foreground">One place for first-time pairing and future Local reconnects.</p>
          {config.notice === 'connection_saved' && (
            <p className="mt-1 text-xs text-muted-foreground">Tunnel address saved. Use the refreshed Action schema below.</p>
          )}
        </div>
        <Badge variant={coreReady ? 'default' : 'secondary'} className="w-fit">{coreReady ? 'WordPress ready' : 'Needs attention'}</Badge>
      </div>

      {!coreReady && (
        <Card>
          <CardHeader>
            <CardTitle>WordPress checks</CardTitle>
            <CardDescription>Fix only the items marked below before pairing ChatGPT.</CardDescription>
          </CardHeader>
          <CardContent className="grid gap-2 sm:grid-cols-2">
            {[
              ['Elementor', config.requirements.elementor, config.requirements.elementorDetail],
              ['Pixfort Core', config.requirements.pixfort, config.requirements.pixfortDetail],
              ['Revisions', config.requirements.revisions, config.requirements.revisions ? 'Enabled' : 'Required'],
              ['Application Passwords', config.requirements.applicationPasswords, config.requirements.applicationPasswords ? 'Available' : 'Unavailable'],
              ['GPT materials', config.materialsReady, config.materialsReady ? 'Ready' : 'Missing'],
            ].map(([label, ready, detail]) => (
              <div key={String(label)} className="flex items-start justify-between gap-3 rounded-md border p-3">
                <div>
                  <p className="text-sm font-medium">{String(label)}</p>
                  <p className="mt-1 text-xs text-muted-foreground">{String(detail)}</p>
                </div>
                <Badge variant={ready ? 'outline' : 'secondary'}>{ready ? 'Ready' : 'Fix'}</Badge>
              </div>
            ))}
          </CardContent>
        </Card>
      )}

      {needsTunnel && (
        <Card>
          <CardHeader>
            <div className="flex flex-col gap-2 sm:flex-row sm:items-start sm:justify-between">
              <div>
                <CardTitle>Local connection</CardTitle>
                <CardDescription className="mt-1">After a PC restart, these are the only three steps you normally need.</CardDescription>
              </div>
              <Badge variant={config.environment.connectionReady ? 'outline' : 'secondary'}>
                {config.environment.connectionReady ? 'Tunnel URL saved' : 'Needs tunnel URL'}
              </Badge>
            </div>
          </CardHeader>
          <CardContent className="space-y-5">
            <Step number={1} title="Start Cloudflare">
              <div className="flex flex-col gap-2 sm:flex-row sm:items-center">
                <Textarea readOnly value={tunnelCommand} className="min-h-11 flex-1 resize-none font-mono text-xs" />
                <Button variant="outline" size="sm" onClick={() => handleCopy(tunnelCommand, 'tunnel-command')}>
                  {copied === 'tunnel-command' ? <Check /> : <Terminal />}
                  {copied === 'tunnel-command' ? 'Copied' : 'Copy command'}
                </Button>
              </div>
              <Button asChild variant="ghost" size="sm" className="px-0">
                <a href="https://developers.cloudflare.com/tunnel/downloads/" target="_blank" rel="noreferrer">
                  <Download /> Install cloudflared <ExternalLink />
                </a>
              </Button>
            </Step>

            <Separator />

            <Step number={2} title="Paste the new tunnel URL">
              <form method="post" action={config.urls.adminPost} className="space-y-2">
                <input type="hidden" name="action" value="elementize_save_connection" />
                <input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
                <Label htmlFor="elementize_setup_public_api_origin" className="sr-only">Public HTTPS address</Label>
                <div className="flex flex-col gap-2 sm:flex-row">
                  <Input
                    id="elementize_setup_public_api_origin"
                    name="elementize_public_api_origin"
                    type="url"
                    defaultValue={config.environment.storedPublic}
                    placeholder="https://random-words.trycloudflare.com"
                    required
                    className="flex-1"
                  />
                  <Button type="submit" size="sm">Save URL</Button>
                </div>
              </form>
            </Step>

            <Separator />

            <Step number={3} title="GPT Builder">
              {config.environment.connectionReady && config.schema ? (
                <>
                  <div className="flex flex-wrap gap-2">
                    <Button variant="outline" size="sm" onClick={() => handleCopy(config.instructions, 'instructions-always')} disabled={!config.instructions}>
                      {copied === 'instructions-always' ? <Check /> : <Copy />}
                      {copied === 'instructions-always' ? 'Copied' : 'Copy instructions'}
                    </Button>
                    <Button size="sm" onClick={() => handleCopy(config.schema, 'schema-reconnect')}>
                      {copied === 'schema-reconnect' ? <Check /> : <Copy />}
                      {copied === 'schema-reconnect' ? 'Copied' : 'Copy Action schema'}
                    </Button>
                    <Button asChild variant="outline" size="sm">
                      <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink /></a>
                    </Button>
                    <Button variant="outline" size="sm" onClick={() => handleCopy(config.testPrompt, 'reconnect-test')}>
                      {copied === 'reconnect-test' ? <Check /> : <Copy />}
                      {copied === 'reconnect-test' ? 'Copied' : 'Copy test'}
                    </Button>
                  </div>
                  {hasExistingKey && (
                    <p className="text-xs text-muted-foreground">For a tunnel restart, only the Action schema changes. Your Instructions and API key stay the same, but both remain accessible here.</p>
                  )}
                </>
              ) : (
                <p className="text-sm text-muted-foreground">Save the tunnel URL first. The GPT setup materials will appear here.</p>
              )}
            </Step>
          </CardContent>
        </Card>
      )}

      {!needsTunnel && !config.environment.connectionReady && (
        <Card>
          <CardHeader>
            <CardTitle>Public website address</CardTitle>
            <CardDescription>Save the public HTTPS origin Elementize should use for the GPT Action.</CardDescription>
          </CardHeader>
          <CardContent>
            <form method="post" action={config.urls.adminPost} className="flex flex-col gap-2 sm:flex-row">
              <input type="hidden" name="action" value="elementize_save_connection" />
              <input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
              <Label htmlFor="elementize_setup_public_api_origin" className="sr-only">Public HTTPS address</Label>
              <Input
                id="elementize_setup_public_api_origin"
                name="elementize_public_api_origin"
                type="url"
                defaultValue={config.environment.storedPublic}
                placeholder="https://your-site.example"
                required
                className="flex-1"
              />
              <Button type="submit" size="sm">Save URL</Button>
            </form>
          </CardContent>
        </Card>
      )}

      {summaryLoading && config.environment.connectionReady && (
        <p className="text-sm text-muted-foreground">Checking existing Elementize key…</p>
      )}

      {connectionSummaryError && config.environment.connectionReady && (
        <Alert>
          <AlertTitle>Could not check existing keys</AlertTitle>
          <AlertDescription>{connectionSummaryError}</AlertDescription>
        </Alert>
      )}

      {firstTimePairing && (
        <Card>
          <CardHeader>
            <div className="flex flex-col gap-2 sm:flex-row sm:items-start sm:justify-between">
              <div>
                <CardTitle>First-time authentication</CardTitle>
                <CardDescription className="mt-1">The GPT materials are available above. Generate the API key once, then test the connection.</CardDescription>
              </div>
              <Button asChild variant="outline" size="sm">
                <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink /></a>
              </Button>
            </div>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="flex flex-wrap gap-2">
              <Button size="sm" disabled={keyBusy} onClick={handleGenerateKey}>
                <KeyRound />
                {keyBusy ? 'Generating…' : 'Generate API key'}
              </Button>
              <Button variant="outline" size="sm" onClick={() => handleCopy(config.testPrompt, 'first-test')}>
                {copied === 'first-test' ? <Check /> : <Copy />}
                {copied === 'first-test' ? 'Copied' : 'Copy test'}
              </Button>
            </div>

            {connectionKey && (
              <Alert variant="accent">
                <AlertTitle>Connection key</AlertTitle>
                <AlertDescription className="space-y-3">
                  <p>Shown once. In GPT Builder choose API Key → Basic and paste this value.</p>
                  <Textarea readOnly value={connectionKey} className="min-h-20 resize-none font-mono text-xs" />
                  <Button variant="outline" size="sm" onClick={() => handleCopy(connectionKey, 'key')}>
                    {copied === 'key' ? <Check /> : <Copy />}
                    {copied === 'key' ? 'Copied' : 'Copy key'}
                  </Button>
                </AlertDescription>
              </Alert>
            )}

            {keyError && (
              <Alert>
                <AlertTitle>Could not generate the key</AlertTitle>
                <AlertDescription>{keyError}</AlertDescription>
              </Alert>
            )}
          </CardContent>
        </Card>
      )}

      {!needsTunnel && config.environment.connectionReady && hasExistingKey && (
        <Card>
          <CardHeader>
            <CardTitle>ChatGPT connection</CardTitle>
            <CardDescription>Your GPT materials remain available here whenever you need to update the Custom GPT.</CardDescription>
          </CardHeader>
          <CardContent className="flex flex-wrap gap-2">
            <Button variant="outline" size="sm" onClick={() => handleCopy(config.instructions, 'public-instructions')} disabled={!config.instructions}>
              {copied === 'public-instructions' ? <Check /> : <Copy />}
              {copied === 'public-instructions' ? 'Copied' : 'Copy instructions'}
            </Button>
            <Button size="sm" onClick={() => handleCopy(config.schema, 'public-schema')} disabled={!config.schema}>
              {copied === 'public-schema' ? <Check /> : <Copy />}
              {copied === 'public-schema' ? 'Copied' : 'Copy Action schema'}
            </Button>
            <Button asChild variant="outline" size="sm">
              <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink /></a>
            </Button>
            <Button variant="outline" size="sm" onClick={() => handleCopy(config.testPrompt, 'public-test')}>
              {copied === 'public-test' ? <Check /> : <Copy />}
              {copied === 'public-test' ? 'Copied' : 'Copy test'}
            </Button>
            <Button variant="ghost" size="sm" onClick={() => onNavigate('connection')}>Manage keys</Button>
          </CardContent>
        </Card>
      )}
    </div>
  );
}