import { Badge, Stack, Text, UnstyledButton } from '@mantine/core';
import { useState } from 'react';
import { ConnectionPage } from '../pages/ConnectionPage';
import { HomePage } from '../pages/HomePage';
import { SettingsPage } from '../pages/SettingsPage';
import { SetupPage } from '../pages/SetupPage';
import type { ElementizeAdminConfig, PageKey } from '../types';

type Props = { config: ElementizeAdminConfig };

const navItems: Array<{ key: PageKey; label: string; short: string }> = [
  { key: 'home', label: 'Home', short: 'H' },
  { key: 'setup', label: 'Setup', short: 'S' },
  { key: 'connection', label: 'Connection', short: 'C' },
  { key: 'settings', label: 'Settings', short: 'S' },
];

function BrandMark() {
  return (
    <div className="elz-brand-mark" aria-hidden="true">
      <span />
      <span />
    </div>
  );
}

function Notice({ notice }: { notice: string }) {
  if (!notice) return null;
  const messages: Record<string, string> = {
    connection_saved: 'Secure website address saved.',
    connection_cleared: 'Address override cleared. Elementize will use the automatic site address when possible.',
    connection_invalid: 'That address could not be saved. Use a public HTTPS origin without a path, query or credentials.',
  };
  const message = messages[notice];
  if (!message) return null;
  return <div className="elz-notice">{message}</div>;
}

export function App({ config }: Props) {
  const [page, setPage] = useState<PageKey>(config.allReady ? 'home' : 'setup');

  return (
    <div className="elz-app">
      <aside className="elz-sidebar">
        <div className="elz-brand">
          <BrandMark />
          <div>
            <Text className="elz-brand-name">Elementize</Text>
            <Text className="elz-brand-subtitle">for WordPress</Text>
          </div>
        </div>

        <nav className="elz-nav" aria-label="Elementize navigation">
          {navItems.map((item) => (
            <UnstyledButton
              key={item.key}
              className={page === item.key ? 'elz-nav-item elz-nav-item--active' : 'elz-nav-item'}
              onClick={() => setPage(item.key)}
            >
              <span className="elz-nav-icon">{item.short}</span>
              <span>{item.label}</span>
            </UnstyledButton>
          ))}
        </nav>

        <div className="elz-sidebar-footer">
          <div className="elz-sidebar-status">
            <span className="elz-status-dot" />
            <div>
              <Text fw={800} size="sm">{config.allReady ? 'WordPress ready' : 'Setup incomplete'}</Text>
              <Text size="xs">v{config.version}</Text>
            </div>
          </div>
        </div>
      </aside>

      <main className="elz-main">
        <header className="elz-topbar">
          <div>
            <Text className="elz-topbar-kicker">Elementize</Text>
            <Text className="elz-topbar-title">Guarded editing, made simple.</Text>
          </div>
          <Badge className={config.allReady ? 'elz-header-badge elz-header-badge--ready' : 'elz-header-badge'} radius="xl">
            {config.allReady ? 'Ready' : 'Needs setup'}
          </Badge>
        </header>

        <Notice notice={config.notice} />

        <div className="elz-content">
          {page === 'home' && <HomePage config={config} onNavigate={setPage} />}
          {page === 'setup' && <SetupPage config={config} onNavigate={setPage} />}
          {page === 'connection' && <ConnectionPage config={config} />}
          {page === 'settings' && <SettingsPage config={config} />}
        </div>

        <footer className="elz-footer">
          <Stack gap={2}>
            <Text size="sm" fw={700}>Elementize keeps layout and visual design in Elementor.</Text>
            <Text size="xs">Content writes remain guarded by fresh state, revisions and persisted verification.</Text>
          </Stack>
        </footer>
      </main>
    </div>
  );
}
