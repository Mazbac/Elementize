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
    <Alert variant="accent" className="mt-4">
      <AlertTitle>{message.title}</AlertTitle>
      <AlertDescription>{message.body}</AlertDescription>
    </Alert>
  );
}

export function App({ config }: Props) {
  const [page, setPage] = useState<PageKey>(config.allReady ? 'home' : 'setup');

  return (
    <div className="min-h-[calc(100vh-32px)] bg-background text-foreground">
      <div className="mx-auto w-full max-w-[1040px] px-5 py-5 lg:px-6">
        <header className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
          <div>
            <div className="flex items-center gap-2">
              <Sparkles className="h-5 w-5 text-primary" />
              <h1 className="text-xl font-semibold tracking-tight">Elementize</h1>
              <Badge variant="outline">v{config.version}</Badge>
            </div>
            <p className="mt-1 text-sm text-muted-foreground">Safe Elementor content editing through ChatGPT.</p>
          </div>

          <Badge variant={config.allReady ? 'default' : 'secondary'} className="w-fit gap-1.5">
            {config.allReady ? <CircleCheck className="h-3.5 w-3.5" /> : <CircleAlert className="h-3.5 w-3.5" />}
            {config.allReady ? 'Ready' : 'Needs setup'}
          </Badge>
        </header>

        <Tabs value={page} onValueChange={(value) => setPage(value as PageKey)} className="mt-4">
          <TabsList className="h-auto gap-1 bg-transparent p-0">
            {navItems.map((item) => {
              const Icon = item.icon;
              return (
                <TabsTrigger
                  key={item.key}
                  value={item.key}
                  className="gap-2 rounded-md px-3 py-2 shadow-none data-[state=active]:bg-secondary data-[state=active]:text-foreground data-[state=active]:shadow-none"
                >
                  <Icon className="h-4 w-4" />
                  {item.label}
                </TabsTrigger>
              );
            })}
          </TabsList>
        </Tabs>

        <Notice notice={config.notice} />

        <main className="mt-5">
          {page === 'home' && <HomePage config={config} onNavigate={setPage} />}
          {page === 'setup' && <SetupPage config={config} onNavigate={setPage} />}
          {page === 'connection' && <ConnectionPage config={config} />}
          {page === 'settings' && <SettingsPage config={config} />}
        </main>
      </div>
    </div>
  );
}
