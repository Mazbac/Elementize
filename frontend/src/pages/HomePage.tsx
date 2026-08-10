import { ExternalLink, Image, Link2, Shapes, ShieldCheck, Type } from 'lucide-react';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = {
  config: ElementizeAdminConfig;
  onNavigate: (page: PageKey) => void;
};

const capabilities = [
  { title: 'Copy & links', body: 'Update recognized text, buttons and destinations without touching layout.', icon: Type },
  { title: 'Images', body: 'Use the Media Library, ChatGPT uploads or permitted public web images.', icon: Image },
  { title: 'Pixfort icons', body: 'Search the installed Pixfort library and use exact verified icon values.', icon: Shapes },
  { title: 'Protected changes', body: 'Fresh-state checks, revisions and persisted verification protect each write.', icon: ShieldCheck },
] as const;

export function HomePage({ config, onNavigate }: Props) {
  return (
    <div className="space-y-6">
      <Card>
        <CardHeader className="pb-3">
          <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
            <div>
              <div className="mb-2 flex items-center gap-2">
                <Badge variant={config.allReady ? 'default' : 'secondary'}>{config.allReady ? 'WordPress ready' : 'Setup incomplete'}</Badge>
              </div>
              <CardTitle className="text-xl">{config.allReady ? 'Elementize is ready to use.' : 'Finish setup to connect ChatGPT.'}</CardTitle>
              <CardDescription className="mt-2 max-w-2xl leading-6">
                Keep building your site in Elementor. Elementize gives ChatGPT a guarded content layer for copy, links, images and Pixfort icons.
              </CardDescription>
            </div>
            <div className="flex shrink-0 flex-wrap gap-2">
              <Button onClick={() => onNavigate('setup')}>{config.allReady ? 'Review setup' : 'Continue setup'}</Button>
              <Button asChild variant="outline">
                <a href={config.urls.gptBuilder} target="_blank" rel="noreferrer">
                  Open GPT Builder <ExternalLink className="h-4 w-4" />
                </a>
              </Button>
            </div>
          </div>
        </CardHeader>
      </Card>

      <section>
        <div className="mb-3">
          <h2 className="text-base font-semibold">What Elementize can change</h2>
          <p className="mt-1 text-sm text-muted-foreground">A small editing surface by design. Layout and styling remain in Elementor.</p>
        </div>
        <div className="grid gap-3 md:grid-cols-2">
          {capabilities.map(({ title, body, icon: Icon }) => (
            <Card key={title}>
              <CardHeader className="flex-row items-start gap-3 space-y-0 pb-2">
                <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-md bg-secondary text-primary">
                  <Icon className="h-4 w-4" />
                </div>
                <div>
                  <CardTitle className="text-sm">{title}</CardTitle>
                  <CardDescription className="mt-1 leading-5">{body}</CardDescription>
                </div>
              </CardHeader>
            </Card>
          ))}
        </div>
      </section>

      <Card>
        <CardContent className="flex flex-col gap-4 pt-5 sm:flex-row sm:items-center sm:justify-between">
          <div className="flex items-start gap-3">
            <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-md bg-secondary text-primary">
              <Link2 className="h-4 w-4" />
            </div>
            <div>
              <p className="font-medium">Connection</p>
              <p className="mt-1 text-sm text-muted-foreground">
                {config.environment.connectionReady ? config.environment.effectiveOrigin : 'A public HTTPS address is still required.'}
              </p>
            </div>
          </div>
          <Button variant="outline" onClick={() => onNavigate('connection')}>Connection settings</Button>
        </CardContent>
      </Card>
    </div>
  );
}
