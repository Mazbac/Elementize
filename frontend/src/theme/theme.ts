import { createTheme } from '@mantine/core';

export const palette = {
  canvas: '#f2f3f7',
  steel: '#94adbf',
  lavender: '#ccafe6',
  violet: '#9854cb',
  royal: '#64379f',
  primary: '#56188f',
  muted: '#584170',
  inkSoft: '#2a1b3a',
  ink: '#1d0432',
} as const;

export const theme = createTheme({
  primaryColor: 'brand',
  primaryShade: 6,
  white: palette.canvas,
  black: palette.ink,
  colors: {
    brand: [
      palette.canvas,
      palette.lavender,
      palette.lavender,
      palette.violet,
      palette.violet,
      palette.royal,
      palette.primary,
      palette.primary,
      palette.inkSoft,
      palette.ink,
    ],
    gray: [
      palette.canvas,
      palette.canvas,
      palette.lavender,
      palette.steel,
      palette.steel,
      palette.muted,
      palette.muted,
      palette.inkSoft,
      palette.inkSoft,
      palette.ink,
    ],
  },
  fontFamily:
    '-apple-system, BlinkMacSystemFont, "Segoe UI", Inter, Roboto, Helvetica, Arial, sans-serif',
  headings: {
    fontFamily:
      '-apple-system, BlinkMacSystemFont, "Segoe UI", Inter, Roboto, Helvetica, Arial, sans-serif',
    fontWeight: '750',
  },
  defaultRadius: 'md',
  cursorType: 'pointer',
});
