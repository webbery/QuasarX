/**
 * PDF 文件管理工具
 * 通过 Electron ipcRenderer 与主进程通信，管理 knowledge 目录下的 PDF 文件
 */

import { ipcRenderer } from 'electron';

export interface PdfFileInfo {
  name: string;
  size: number;
  mtime: number;
  path: string;
  hash: string;  // SHA-256 hash
}

export interface SavePdfResult {
  success: boolean;
  fileName: string;
  path: string;
  error?: string;
}

export interface ListPdfsResult {
  success: boolean;
  pdfs: PdfFileInfo[];
  error?: string;
}

export interface ReadPdfResult {
  success: boolean;
  data: string;  // base64 encoded
  error?: string;
}

/**
 * 计算文件的 SHA-256 hash（用于去重）
 */
export async function calculateFileHash(fileData: ArrayBuffer): Promise<string> {
  const buffer = new Uint8Array(fileData);
  const hash = await crypto.subtle.digest('SHA-256', buffer);
  const hashArray = Array.from(new Uint8Array(hash));
  return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
}

/**
 * 保存 PDF 文件到 knowledge 目录
 */
export async function savePdf(fileData: ArrayBuffer, fileName: string): Promise<SavePdfResult> {
  const uint8Array = new Uint8Array(fileData);
  return await ipcRenderer.invoke('knowledge-save-pdf', Array.from(uint8Array), fileName);
}

/**
 * 列出 knowledge 目录下的所有 PDF 文件
 */
export async function listPdfs(): Promise<ListPdfsResult> {
  return await ipcRenderer.invoke('knowledge-list-pdfs');
}

/**
 * 删除 knowledge 目录下的 PDF 文件
 */
export async function deletePdf(fileName: string): Promise<{ success: boolean; error?: string }> {
  return await ipcRenderer.invoke('knowledge-delete-pdf', fileName);
}

/**
 * 读取 PDF 文件内容（base64）
 */
export async function readPdf(fileName: string): Promise<ReadPdfResult> {
  return await ipcRenderer.invoke('knowledge-read-pdf', fileName);
}

/**
 * 获取 knowledge 目录路径
 */
export async function getKnowledgeDir(): Promise<{ success: boolean; path: string }> {
  return await ipcRenderer.invoke('knowledge-get-dir');
}

/**
 * 下载 PDF 文件（触发浏览器下载行为）
 */
export async function downloadPdf(fileName: string): Promise<void> {
  const result = await readPdf(fileName);
  if (!result.success) {
    throw new Error(result.error || '读取 PDF 失败');
  }

  // 将 base64 转换为 Blob 并触发下载
  const byteCharacters = atob(result.data);
  const byteNumbers = new Array(byteCharacters.length);
  for (let i = 0; i < byteCharacters.length; i++) {
    byteNumbers[i] = byteCharacters.charCodeAt(i);
  }
  const byteArray = new Uint8Array(byteNumbers);
  const blob = new Blob([byteArray], { type: 'application/pdf' });

  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = fileName;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}
