import { ExternalLink, History, RefreshCw, RotateCcw, Sparkles } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Separator } from '@/components/ui/separator';
import { getActivity, undoActivity } from '../lib/actions';
import type { ActivityItem, ActivityResponse, ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

const kindLabels: Record<string, string> = {
  text: 'copy',
  link: 'link',
  media: 'image',
  pixfort_icon: 'icon',
  insert_template: 'template insert',
  remove_element: 'removal',
  duplicate_element: 'duplication',
  move_element: 'move',
  reorder_children: 'reorder',
  design_color: 'color change',
  design_spacing: 'spacing change',
  design_radius: 'radius change',
  design_alignment: 'alignment change',
  design_typography: 'typography change',
  design_size: 'size change',
};

function formatKinds(item: ActivityItem): string {
  const parts = Object.entries(item.kinds || {})
    .filter(([, count]) => count > 0)
    .map(([kind, count]) => `${count} ${kindLabels[kind] || kind}${count === 1 ? '' : 's'}`);
  return parts.length ? parts.join(', ') : `${item.updated_count} change${item.updated_count === 1 ? '' : 's'}`;
}

function formatDate(value: string): string {
  if (!value) return '';
  const date = new Date(`${value.replace(' ', 'T')}Z`);
  if (Number.isNaN(date.getTime())) return value;
  return new Intl.DateTimeFormat(undefined, {
    dateStyle: 'medium',
    timeStyle: 'short',
  }).format(date);
}

export function ActivityPage({ config }: Props) {
  const [activity, setActivity] = useState<ActivityResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [busyId, setBusyId] = useState('');
  const [confirmId, setConfirmId] = useState('');
  const [error, setError] = useState('');

  async function load() {
    setLoading(true);
    setError('');
    try {
      setActivity(await getActivity(config, 30));
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : 'Could not load activity.');
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    void load();
  }, []);

  const items = useMemo(() => activity?.items || [], [activity]);

  async function handleUndo(item: ActivityItem) {
    setBusyId(item.id);
    setError('');
    try {
      await undoActivity(config, item.id, item.page_status === 'publish');
      setConfirmId('');
      await load();
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : 'Could not undo this change.');
    } finally {
      setBusyId('');
    }
  }

  return (
    <div className="space-y-5">
      <div className="flex flex-col gap-3 sm:flex-row sm:items-end sm:justify-between">
        <div>
          <h2 className="text-lg font-semibold">Activity</h2>
          <p className="mt-1 text-sm text-muted-foreground">Recent verified Elementize edits and creative transactions, with guarded whole-change Undo.</p>
        </div>
        <Button variant="outline" size="sm" onClick={() => void load()} disabled={loading}>
          <RefreshCw className={loading ? 'animate-spin' : ''} />
          Refresh
        </Button>
      </div>

      {error && (
        <Alert>
          <AlertTitle>Activity needs attention</AlertTitle>
          <AlertDescription>{error}</AlertDescription>
        </Alert>
      )}

      <Card>
        <CardHeader>
          <div className="flex items-center gap-2">
            <History className="h-4 w-4 text-primary" />
            <CardTitle>Recent changes</CardTitle>
          </div>
          <CardDescription>Only successful writes that passed persisted verification appear here.</CardDescription>
        </CardHeader>
        <CardContent>
          {loading && !activity ? (
            <p className="text-sm text-muted-foreground">Loading activity…</p>
          ) : items.length === 0 ? (
            <div className="py-6 text-center">
              <p className="font-medium">No Elementize changes yet.</p>
              <p className="mt-1 text-sm text-muted-foreground">Verified edits will appear here automatically.</p>
            </div>
          ) : (
            items.map((item, index) => {
              const title = item.page?.title || item.page_title || `Page ${item.page_id}`;
              const targetUrl = item.page?.elementor_edit_url || item.page?.preview_url || item.page?.permalink || '';
              const undone = Boolean(item.undone_at_gmt);
              const confirming = confirmId === item.id;
              return (
                <div key={item.id}>
                  {index > 0 && <Separator className="my-4" />}
                  <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
                    <div className="min-w-0">
                      <div className="flex flex-wrap items-center gap-2">
                        <p className="font-medium">{title}</p>
                        <Badge variant={undone ? 'secondary' : 'outline'}>{undone ? 'Undone' : 'Verified'}</Badge>
                        {item.creative && <Badge variant="default" className="gap-1"><Sparkles className="h-3 w-3" /> Creative</Badge>}
                        {item.page_status === 'publish' && <Badge variant="secondary">Live page</Badge>}
                      </div>
                      {item.creative && item.plan_label && <p className="mt-1 text-sm font-medium">{item.plan_label}</p>}
                      <p className="mt-1 text-sm text-muted-foreground">{formatKinds(item)}</p>
                      <p className="mt-1 text-xs text-muted-foreground">
                        {formatDate(item.created_gmt)}{item.user_name ? ` · ${item.user_name}` : ''} · Revision {item.revision_id}
                      </p>
                      {!undone && !item.undo_available && (
                        <p className="mt-2 text-xs text-muted-foreground">Undo is unavailable because the page changed afterward or the snapshot is no longer available.</p>
                      )}
                      {undone && item.undone_at_gmt && (
                        <p className="mt-2 text-xs text-muted-foreground">Undone {formatDate(item.undone_at_gmt)} · Safety revision {item.undo_revision_id}</p>
                      )}
                    </div>

                    <div className="flex shrink-0 flex-wrap gap-2">
                      {targetUrl && (
                        <Button asChild variant="outline" size="sm">
                          <a href={targetUrl} target="_blank" rel="noreferrer">Open page <ExternalLink /></a>
                        </Button>
                      )}
                      {item.undo_available && !confirming && (
                        <Button variant="outline" size="sm" onClick={() => setConfirmId(item.id)}>
                          <RotateCcw /> Undo
                        </Button>
                      )}
                      {item.undo_available && confirming && (
                        <>
                          <Button variant="outline" size="sm" onClick={() => setConfirmId('')} disabled={busyId === item.id}>Cancel</Button>
                          <Button size="sm" onClick={() => void handleUndo(item)} disabled={busyId === item.id}>
                            <RotateCcw /> {busyId === item.id ? 'Undoing…' : item.page_status === 'publish' ? 'Confirm live undo' : 'Confirm undo'}
                          </Button>
                        </>
                      )}
                    </div>
                  </div>
                </div>
              );
            })
          )}
        </CardContent>
      </Card>
    </div>
  );
}
