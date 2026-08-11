export type PageKey = 'home' | 'setup' | 'connection' | 'settings';

export interface ElementizeAdminConfig {
  version: string;
  requirements: {
    elementor: boolean;
    elementorDetail: string;
    pixfort: boolean;
    pixfortDetail: string;
    revisions: boolean;
    applicationPasswords: boolean;
  };
  environment: {
    siteOrigin: string;
    sitePublicHttps: boolean;
    storedPublic: string;
    effectiveOrigin: string;
    connectionReady: boolean;
  };
  materialsReady: boolean;
  allReady: boolean;
  instructions: string;
  schema: string;
  notice: string;
  testPrompt: string;
  urls: {
    gptBuilder: string;
    ajax: string;
    adminPost: string;
    setup: string;
  };
  nonces: {
    generateKey: string;
    saveConnection: string;
  };
}

declare global {
  interface Window {
    ElementizeAdminConfig?: ElementizeAdminConfig;
  }
}
