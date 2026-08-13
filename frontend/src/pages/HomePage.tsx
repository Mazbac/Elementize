import { ExternalLink, Image, Shapes, ShieldCheck, Sparkles, Type } from 'lucide-react';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Separator } from '@/components/ui/separator';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = {
  config: ElementizeAdminConfig;
  onNavigate: (page: PageKey) => void;
};

const capabilities = [
  { title: 'Copy & links', body: 'Recognized text, buttons and destinations.', icon: Type },
  { title: 'Images', body: 'Media Library, uploads, ChatGPT-generated images and permitted public images.', icon: Image },
  { title: 'Pixfort icons', body: 'Exact icons from the installed Pixfort library.', icon: Shapes },
  { title: 'Creative Control', body: 'Optional page-scoped templates, structure, reordering and guarded design controls.', icon: Sparkles },
  { title: 'Protected changes', body: 'Fresh state, revisions, persisted verification, Activity and whole-change Undo.', icon: ShieldCheck },
] as const;

export function HomePage({ config, onNavigate }: Props) {
  const creative = window.ElementizeCreativeConfig;
  const creativePage = creative?.pages.find((item) => item.id === creative.state.scope_page_id);

  return (
    <div className="space-y-5">
      <Card>
        <CardHeader>
          <div className="flex flex-col gap-4 sm:flex-row sm:items-start sm:justify-between">
            <div>
              <Badge variant={config.allReady ? 'default' : 'secondary'}>{config.allReady ? 'WordPress ready' : 'Setup incomplete'}</Badge>
              <CardTitle className="mt-3 text-lg">{config.allReady ? 'Elementize is ready.' : 'Finish setup to connect ChatGPT.'}</CardTitle>
              <CardDescription className="mt-2 max-w-2xl leading-5">
                Standard editing stays content-focused. Enable Creative Control for one page when ChatGPT should also compose and restyle local Elementor content.
              </CardDescription>
              {creative?.state.creative_enabled && creativePage && (
                <p className="mt-2 text-sm font-medium">Creative Control is active for {creativePage.title}.</p>
              )}
            </div>
            <div className="flex shrink-0 flex-wrap gap-2">
              <Button onClick={() => onNavigate(config.allReady ? 'settings' : 'setup')}>{config.allReady ? 'Editing control' : 'Continue setup'}</Button>
              <Button asChild variant="outline">
                <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">Open GPT Builder <ExternalLink /></a>
              </Button>
            </div>
          </div>
        </CardHeader>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Supported changes</CardTitle>
          <CardDescription>Creative power is explicit; the same safety engine stays on in both modes.</CardDescription>
        </CardHeader>
        <CardContent>
          {capabilities.map(({ title, body, icon: Icon }, index) => (
            <div key={title}>
              {index > 0 && <Separator className="my-3" />}
              <div className="flex items-start gap-3">
                <Badge variant="secondary" className="h-7 w-7 shrink-0 justify-center p-0"><Icon className="h-4 w-4" /></Badge>
                <div>
                  <p className="font-medium">{title}</p>
                  <p className="mt-1 text-sm text-muted-foreground">{body}</p>
                </div>
              </div>
            </div>
          ))}
        </CardContent>
      </Card>
    </div>
  );
}
