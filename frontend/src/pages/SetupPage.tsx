import { Check, Copy, Download, ExternalLink, KeyRound, Terminal } from 'lucide-react';
import { useEffect, useMemo, useState, type ReactNode } from 'react';
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
  const [connectionSummary, setConnectionSummary] = useState<ConnectionSummary | null>(null);
  const [connectionSummaryError, setConnectionSummaryError] = useState('');
  const tunnelCommand = cloudflareCommand(config.environment.siteOrigin);
  const previouslyVerified = Boolean(connectionSummary?.connected);
  const existingKeyCount = connectionSummary?.activeCount ?? 0;
  const connectionSummaryLoading = connectionSummary === null && !connectionSummaryError;

  const requirements = useMemo(
    () => [
      ['Elementor', config.requirements.elementor, config.requirements.elementorDetail],
      ['Pixfort Core', config.requirements.pixfort, config.requirements.pixfortDetail],
      ['WordPress revisions', config.requirements.revisions, config.requirements.revisions ? 'Enabled' : 'Enable revisions before editing'],
      ['Secure login', config.requirements.applicationPasswords, config.requirements.applicationPasswords ? 'Application Passwords available' : 'Application Passwords unavailable'],
      [
        'Public HTTPS address',
        config.environment.connectionReady,
        config.environment.connectionReady
          ? needsTunnel
            ? `${config.environment.effectiveOrigin} (saved tunnel address)`
            : config.environment.effectiveOrigin
          : 'A public HTTPS address is required',
      ],
      ['GPT setup materials', config.materialsReady, config.materialsReady ? 'Instructions and Action schema available' : 'Setup files could not be loaded'],
    ] as const,
    [config, needsTunnel]
  );

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
        // The generated key is still valid even if the optional status refresh fails.
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
          <p className="mt-1 text-sm text-muted-foreground">Set up once, then use the same page whenever a local tunnel needs reconnecting.</p>
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
          </CardContent>
        </Card>
      )}

      {needsTunnel && (
        <Card>
          <CardHeader>
            <div className="flex flex-col gap-2 sm:flex-row sm:items-start sm:justify-between">
              <div>
                <CardTitle>Local site connection</CardTitle>
                <CardDescription className="mt-1">
                  Your WordPress site is not public HTTPS, so ChatGPT reaches it through a temporary Cloudflare Quick Tunnel. Reconnect it here after a PC or tunnel restart.
                </CardDescription>
              </div>
              <Badge variant={config.environment.connectionReady ? 'outline' : 'secondary'} className="w-fit">
                {config.environment.connectionReady ? 'Tunnel address saved' : 'Needs tunnel'}
              </Badge>
            </div>
          </CardHeader>
          <CardContent>
            {config.environment.storedPublic && (
              <Alert variant="accent" className="mb-4">
                <AlertTitle>Quick Tunnel addresses are temporary</AlertTitle>
                <AlertDescription>
                  If cloudflared or your PC was restarted, the old <code>trycloudflare.com</code> address can stop working. Run the command below, save the new address, then refresh the GPT Action schema. Your existing connection key does not need to change.
                </AlertDescription>
              </Alert>
            )}

            <ActionRow
              number={1}
              title="Start or restart the tunnel"
              description="Run this exact command in PowerShell or Command Prompt and keep that terminal open. The Host header is pinned to your Local site so Local routes the request to the correct WordPress install."
              action={
                <Button asChild variant="outline" size="sm">
                  <a href="https://developers.cloudflare.com/tunnel/downloads/" target="_blank" rel="noreferrer">
                    <Download /> cloudflared <ExternalLink />
                  </a>
                </Button>
              }
            />
            <div className="mt-3 space-y-3">
              <Textarea readOnly value={tunnelCommand} className="min-h-16 resize-none font-mono text-xs" />
              <Button variant="outline" size="sm" onClick={() => handleCopy(tunnelCommand, 'tunnel-command')}>
                {copied === 'tunnel-command' ? <Check /> : <Terminal />}
                {copied === 'tunnel-command' ? 'Copied' : 'Copy tunnel command'}
              </Button>
            </div>

            <Separator className="my-4" />

            <div className="space-y-3">
              <div className="flex items-start gap-3">
                <Badge variant="secondary" className="h-7 w-7 shrink-0 justify-center p-0">2</Badge>
                <div>
                  <p className="font-medium">Save the new Cloudflare address</p>
                  <p className="mt-1 text-sm text-muted-foreground">Copy the generated <code>https://…trycloudflare.com</code> URL from the terminal and replace the saved address below. Save only the origin, without <code>/wp-json</code>.</p>
                </div>
              </div>

              <form method="post" action={config.urls.adminPost} className="space-y-3">
                <input type="hidden" name="action" value="elementize_save_connection" />
                <input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
                <div className="space-y-2">
                  <Label htmlFor="elementize_setup_public_api_origin">Public HTTPS address</Label>
                  <Input
                    id="elementize_setup_public_api_origin"
                    name="elementize_public_api_origin"
                    type="url"
                    defaultValue={config.environment.storedPublic}
                    placeholder="https://random-words.trycloudflare.com"
                    required
                  />
                  {config.environment.effectiveOrigin && (
                    <p className="text-xs text-muted-foreground">Currently saved: <span className="font-medium break-all">{config.environment.effectiveOrigin}</span></p>
                  )}
                </div>
                <Button type="submit" size="sm">Save & continue setup</Button>
              </form>
            </div>

            <Alert className="mt-4">
              <AlertTitle>Stay on this Setup page</AlertTitle>
              <AlertDescription>After saving, Elementize reloads with the new address and regenerates the Action schema for that tunnel. You should not need the separate Connection page for a normal restart.</AlertDescription>
            </Alert>

            {config.environment.connectionReady && config.schema && (
              <>
                <Separator className="my-4" />
                <ActionRow
                  number={3}
                  title="Refresh the GPT Action"
                  description={previouslyVerified
                    ? 'Open your existing GPT Action and paste the fresh schema. Keep the current API key; only the temporary tunnel address changed.'
                    : 'Paste this fresh schema into the GPT Action. If this is your first setup, continue with the pairing steps below afterward.'}
                  action={
                    <div className="flex flex-wrap gap-2">
                      <Button size="sm" onClick={() => handleCopy(config.schema, 'schema-reconnect')}>
                        {copied === 'schema-reconnect' ? <Check /> : <Copy />}
                        {copied === 'schema-reconnect' ? 'Copied' : 'Copy fresh schema'}
                      </Button>
                      <Button asChild variant="outline" size="sm">
                        <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink /></a>
                      </Button>
                    </div>
                  }
                />
                <div className="mt-3 flex flex-wrap gap-2 pl-0 sm:pl-10">
                  <Button variant="outline" size="sm" onClick={() => handleCopy(config.testPrompt, 'reconnect-test')}>
                    {copied === 'reconnect-test' ? <Check /> : <Copy />}
                    {copied === 'reconnect-test' ? 'Copied' : 'Copy connection test'}
                  </Button>
                </div>
              </>
            )}
          </CardContent>
        </Card>
      )}

      {config.environment.sitePublicHttps && !config.environment.connectionReady && (
        <Card>
          <CardHeader>
            <CardTitle>Public HTTPS address</CardTitle>
            <CardDescription>Elementize needs a public HTTPS origin before it can generate the GPT Action schema.</CardDescription>
          </CardHeader>
          <CardContent>
            <form method="post" action={config.urls.adminPost} className="space-y-3">
              <input type="hidden" name="action" value="elementize_save_connection" />
              <input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
              <div className="space-y-2">
                <Label htmlFor="elementize_public_api_origin">Public HTTPS address</Label>
                <Input
                  id="elementize_public_api_origin"
                  name="elementize_public_api_origin"
                  type="url"
                  defaultValue={config.environment.storedPublic}
                  placeholder="https://your-secure-address.example"
                  required
                />
              </div>
              <Button type="submit" size="sm">Save & continue setup</Button>
            </form>
          </CardContent>
        </Card>
      )}

      {config.environment.connectionReady && connectionSummaryLoading && (
        <Card>
          <CardHeader>
            <CardTitle>Checking ChatGPT pairing…</CardTitle>
            <CardDescription>Elementize is checking whether this WordPress connection has already completed a successful Action call.</CardDescription>
          </CardHeader>
        </Card>
      )}

      {config.environment.connectionReady && !connectionSummaryLoading && previouslyVerified && (
        <Card>
          <CardHeader>
            <div className="flex items-start justify-between gap-4">
              <div>
                <CardTitle>ChatGPT already paired</CardTitle>
                <CardDescription className="mt-1">A current Elementize key has successfully authenticated before.</CardDescription>
              </div>
              <Badge variant="default">Previously verified</Badge>
            </div>
          </CardHeader>
          <CardContent>
            <p className="text-sm text-muted-foreground">
              For a normal Local/Cloudflare restart, use the three reconnect steps above. Do not create a new API key or repaste the Instructions unless you intentionally want to replace the GPT connection.
            </p>
            <div className="mt-4 flex flex-wrap gap-2">
              <Button variant="outline" size="sm" onClick={() => handleCopy(config.testPrompt, 'paired-test')}>
                {copied === 'paired-test' ? <Check /> : <Copy />}
                {copied === 'paired-test' ? 'Copied' : 'Copy connection test'}
              </Button>
              <Button asChild variant="outline" size="sm">
                <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink /></a>
              </Button>
              <Button variant="ghost" size="sm" onClick={() => onNavigate('connection')}>Manage keys</Button>
            </div>
          </CardContent>
        </Card>
      )}

      {config.environment.connectionReady && !connectionSummaryLoading && !previouslyVerified && (
        <Card>
          <CardHeader>
            <CardTitle>Connect ChatGPT</CardTitle>
            <CardDescription>First-time pairing stays on this Setup page. Once a successful Action call is received, future tunnel restarts use the short reconnect flow above.</CardDescription>
          </CardHeader>
          <CardContent>
            {connectionSummaryError && (
              <Alert className="mb-4">
                <AlertTitle>Connection status could not be checked</AlertTitle>
                <AlertDescription>{connectionSummaryError}</AlertDescription>
              </Alert>
            )}

            {existingKeyCount > 0 && (
              <Alert variant="accent" className="mb-4">
                <AlertTitle>An Elementize key already exists</AlertTitle>
                <AlertDescription>If that key is already pasted into GPT Builder, keep using it. Generate another key only if you still need a one-time key value to paste.</AlertDescription>
              </Alert>
            )}

            <ActionRow
              number={1}
              title="Open GPT Builder"
              description="Keep this WordPress Setup page open while you configure the GPT."
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
              description="Create one Action in GPT Builder and paste the schema generated for the current public address."
              action={<Button size="sm" disabled={!config.schema} onClick={() => handleCopy(config.schema, 'schema')}>{copied === 'schema' ? <Check /> : <Copy />}{copied === 'schema' ? 'Copied' : 'Copy Action schema'}</Button>}
            />
            <Separator className="my-4" />
            <ActionRow
              number={4}
              title="Generate connection key"
              description="Choose API Key → Basic in Action authentication and paste this key. The key survives tunnel and PC restarts."
              action={<Button size="sm" disabled={keyBusy || !websiteReady} onClick={handleGenerateKey}><KeyRound />{keyBusy ? 'Generating…' : 'Generate key'}</Button>}
            />

            {connectionKey && (
              <>
                <Separator className="my-4" />
                <div className="space-y-3">
                  <div>
                    <p className="font-medium">Connection key</p>
                    <p className="mt-1 text-sm text-muted-foreground">Shown once. Treat it like a password. You do not need a fresh key when only the Cloudflare URL changes.</p>
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
                <p className="mt-1 text-sm text-muted-foreground">Send one status message from your Custom GPT. After the first successful call, Elementize will recognize this connection as paired.</p>
              </div>
              <div className="flex flex-wrap gap-2">
                <Button variant="outline" size="sm" onClick={() => handleCopy(config.testPrompt, 'test')}>
                  {copied === 'test' ? <Check /> : <Copy />}
                  {copied === 'test' ? 'Copied' : 'Copy test message'}
                </Button>
                <Button asChild variant="outline" size="sm">
                  <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink /></a>
                </Button>
                <Button variant="ghost" size="sm" onClick={() => onNavigate('connection')}>Advanced connection</Button>
              </div>
            </div>
          </CardContent>
        </Card>
      )}
    </div>
  );
}
