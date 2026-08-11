import { Check, Copy, KeyRound, PlugZap, RefreshCw, Trash2 } from 'lucide-react';
import { useEffect, useState } from 'react';
import { Accordion, AccordionContent, AccordionItem, AccordionTrigger } from '@/components/ui/accordion';
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert';
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from '@/components/ui/alert-dialog';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Table, TableBody, TableCell, TableRow } from '@/components/ui/table';
import { Textarea } from '@/components/ui/textarea';
import { copyText, generateConnectionKey, getConnections, revokeAllConnections, revokeConnection } from '../lib/actions';
import type { ConnectionSummary, ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

function formatTime(timestamp: number): string {
  if (!timestamp) return 'Never';
  return new Date(timestamp * 1000).toLocaleString();
}

export function ConnectionPage({ config }: Props) {
  const ready = config.environment.connectionReady;
  const [summary, setSummary] = useState<ConnectionSummary | null>(null);
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState('');
  const [error, setError] = useState('');
  const [connectionKey, setConnectionKey] = useState('');
  const [copied, setCopied] = useState(false);

  async function refreshConnections() {
    setLoading(true);
    setError('');
    try {
      setSummary(await getConnections(config));
    } catch (requestError) {
      setError(requestError instanceof Error ? requestError.message : 'Could not load connection status.');
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    void refreshConnections();
  }, []);

  async function handleGenerateKey() {
    setBusy('generate');
    setError('');
    setConnectionKey('');
    try {
      setConnectionKey(await generateConnectionKey(config));
      setSummary(await getConnections(config));
    } catch (requestError) {
      setError(requestError instanceof Error ? requestError.message : 'Could not generate a connection key.');
    } finally {
      setBusy('');
    }
  }

  async function handleCopyKey() {
    await copyText(connectionKey);
    setCopied(true);
    window.setTimeout(() => setCopied(false), 1500);
  }

  async function handleRevoke(uuid: string) {
    setBusy(uuid);
    setError('');
    try {
      setSummary(await revokeConnection(config, uuid));
    } catch (requestError) {
      setError(requestError instanceof Error ? requestError.message : 'Could not revoke the connection.');
    } finally {
      setBusy('');
    }
  }

  async function handleRevokeAll() {
    setBusy('all');
    setError('');
    try {
      setSummary(await revokeAllConnections(config));
    } catch (requestError) {
      setError(requestError instanceof Error ? requestError.message : 'Could not revoke the connections.');
    } finally {
      setBusy('');
    }
  }

  const statusLabel = loading
    ? 'Checking…'
    : summary?.connected
      ? 'Connected'
      : summary?.activeCount
        ? 'Waiting for test'
        : 'Not connected';

  const statusDescription = summary?.connected
    ? 'A successful authenticated Elementize Action call has been received.'
    : summary?.activeCount
      ? 'A key exists. Send the status test from your Custom GPT to verify the connection.'
      : 'Create a connection key and add it to your Custom GPT.';

  return (
    <div className="space-y-5">
      <div className="flex flex-col gap-2 sm:flex-row sm:items-end sm:justify-between">
        <div>
          <h2 className="text-lg font-semibold">Connection</h2>
          <p className="mt-1 text-sm text-muted-foreground">See whether ChatGPT is actually connected and manage its access.</p>
        </div>
        <Button variant="outline" size="sm" disabled={loading} onClick={() => void refreshConnections()}>
          <RefreshCw className={loading ? 'animate-spin' : ''} />
          Refresh
        </Button>
      </div>

      <Card>
        <CardHeader>
          <div className="flex items-start justify-between gap-4">
            <div className="min-w-0">
              <div className="flex items-center gap-2">
                <PlugZap className="h-4 w-4 text-primary" />
                <CardTitle>ChatGPT connection</CardTitle>
              </div>
              <CardDescription className="mt-2">{statusDescription}</CardDescription>
            </div>
            <Badge variant={summary?.connected ? 'default' : 'secondary'}>{statusLabel}</Badge>
          </div>
        </CardHeader>

        <CardContent>
          <div className="flex flex-wrap gap-x-8 gap-y-2 text-sm">
            <p><span className="text-muted-foreground">Last successful call</span> <span className="ml-1 font-medium">{summary ? formatTime(summary.lastSuccessfulCall) : '—'}</span></p>
            <p><span className="text-muted-foreground">Active keys</span> <span className="ml-1 font-medium">{summary?.activeCount ?? '—'}</span></p>
          </div>

          {error && (
            <Alert className="mt-4">
              <AlertTitle>Connection check failed</AlertTitle>
              <AlertDescription>{error}</AlertDescription>
            </Alert>
          )}

          <Separator className="my-4" />

          <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
            <div>
              <p className="font-medium">Connection keys</p>
              <p className="mt-1 text-sm text-muted-foreground">Only keys created by Elementize are shown here.</p>
            </div>
            <div className="flex flex-wrap gap-2">
              <Button size="sm" disabled={!ready || busy === 'generate'} onClick={handleGenerateKey}>
                <KeyRound />
                {busy === 'generate' ? 'Generating…' : 'New key'}
              </Button>

              {(summary?.activeCount ?? 0) > 1 && (
                <AlertDialog>
                  <AlertDialogTrigger asChild>
                    <Button variant="outline" size="sm" disabled={busy === 'all'}><Trash2 />Revoke all</Button>
                  </AlertDialogTrigger>
                  <AlertDialogContent>
                    <AlertDialogHeader>
                      <AlertDialogTitle>Revoke all Elementize keys?</AlertDialogTitle>
                      <AlertDialogDescription>Your Custom GPT will lose access until you generate and configure a new key.</AlertDialogDescription>
                    </AlertDialogHeader>
                    <AlertDialogFooter>
                      <AlertDialogCancel>Cancel</AlertDialogCancel>
                      <AlertDialogAction onClick={() => void handleRevokeAll()}>Revoke all</AlertDialogAction>
                    </AlertDialogFooter>
                  </AlertDialogContent>
                </AlertDialog>
              )}
            </div>
          </div>

          {connectionKey && (
            <Alert variant="accent" className="mt-4">
              <AlertTitle>New connection key</AlertTitle>
              <AlertDescription className="space-y-3">
                <p>Shown once. Replace the API key in GPT Builder only if you intend to use this new connection.</p>
                <Textarea readOnly value={connectionKey} className="min-h-20 resize-none font-mono text-xs" />
                <Button variant="outline" size="sm" onClick={handleCopyKey}>
                  {copied ? <Check /> : <Copy />}
                  {copied ? 'Copied' : 'Copy key'}
                </Button>
              </AlertDescription>
            </Alert>
          )}

          <div className="mt-4">
            {summary && summary.connections.length === 0 && (
              <p className="text-sm text-muted-foreground">No Elementize connection keys exist yet.</p>
            )}

            {summary?.connections.map((connection, index) => (
              <div key={connection.uuid}>
                {index > 0 && <Separator className="my-3" />}
                <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
                  <div className="min-w-0">
                    <p className="font-medium">Elementize key {index + 1}</p>
                    <p className="mt-1 text-xs text-muted-foreground">
                      Created {formatTime(connection.created)} · Last call {formatTime(connection.lastSuccessfulCall)}
                    </p>
                  </div>
                  <AlertDialog>
                    <AlertDialogTrigger asChild>
                      <Button variant="outline" size="sm" disabled={busy === connection.uuid}><Trash2 />Revoke</Button>
                    </AlertDialogTrigger>
                    <AlertDialogContent>
                      <AlertDialogHeader>
                        <AlertDialogTitle>Revoke this connection?</AlertDialogTitle>
                        <AlertDialogDescription>This key will immediately stop authenticating your Custom GPT.</AlertDialogDescription>
                      </AlertDialogHeader>
                      <AlertDialogFooter>
                        <AlertDialogCancel>Cancel</AlertDialogCancel>
                        <AlertDialogAction onClick={() => void handleRevoke(connection.uuid)}>Revoke key</AlertDialogAction>
                      </AlertDialogFooter>
                    </AlertDialogContent>
                  </AlertDialog>
                </div>
              </div>
            ))}
          </div>

          <Separator className="my-4" />

          <Accordion type="single" collapsible>
            <AccordionItem value="address" className="border-0">
              <AccordionTrigger>Connection address</AccordionTrigger>
              <AccordionContent className="space-y-4">
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
                      placeholder={config.environment.effectiveOrigin || 'https://your-secure-address.example'}
                    />
                    <p className="text-xs text-muted-foreground">Origin only, without <code>/wp-json</code> or another path.</p>
                  </div>
                  <div className="flex flex-wrap gap-2">
                    <Button type="submit">Save address</Button>
                    {config.environment.storedPublic && (
                      <Button type="submit" variant="outline" name="elementize_clear_public_origin" value="1">Use automatic address</Button>
                    )}
                  </div>
                </form>

                <Table>
                  <TableBody>
                    <TableRow>
                      <TableCell className="w-[220px] text-muted-foreground">Current public address</TableCell>
                      <TableCell className="font-medium break-all">{config.environment.effectiveOrigin || 'Unavailable'}</TableCell>
                    </TableRow>
                    <TableRow>
                      <TableCell className="text-muted-foreground">WordPress site</TableCell>
                      <TableCell className="font-medium break-all">{config.environment.siteOrigin || 'Unavailable'}</TableCell>
                    </TableRow>
                    <TableRow>
                      <TableCell className="text-muted-foreground">Public HTTPS detected</TableCell>
                      <TableCell className="font-medium">{config.environment.sitePublicHttps ? 'Yes' : 'No'}</TableCell>
                    </TableRow>
                  </TableBody>
                </Table>
              </AccordionContent>
            </AccordionItem>
          </Accordion>
        </CardContent>
      </Card>
    </div>
  );
}
