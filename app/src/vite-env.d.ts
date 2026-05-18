/// <reference types="vite/client" />

interface IPCRenderer {
  invoke(channel: string, ...args: any[]): Promise<any>;
  on(channel: string, listener: (event: any, ...args: any[]) => void): void;
  removeListener(channel: string, listener: (event: any, ...args: any[]) => void): void;
  send(channel: string, ...args: any[]): void;
}

interface Window {
  ipcRenderer: IPCRenderer;
}
