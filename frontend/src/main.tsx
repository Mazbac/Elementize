import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './app/App';
import './index.css';

const root = document.getElementById('elementize-admin-root');
const config = window.ElementizeAdminConfig;

if (root && config) {
  createRoot(root).render(
    <StrictMode>
      <App config={config} />
    </StrictMode>
  );
}
