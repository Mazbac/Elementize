import { CheckCircle2, Sparkles } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Separator } from '@/components/ui/separator';
import { Table, TableBody, TableCell, TableRow } from '@/components/ui/table';
import type { ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

const safety = [
  ['Fresh state required', 'Every write uses a fresh page read and exact expected values.'],
  ['Revision before change', 'A recoverable WordPress revision is required before a content or creative write.'],
  ['Persisted verification', 'Saved Elementor data is verified after each write.'],
  ['Published-page confirmation', 'Live pages require an additional explicit confirmation.'],
] as const;

export function SettingsPage({ config }: Props) {
  const creative = window.ElementizeCreativeConfig;
  const [query, setQuery] = useState('');
  const [selectedPage, setSelectedPage] = useState<number>(creative?.state.scope_page_id || creative?.pages[0]?.id || 0);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');

  const pages = useMemo(() => {
    if (!creative) return [];
    const needle = query.trim().toLowerCase();
    if (!needle) return creative.pages.slice(0, 10);
    return creative.pages
      .filter((page) => `${page.title} ${page.status}`.toLowerCase().includes(needle))
      .slice(0, 10);
  }, [creative, query]);

  async function saveProfile(profile: 'standard' | 'creative', pageId = 0) {
    if (!creative || saving) return;
    if (profile === 'creative' && !pageId) {
      setError('Choose a page before enabling Creative Control.');
      return;
    }
    setSaving(true);
    setError('');
    try {
      const body = new URLSearchParams();
      body.set('action', 'elementize_set_editing_profile');
      body.set('nonce', creative.nonce);
      body.set('profile', profile);
      body.set('page_id', String(pageId));
      const response = await fetch(creative.ajax, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8' },
        body: body.toString(),
        credentials: 'same-origin',
      });
      const result = await response.json();
      if (!result?.success) throw new Error(result?.data?.message || 'Could not change the editing profile.');
      window.location.reload();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Could not change the editing profile.');
      setSaving(false);
    }
  }

  const currentPage = creative?.pages.find((page) => page.id === creative.state.scope_page_id);
  const selected = creative?.pages.find((page) => page.id === selectedPage);

  return (
    <div className="space-y-5">
      <div>
        <h2 className="text-lg font-semibold">Settings</h2>
        <p className="mt-1 text-sm text-muted-foreground">Choose how much control Elementize has. Core safety protections always stay on.</p>
      </div>

      <Card>
        <CardHeader>
          <div className="flex items-center justify-between gap-3">
            <div>
              <CardTitle>Editing control</CardTitle>
              <CardDescription className="mt-1">Standard editing is the default. Creative Control is explicitly enabled for one page at a time.</CardDescription>
            </div>
            <Badge variant={creative?.state.creative_enabled ? 'default' : 'outline'}>
              {creative?.state.creative_enabled ? 'Creative Control on' : 'Standard editing'}
            </Badge>
          </div>
        </CardHeader>
        <CardContent className="space-y-4">
          {!creative ? (
            <Alert>
              <AlertTitle>Creative controls unavailable</AlertTitle>
              <AlertDescription>Reload Elementize after updating the plugin files.</AlertDescription>
            </Alert>
          ) : (
            <>
              <div className="grid gap-3 md:grid-cols-2">
                <div className={`rounded-lg border p-4 ${!creative.state.creative_enabled ? 'bg-secondary/50' : ''}`}>
                  <div className="flex items-center justify-between gap-3">
                    <div>
                      <p className="font-medium">Standard editing</p>
                      <p className="mt-1 text-sm text-muted-foreground">Copy, links, images, icons, natural targeting and screenshot grounding. Structure and styling stay untouched.</p>
                    </div>
                    {!creative.state.creative_enabled && <CheckCircle2 className="h-5 w-5 text-primary" />}
                  </div>
                  <Button className="mt-4" variant={!creative.state.creative_enabled ? 'secondary' : 'outline'} disabled={saving || !creative.state.creative_enabled} onClick={() => void saveProfile('standard')}>
                    {creative.state.creative_enabled ? 'Switch to standard' : 'Active'}
                  </Button>
                </div>

                <div className={`rounded-lg border p-4 ${creative.state.creative_enabled ? 'bg-secondary/50' : ''}`}>
                  <div className="flex items-center justify-between gap-3">
                    <div>
                      <p className="font-medium">Creative Control</p>
                      <p className="mt-1 text-sm text-muted-foreground">Adds templates, structural edits, reordering and guarded design controls while preserving revisions, verification and Undo.</p>
                    </div>
                    <Sparkles className="h-5 w-5 text-primary" />
                  </div>
                  {creative.state.creative_enabled && currentPage && (
                    <p className="mt-3 text-sm"><span className="text-muted-foreground">Current page:</span> <strong>{currentPage.title}</strong></p>
                  )}
                </div>
              </div>

              <Separator />

              <div>
                <p className="font-medium">Creative page</p>
                <p className="mt-1 text-sm text-muted-foreground">Creative operations are blocked server-side on every other page.</p>
                <Input className="mt-3 max-w-md" value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Search pages…" />
                <div className="mt-3 grid gap-2 md:grid-cols-2">
                  {pages.map((page) => (
                    <Button
                      key={page.id}
                      type="button"
                      variant={selectedPage === page.id ? 'secondary' : 'outline'}
                      className="h-auto justify-start px-3 py-2 text-left"
                      onClick={() => setSelectedPage(page.id)}
                    >
                      <span className="min-w-0">
                        <span className="block truncate font-medium">{page.title}</span>
                        <span className="block text-xs font-normal text-muted-foreground">{page.status}</span>
                      </span>
                    </Button>
                  ))}
                </div>
                {selected && (
                  <Button className="mt-4" disabled={saving || (creative.state.creative_enabled && creative.state.scope_page_id === selected.id)} onClick={() => void saveProfile('creative', selected.id)}>
                    {saving ? 'Saving…' : creative.state.creative_enabled && creative.state.scope_page_id === selected.id ? 'Creative Control active here' : `Enable for ${selected.title}`}
                  </Button>
                )}
              </div>

              {error && (
                <Alert variant="destructive">
                  <AlertTitle>Could not change editing control</AlertTitle>
                  <AlertDescription>{error}</AlertDescription>
                </Alert>
              )}
            </>
          )}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <div className="flex items-center justify-between gap-3">
            <div>
              <CardTitle>Safety</CardTitle>
              <CardDescription className="mt-1">Built into every supported write, including Creative Control.</CardDescription>
            </div>
            <Badge variant="outline">Always on</Badge>
          </div>
        </CardHeader>
        <CardContent>
          {safety.map(([title, body], index) => (
            <div key={title}>
              {index > 0 && <Separator className="my-3" />}
              <div className="flex items-start gap-3">
                <CheckCircle2 className="mt-0.5 h-4 w-4 shrink-0 text-primary" />
                <div>
                  <p className="font-medium">{title}</p>
                  <p className="mt-1 text-sm text-muted-foreground">{body}</p>
                </div>
              </div>
            </div>
          ))}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Environment</CardTitle>
          <CardDescription>Detected versions and requirements.</CardDescription>
        </CardHeader>
        <CardContent>
          <Table>
            <TableBody>
              <TableRow>
                <TableCell className="w-[220px] text-muted-foreground">Elementize</TableCell>
                <TableCell className="font-medium">v{config.version}</TableCell>
              </TableRow>
              <TableRow>
                <TableCell className="text-muted-foreground">Elementor</TableCell>
                <TableCell className="font-medium">{config.requirements.elementorDetail}</TableCell>
              </TableRow>
              <TableRow>
                <TableCell className="text-muted-foreground">Pixfort</TableCell>
                <TableCell className="font-medium">{config.requirements.pixfortDetail}</TableCell>
              </TableRow>
              <TableRow>
                <TableCell className="text-muted-foreground">WordPress revisions</TableCell>
                <TableCell className="font-medium">{config.requirements.revisions ? 'Enabled' : 'Needs attention'}</TableCell>
              </TableRow>
            </TableBody>
          </Table>
        </CardContent>
      </Card>
    </div>
  );
}
