import '@mantine/core/styles.css';
import './styles.css';
import { MantineProvider } from '@mantine/core';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './app/App';
import { theme } from './theme/theme';

const root = document.getElementById('elementize-admin-root');
const config = window.ElementizeAdminConfig;

if (root && config) {
  createRoot(root).render(
    <StrictMode>
      <MantineProvider theme={theme} defaultColorScheme="light">
        <App config={config} />
      </MantineProvider>
    </StrictMode>
  );
}
