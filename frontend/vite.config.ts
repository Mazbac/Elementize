import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const root = fileURLToPath(new URL('.', import.meta.url));

export default defineConfig({
  plugins: [react()],
  define: {
    'process.env.NODE_ENV': JSON.stringify('production'),
  },
  build: {
    target: 'es2020',
    outDir: resolve(root, '../assets/admin'),
    emptyOutDir: true,
    cssCodeSplit: false,
    sourcemap: false,
    lib: {
      entry: resolve(root, 'src/main.tsx'),
      name: 'ElementizeAdmin',
      formats: ['iife'],
      fileName: () => 'elementize-admin.js',
      cssFileName: 'elementize-admin',
    },
  },
});
