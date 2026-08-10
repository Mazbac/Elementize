import { CircleAlert, CircleCheck, Home, Plug, Settings, Sparkles } from 'lucide-react';
import { useState } from 'react';
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert';
import { Badge } from '@/components/ui/badge';
import { Tabs, TabsList, TabsTrigger } from '@/components/ui/tabs';
import { ConnectionPage } from '../pages/ConnectionPage';
import { HomePage } from '../pages/HomePage';
import { SettingsPage } from '../pages/SettingsPage';
import { SetupPage } from '../pages/SetupPage';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = { config: ElementizeAdminConfig };

const navItems = [
  { key: 'home' as const, label: 'Home', icon: Home },
  { key: 'setup' as const, label: 'Setup', icon: Sparkles },
  { key: 'connection' as const, label: 'Connection', icon: Plug },
  { key: 'settings' as const, label: 'Settings', icon: Settings },
];

function Notice({ notice }: { notice: string }) {
  const messages: Record<string, { title: string; body: string }> = {
    connection_saved: { title: 'Connection saved', body: 'The secure website address was updated.' },
    connection_cleared: { title: 'Automatic address restored', body: 'Elementize will use the detected site address when possible.' },
    connection_invalid: { title: 'Address not saved', body: 'Use a public HTTPS origin without a path, query or credentials.' },
  };

  const message = messages[notice];
  if (!message) return null;

  return (
    <Alert variant="accent" className="mt-5">
      <AlertTitle>{message.title}</AlertTitle>
      <AlertDescription>{message.body}</AlertDescription>
    </Alert>
  );
}

export function App({ config }: Props) {
  const [page, setPage] = useState<PageKey>(config.allReady ? 'home' : 'setup');

  return (
    <div className="min-h-[calc(100vh-32px)] bg-background text-foreground">
      <div className="mx-auto w-full max-w-[1180px] px-5 py-6 lg:px-7">
        <header className="flex flex-col gap-4 sm:flex-row sm:items-start sm:justify-between">
          <div className="flex items-start gap-3">
            <div className="flex h-9 w-9 shrink-0 items-center justify-center rounded-md bg-primary text-primary-foreground shadow-sm">
              <Sparkles className="h-4 w-4" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h1 className="text-[23px] font-semibold leading-7 tracking-[-0.01em]">Elementize</h1>
                <Badge variant="outline">v{config.version}</Badge>
              </div>
              <p className="mt-1 max-w-2xl text-sm text-muted-foreground">
                Safe content editing for Elementor and Pixfort, connected to ChatGPT.
              </p>
            </div>
          </div>

          <Badge variant={config.allReady ? 'default' : 'secondary'} className="w-fit gap-1.5 px-2.5 py-1">
            {config.allReady ? <CircleCheck className="h-3.5 w-3.5" /> : <CircleAlert className="h-3.5 w-3.5" />}
            {config.allReady ? 'Ready' : 'Needs setup'}
          </Badge>
        </header>

        <Tabs value={page} onValueChange={(value) => setPage(value as PageKey)} className="mt-5">
          <TabsList className="h-auto w-full justify-start gap-1 rounded-none border-b bg-transparent p-0">
            {navItems.map((item) => {
              const Icon = item.icon;
              return (
                <TabsTrigger
                  key={item.key}
                  value={item.key}
                  className="gap-2 rounded-none border-b-2 border-transparent bg-transparent px-3 py-2.5 text-[13px] shadow-none data-[state=active]:border-primary data-[state=active]:bg-transparent data-[state=active]:text-primary data-[state=active]:shadow-none"
                >
                  <Icon className="h-4 w-4" />
                  {item.label}
                </TabsTrigger>
              );
            })}
          </TabsList>
        </Tabs>

        <Notice notice={config.notice} />

        <main className="mt-6">
          {page === 'home' && <HomePage config={config} onNavigate={setPage} />}
          {page === 'setup' && <SetupPage config={config} onNavigate={setPage} />}
          {page === 'connection' && <ConnectionPage config={config} />}
          {page === 'settings' && <SettingsPage config={config} />}
        </main>

        <footer className="mt-8 border-t pt-4 text-xs text-muted-foreground">
          Elementize leaves layout and visual design in Elementor. Content writes stay guarded by fresh state, revisions and persisted verification.
        </footer>
      </div>
    </div>
  );
}
